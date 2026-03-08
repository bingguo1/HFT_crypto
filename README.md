# HF Market Making — C++ / Binance / Avellaneda-Stoikov

A low-latency market making engine targeting Binance spot markets. The system subscribes to Binance's WebSocket depth feed, maintains a local L2 order book, computes optimal bid/ask quotes using the Avellaneda-Stoikov model, and manages a single resting bid and ask order. Paper trading mode is on by default; live trading requires API credentials.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Directory Layout](#directory-layout)
3. [Components](#components)
   - [types.hpp — Core Types](#typeshpp--core-types)
   - [SpscRingBuffer — Lock-Free Queue](#spscringbuffer--lock-free-queue)
   - [OrderBook — L2 Book](#orderbook--l2-book)
   - [MarketDataFeed — WebSocket Feed](#marketdatafeed--websocket-feed)
   - [AvellanedaStoikov — Strategy](#avellanedastoikov--strategy)
   - [BinanceRest — REST Client](#binancerest--rest-client)
   - [OrderManager — Order Lifecycle](#ordermanager--order-lifecycle)
   - [Engine — Orchestrator](#engine--orchestrator)
4. [Avellaneda-Stoikov Model](#avellaneda-stoikov-model)
5. [Thread Model and Data Flow](#thread-model-and-data-flow)
6. [Configuration Reference](#configuration-reference)
7. [Dependencies](#dependencies)
8. [Building](#building)
9. [Running](#running)
10. [Testing](#testing)
11. [Going Live](#going-live)

---

## Architecture Overview

```
                    Binance WebSocket
                         │
                    ┌────▼────────────┐
                    │ MarketDataFeed  │  ixwebsocket thread
                    │  (WS parser)   │
                    └────┬────────────┘
                         │ BookUpdateEvent (stack-allocated)
                         ▼
                   SpscRingBuffer<1024>     ← lock-free SPSC
                         │
                    ┌────▼────────────┐
                    │ Strategy Thread │
                    │                 │
                    │  OrderBook      │  apply snapshot / delta
                    │     │           │
                    │  AvellanedaStoikov  compute bid/ask prices
                    │     │           │
                    │  OrderManager   │  reprice / simulate fills
                    └─────────────────┘
                         │
               ┌─────────▼──────────┐
               │   BinanceRest      │  paper: no-op
               │  (libcurl + HMAC)  │  live:  POST /api/v3/order
               └────────────────────┘

         Main thread: status prints, signal handling
```

The system is intentionally single-market (one symbol, one bid, one ask) to keep the hot path simple and auditable.

---

## Directory Layout

```
hf_marketmaking/
├── CMakeLists.txt
├── cmake/
│   └── Dependencies.cmake      # FetchContent for all third-party deps
├── include/hfmm/
│   ├── core/
│   │   ├── types.hpp           # All shared data types
│   │   ├── order_book.hpp      # L2 OrderBook declaration
│   │   └── ring_buffer.hpp     # SpscRingBuffer<T, N> (header-only)
│   ├── feed/
│   │   └── market_data_feed.hpp
│   ├── strategy/
│   │   └── avellaneda_stoikov.hpp
│   ├── execution/
│   │   ├── order_manager.hpp
│   │   └── binance_rest.hpp
│   └── engine/
│       └── engine.hpp
├── src/                        # .cpp implementations
│   ├── core/order_book.cpp
│   ├── feed/market_data_feed.cpp
│   ├── strategy/avellaneda_stoikov.cpp
│   ├── execution/binance_rest.cpp
│   ├── execution/order_manager.cpp
│   └── engine/engine.cpp
├── app/
│   └── main.cpp               # Entry point
├── tests/
│   ├── CMakeLists.txt
│   ├── test_order_book.cpp
│   ├── test_avellaneda_stoikov.cpp
│   └── test_ring_buffer.cpp
└── config/
    └── config.json            # Runtime parameters
```

---

## Components

### `types.hpp` — Core Types

**File:** `include/hfmm/core/types.hpp`

All monetary values are stored as `int64_t` scaled by 10⁸ (i.e. 1 unit = 10⁻⁸). This eliminates floating-point comparison issues at price boundaries and avoids rounding drift in accumulated inventory.

```cpp
using Price    = int64_t;   // 50000.00 BTC/USDT → 5000000000000LL
using Quantity = int64_t;   // 0.001 BTC          → 100000LL

Price    to_price(double d);    // double → fixed-point
Quantity to_qty(double d);
double   from_price(Price p);   // fixed-point → double
double   from_qty(Quantity q);
```

Key structs:

| Struct | Purpose |
|---|---|
| `PriceLevel` | A single `{price, qty}` pair |
| `BookUpdateEvent` | Stack-allocated event carrying up to 64 bid/ask levels; kind is `Snapshot` or `Delta` |
| `QuoteDecision` | Output of the strategy: bid/ask prices+quantities, reservation price, optimal spread, sigma |
| `Order` | Internal representation of a live resting order |
| `Config` | All runtime parameters; loaded from `config.json` at startup |

`BookUpdateEvent` is deliberately stack-allocated (no heap) and sized to pass through the ring buffer by value without dynamic allocation.

---

### `SpscRingBuffer` — Lock-Free Queue

**File:** `include/hfmm/core/ring_buffer.hpp`

A single-producer single-consumer ring buffer. The WebSocket callback (producer) and the strategy loop (consumer) exchange `BookUpdateEvent` values through this queue without any mutex.

```cpp
template <typename T, std::size_t Capacity>
class SpscRingBuffer;

using BookEventQueue = SpscRingBuffer<BookUpdateEvent, 1024>;
```

Design choices:

- **Power-of-2 capacity** — index wrapping uses a bitmask (`& MASK`) instead of modulo.
- **Cache-line padding** — `head_` and `tail_` atomics are each `alignas(64)`, placed on separate cache lines to prevent false sharing between the producer and consumer cores.
- **Non-blocking** — `push()` returns `false` if full; `pop()` returns `std::nullopt` if empty. Neither spins or blocks.
- **Memory ordering** — `push` uses `relaxed` load of head, `acquire` load of tail, `release` store of new head. `pop` is symmetric. This is the minimal correct ordering for x86 and ARM.

---

### `OrderBook` — L2 Book

**File:** `include/hfmm/core/order_book.hpp` / `src/core/order_book.cpp`

Maintains a full L2 order book for one symbol.

```
Bids: std::map<Price, Quantity, std::greater<>>   — highest price first
Asks: std::map<Price, Quantity>                    — lowest price first
```

**Binance snapshot + replay protocol**

Binance's diff-depth WebSocket stream requires a specific bootstrap sequence to avoid gaps:

1. Connect to WebSocket and **buffer** all incoming delta events (do not apply yet).
2. Fetch a REST depth snapshot (`GET /api/v3/depth`). Record its `lastUpdateId`.
3. Discard buffered deltas where `final_update_id <= lastUpdateId`.
4. The first valid delta must satisfy: `first_update_id (U) <= lastUpdateId + 1 <= final_update_id (u)`.
5. Apply remaining buffered deltas in order, then continue with the live stream.

`apply_delta()` enforces this by returning `false` on any sequence gap, signalling the engine to re-snapshot.

Key methods:

| Method | Description |
|---|---|
| `apply_snapshot(event)` | Replaces entire book; resets `last_update_id_` |
| `apply_delta(event)` | Returns `false` on gap; qty == 0 removes the level |
| `best_bid()` | `std::optional<Price>` — best bid price |
| `best_ask()` | `std::optional<Price>` — best ask price |
| `mid_price()` | `(best_bid + best_ask) / 2` as `double` |
| `spread_bps()` | `(ask - bid) / mid * 10000` |

---

### `MarketDataFeed` — WebSocket Feed

**File:** `include/hfmm/feed/market_data_feed.hpp` / `src/feed/market_data_feed.cpp`

Manages the Binance WebSocket connection and snapshot bootstrapping.

**WebSocket parsing (hot path)**

`on_message()` is called on ixwebsocket's internal thread for every incoming frame. It uses the **simdjson ondemand API** to parse the JSON payload without copying strings. Price and quantity strings are converted to `double` using `std::from_chars` (zero-allocation, no locale overhead), then immediately converted to fixed-point integers.

The parsed `BookUpdateEvent` is pushed onto the `SpscRingBuffer`. If the snapshot has not yet been applied, events are buffered in a small on-stack array (`pre_snap_buf_`) and replayed after the snapshot arrives.

**REST snapshot**

`fetch_rest_snapshot()` performs a blocking `GET /api/v3/depth` via libcurl, parses the response with nlohmann/json (off the hot path), and pushes a `Snapshot` event followed by any buffered deltas that post-date it.

---

### `AvellanedaStoikov` — Strategy

**File:** `include/hfmm/strategy/avellaneda_stoikov.hpp` / `src/strategy/avellaneda_stoikov.cpp`

Implements the Avellaneda-Stoikov (2008) market making model.

#### Formulas

Let:
- `s` = current mid-price
- `q` = net inventory in base currency (positive = long)
- `γ` = risk aversion coefficient
- `σ` = volatility estimate (updated online)
- `k` = order arrival rate parameter
- `τ` = time remaining in horizon (`T - t`)

**Reservation price** — the price at which the market maker is indifferent to buying or selling given their current inventory:

```
r = s - q · γ · σ² · τ
```

A long inventory (`q > 0`) pulls `r` below mid, incentivising the strategy to post lower bid prices and attract sells to reduce inventory. A short inventory has the opposite effect.

**Optimal spread** — derived from the market maker's HJB equation:

```
δ = γ · σ² · τ + (2/γ) · ln(1 + γ/k)
```

**Quote prices:**

```
bid = r - δ/2
ask = r + δ/2
```

#### Online Volatility (EMA)

`sigma` is updated on every mid-price tick using an EMA of squared log-returns:

```
log_ret  = ln(mid_t / mid_{t-1})
sigma²_t = alpha · log_ret² + (1 - alpha) · sigma²_{t-1}
```

`alpha = 0.05` gives a half-life of roughly 14 ticks.

#### Inventory Scaling

Quote quantities are linearly scaled down as `|q|` approaches `max_inventory`:

```
scale = 1 - |q| / max_inventory     (clamped to [0, 1])

bid_qty = base_qty · scale     (when long, reduce bid aggression)
ask_qty = base_qty · scale     (when short, reduce ask aggression)
```

A minimum quantity of `base_qty × 0.1` is always maintained so the book side is never abandoned entirely.

#### Spread Floor

If the computed spread is narrower than `min_spread_bps`, it is symmetrically widened:

```
min_spread = mid · min_spread_bps · 1e-4
if (ask - bid) < min_spread:
    adj = (min_spread - (ask - bid)) / 2
    bid -= adj
    ask += adj
```

This prevents quoting inside the natural market spread at low-volatility periods near the end of the time horizon.

---

### `BinanceRest` — REST Client

**File:** `include/hfmm/execution/binance_rest.hpp` / `src/execution/binance_rest.cpp`

Wraps a single reused `CURL*` handle for all REST calls. The response body is written into a pre-allocated `std::string` (`response_buf_`) to avoid per-call heap allocation.

**Request signing**

Authenticated endpoints (order placement, cancellation) require HMAC-SHA256 of the full query string, appended as `&signature=<hex>`. The timestamp is added just before signing so the signature always covers a fresh timestamp within Binance's 5-second window.

```
params = "symbol=BTCUSDT&side=BUY&...&timestamp=<unix_ms>"
signature = HMAC-SHA256(api_secret, params)
body = params + "&signature=" + signature
```

**Paper mode**

When `paper_trading = true`, the `CURL*` handle is never initialised. `place_order()` returns a synthetic `OrderResponse` with an auto-incrementing local ID. `cancel_order()` is a no-op returning `true`.

---

### `OrderManager` — Order Lifecycle

**File:** `include/hfmm/execution/order_manager.hpp` / `src/execution/order_manager.cpp`

Maintains exactly one active bid and one active ask at all times.

**Repricing logic**

On each `QuoteDecision`, the new target price is compared to the currently resting price. A reprice only happens if the price has moved by more than `reprice_threshold_bps`:

```
|new_price - old_price| / old_price × 10000 > reprice_threshold_bps
```

This prevents excessive order churn when the book ticks by tiny amounts, which would consume rate-limit budget and generate unnecessary noise.

**Paper fill simulation**

In paper mode, fills are simulated against the current best bid/ask after each quote update:

- Bid fills if `best_ask ≤ bid_price` (our bid is at or through the market)
- Ask fills if `best_bid ≥ ask_price` (our ask is at or through the market)

Inventory is updated immediately on fill. Realised PnL is computed as:

```
pnl = inventory_quote + inventory_base × mid_price - initial_quote
```

The paper account starts with 1000 USDT and 0 BTC.

---

### `Engine` — Orchestrator

**File:** `include/hfmm/engine/engine.hpp` / `src/engine/engine.cpp`

Owns all components and manages the two-thread lifecycle.

**Startup sequence:**

1. Start `MarketDataFeed` (WS connects, events buffered internally).
2. Wait up to 5 seconds for WebSocket connection.
3. Call `fetch_rest_snapshot()` — blocks until snapshot is fetched and buffered deltas replayed.
4. Launch strategy thread.
5. Main thread enters status-print loop.

**Strategy loop (hot path):**

```
queue_.pop()              ~10 ns   — atomic load
apply_snapshot/delta()    ~100–500 ns — std::map insert/erase
update_volatility()       ~10 ns   — EMA update
compute_quotes()          ~50 ns   — arithmetic
simulate_fills()          ~50 ns   — two comparisons
on_quote_decision()       ~50 ns   — conditional reprice
```

On an empty queue the loop calls `std::this_thread::yield()` rather than spinning at 100% CPU.

**Time horizon reset**

The A-S model uses a finite time horizon `T`. When `elapsed >= T`, the horizon clock resets to keep `τ` bounded away from zero (preventing degenerate near-zero spreads at the end of the period).

---

## Avellaneda-Stoikov Model

For intuition on the parameters:

| Parameter | Effect |
|---|---|
| `gamma` (γ) | Higher → wider spread, more inventory risk aversion, stronger skew |
| `sigma` (σ) | Higher → wider spread; updated online via EMA |
| `k` | Higher → narrower spread (more frequent order arrivals assumed) |
| `T` | Horizon length in seconds; spread narrows as `t → T` |
| `base_qty` | Base order size in BTC |
| `max_inventory` | Net position limit; above this, qty scaling kicks in |
| `min_spread_bps` | Hard floor on spread regardless of model output |

The key intuition: the model explicitly trades off **inventory risk** against **fill frequency**. A risk-neutral market maker (`γ → 0`) posts at the mid with zero spread and captures maximum flow but accumulates unbounded inventory. Increasing `γ` widens the spread and introduces the skew that actively steers inventory back to zero.

---

## Thread Model and Data Flow

```
Thread              Owns                    Communicates via
──────────────────────────────────────────────────────────
ixwebsocket (WS)    WebSocket connection    SpscRingBuffer (push)
Strategy thread     OrderBook, Strategy,    SpscRingBuffer (pop)
                    OrderManager
Main thread         Engine lifecycle        std::atomic<bool> running_
Watcher thread      Signal monitoring       std::atomic<bool> g_shutdown
```

No mutex is used on the hot path. The only synchronisation primitives are:
- `SpscRingBuffer` — acquire/release atomics on head/tail
- `running_` — relaxed store from main/watcher, relaxed load in strategy loop
- `connected_` / `snapshot_done_` — release store in WS thread, acquire load in main thread

---

## Configuration Reference

All parameters live in `config/config.json`:

```json
{
  "symbol":               "BTCUSDT",
  "ws_endpoint":          "wss://testnet.binance.vision",
  "rest_endpoint":        "https://testnet.binance.vision",
  "api_key":              "",
  "api_secret":           "",

  "gamma":                0.1,    // risk aversion [0.01 – 1.0]
  "sigma":                0.02,   // initial volatility; updated online
  "k":                    1.5,    // order arrival rate
  "T":                    60.0,   // time horizon seconds
  "base_qty":             0.001,  // BTC per order
  "max_inventory":        0.01,   // max net BTC position
  "min_spread_bps":       1.0,    // minimum spread floor
  "reprice_threshold_bps":0.5,    // minimum move to trigger reprice
  "ema_alpha":            0.05,   // EMA weight for volatility (~14-tick half-life)

  "book_depth":           20,     // levels to fetch in REST snapshot
  "paper_trading":        true,   // false = live orders
  "status_interval_ms":   1000    // console status print frequency
}
```

**Testnet vs mainnet endpoints:**

| Mode | ws_endpoint | rest_endpoint |
|---|---|---|
| Testnet | `wss://testnet.binance.vision` | `https://testnet.binance.vision` |
| Mainnet | `wss://stream.binance.com:9443` | `https://api.binance.com` |

---

## Dependencies

All fetched automatically by CMake via `FetchContent` (no manual installation needed):

| Library | Version | Use |
|---|---|---|
| [ixwebsocket](https://github.com/machinezone/IXWebSocket) | v11.4.5 | WebSocket client with TLS |
| [simdjson](https://github.com/simdjson/simdjson) | v3.10.1 | Fast JSON parsing on hot path |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | Config loading and snapshot parsing |
| [googletest](https://github.com/google/googletest) | v1.14.0 | Unit tests |

System dependencies (must be installed):

```bash
# Ubuntu / Debian
sudo apt install libcurl4-openssl-dev libssl-dev cmake build-essential

# Fedora / RHEL
sudo dnf install libcurl-devel openssl-devel cmake gcc-c++
```

---

## Building

```bash
# Configure (Debug build with AddressSanitizer + UBSan)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Configure (Release build: -O3 -march=native)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build everything
cmake --build build -j$(nproc)
```

The build produces:
- `build/hfmm` — the main executable
- `build/tests/test_order_book`
- `build/tests/test_avellaneda_stoikov`
- `build/tests/test_ring_buffer`
- `build/compile_commands.json` — for clangd / IDE integration

---

## Running

```bash
# Paper trading against Binance testnet (default config)
./build/hfmm

# Custom config path
./build/hfmm /path/to/my_config.json
```

Sample console output:

```
[main] HF Market Maker starting
  symbol:        BTCUSDT
  paper_trading: yes
  gamma:         0.1
  ...
[engine] Connecting to Binance WebSocket...
[engine] Connected. Fetching snapshot...
[engine] Snapshot received. Starting strategy loop.
[status] mid=84123.5000 bid=84122.9500 ask=84124.0500 spread=0.1312bps inv_base=0.0000 inv_quote=1000.0000 pnl=0.0000 sigma=0.0198
[status] mid=84124.0000 bid=84123.4500 ask=84124.5500 spread=0.1311bps inv_base=0.0010 inv_quote=915.8769 pnl=0.0231 sigma=0.0201
...
```

Stop with `Ctrl+C` (SIGINT) or `kill <pid>` (SIGTERM).

---

## Testing

```bash
# Run all tests
cd build && ctest --output-on-failure

# Run individual test binary
./build/tests/test_order_book
./build/tests/test_avellaneda_stoikov
./build/tests/test_ring_buffer
```

### Test coverage

**`test_order_book`** (7 tests)
- `SnapshotApply` — levels loaded, best bid/ask correct, mid/spread math
- `DeltaApply` — qty updated, `last_update_id` advanced
- `StaleReject` — delta with `final_update_id <= last_update_id` silently skipped
- `GapReject` — `apply_delta` returns `false` when sequence gap detected
- `QtyZeroRemoval` — level with qty=0 removed from book
- `MidAndSpread` — arithmetic correctness
- `EmptyBook` — all accessors return `nullopt`

**`test_avellaneda_stoikov`** (5 tests)
- `ZeroInventorySymmetric` — bid and ask equidistant from reservation price when `q=0`
- `PositiveInventoryBidSkewedDown` — reservation price and bid price lower when long
- `SpreadFloor` — spread never narrows below `min_spread_bps`
- `InventoryClamping` — bid qty scaled to minimum when inventory ≥ max
- `VolatilityUpdate` — EMA sigma changes after two mid-price observations

**`test_ring_buffer`** (6 tests)
- `PushPop` — basic round-trip
- `FullQueueReturnsFalse` — push on full buffer returns `false`
- `EmptyReturnsNullopt` — pop on empty buffer returns `std::nullopt`
- `WrapAround` — correct behaviour across index wraparound
- `SpscThreaded` — 100,000 items, producer thread / consumer main thread, order preserved
- `CapacityIsPowerOfTwo` — static assertion + runtime check

---

## Going Live

> **Warning:** Live trading sends real orders to Binance and risks real funds. Start with the smallest possible `base_qty` and `max_inventory` and monitor closely.

1. Obtain Binance API credentials and enable spot trading permission.
2. Edit `config/config.json`:
   ```json
   {
     "ws_endpoint":   "wss://stream.binance.com:9443",
     "rest_endpoint": "https://api.binance.com",
     "api_key":       "<your_key>",
     "api_secret":    "<your_secret>",
     "paper_trading": false,
     "base_qty":      0.001,
     "max_inventory": 0.005
   }
   ```
3. Build in Release mode: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)`
4. Run and observe the first few order placements in the Binance UI before leaving unattended.

**Binance rate limits to be aware of:**
- 1200 request weight per minute (depth snapshot = 10 weight)
- 10 orders per second per symbol
- The engine places at most 2 orders per strategy tick (one bid, one ask) and only reprices when the price has moved by `reprice_threshold_bps`
