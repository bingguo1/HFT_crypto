#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace hfmm {

// Fixed-point arithmetic: 1 unit = 1e-8
using Price    = int64_t;
using Quantity = int64_t;

constexpr int64_t PRICE_SCALE = 100'000'000LL; // 1e8

inline Price    to_price(double d) { return static_cast<Price>(d * PRICE_SCALE + 0.5); }
inline Quantity to_qty(double d)   { return static_cast<Quantity>(d * PRICE_SCALE + 0.5); }
inline double   from_price(Price p) { return static_cast<double>(p) / PRICE_SCALE; }
inline double   from_qty(Quantity q) { return static_cast<double>(q) / PRICE_SCALE; }

// -----------------------------------------------------------------------
// PriceLevel — a single (price, qty) pair
// -----------------------------------------------------------------------
struct PriceLevel {
    Price    price{0};
    Quantity qty{0};
};

// -----------------------------------------------------------------------
// BookUpdateEvent — stack-allocated; max 64 levels each side
// -----------------------------------------------------------------------
enum class EventKind : uint8_t { Snapshot, Delta };

constexpr int MAX_LEVELS = 64;

struct BookUpdateEvent {
    EventKind  kind{EventKind::Delta};
    uint64_t   first_update_id{0}; // U  (delta only)
    uint64_t   final_update_id{0}; // u
    uint64_t   prev_update_id{0};  // pu (futures: prev event's u, for gap detection)
    uint64_t   last_update_id{0};  // lastUpdateId (snapshot)

    char       symbol[16]{};       // e.g. "BTC-USD\0"

    int        bid_count{0};
    int        ask_count{0};
    std::array<PriceLevel, MAX_LEVELS> bids{};
    std::array<PriceLevel, MAX_LEVELS> asks{};
};

// -----------------------------------------------------------------------
// QuoteDecision — output of strategy
// -----------------------------------------------------------------------
struct QuoteDecision {
    Price    bid_price{0};
    Quantity bid_qty{0};
    Price    ask_price{0};
    Quantity ask_qty{0};
    double   reservation_price{0.0};
    double   optimal_spread{0.0};
    double   sigma{0.0};
    bool     valid{false};
};

// -----------------------------------------------------------------------
// Order — internal representation
// -----------------------------------------------------------------------
enum class Side : uint8_t { Buy, Sell };
enum class OrderStatus : uint8_t { New, PartialFill, Filled, Cancelled };

struct Order {
    uint64_t    order_id{0};
    Side        side{Side::Buy};
    Price       price{0};
    Quantity    orig_qty{0};
    Quantity    filled_qty{0};
    OrderStatus status{OrderStatus::New};
    bool        active{false};
};

// -----------------------------------------------------------------------
// PairConfig — per-symbol A-S parameters
// -----------------------------------------------------------------------
struct PairConfig {
    std::string symbol;
    double gamma{0.1};
    double sigma{0.02};
    double k{1.5};
    double T{60.0};
    double base_qty{0.001};
    double max_inventory{0.01};
    double min_spread_bps{1.0};
    double reprice_threshold_bps{0.5};
    double ema_alpha{0.05};
    int    book_depth{20};
};

// -----------------------------------------------------------------------
// Config — loaded from config.json (global / exchange-level fields)
// -----------------------------------------------------------------------
struct Config {
    // Exchange selector: "coinbase" (default), "binance", "binance_us"
    std::string exchange{"coinbase"};

    std::string ws_endpoint{"wss://advanced-trade-ws.coinbase.com"};
    std::string rest_endpoint{"https://api.coinbase.com"};
    std::string api_key{};
    std::string api_secret{};

    bool   paper_trading{true};
    int    status_interval_ms{1000};

    std::unordered_map<std::string, PairConfig> pairs; // key: symbol
};

} // namespace hfmm
