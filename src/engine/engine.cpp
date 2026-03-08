#include "hfmm/engine/engine.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <cstring>

namespace hfmm {

Engine::Engine(const Config& cfg)
    : cfg_(cfg)
    , rest_(cfg_)
    , feed_(make_feed(cfg_, queue_))
{
    for (const auto& [sym, pc] : cfg_.pairs)
        pairs_.try_emplace(pc.symbol, pc, cfg_, rest_);
}

Engine::~Engine() {
    stop();
}

void Engine::run() {
    running_.store(true, std::memory_order_release);
    events_processed_.store(0, std::memory_order_relaxed);

    // Start WebSocket feed (runs on ixwebsocket internal thread)
    feed_->start();

    // Wait for connection
    std::cout << "[engine] Connecting to " << cfg_.exchange << " WebSocket ("
              << cfg_.ws_endpoint << ")...\n";
    for (int i = 0; i < 50 && !feed_->connected(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!feed_->connected()) {
        std::cerr << "[engine] Failed to connect to WebSocket.\n";
        running_.store(false);
        return;
    }
    std::cout << "[engine] Connected. Starting strategy loop for "
              << pairs_.size() << " pair(s).\n";

    // Launch strategy thread
    strategy_thread_ = std::thread([this] { strategy_loop(); });

    // Main thread: periodic status print
    auto last_status = std::chrono::steady_clock::now();
    auto last_feed_stats = last_status;
    uint64_t last_event_count = 0;
    constexpr auto kFeedStatsInterval = std::chrono::seconds(10);
    while (running_.load(std::memory_order_relaxed)) {
        auto now = std::chrono::steady_clock::now();
        auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - last_status).count();
        if (ms >= cfg_.status_interval_ms) {
            print_status();
            last_status = now;
        }

        if (now - last_feed_stats >= kFeedStatsInterval) {
            const auto total = events_processed_.load(std::memory_order_relaxed);
            const auto window = total - last_event_count;
            const auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                now - last_feed_stats).count();
            const double eps = secs > 0 ? static_cast<double>(window) / static_cast<double>(secs) : 0.0;

            std::cout << std::fixed << std::setprecision(2)
                      << "[feed] events=" << window
                      << " in " << secs << "s"
                      << " (" << eps << " ev/s), total=" << total << "\n";

            last_feed_stats = now;
            last_event_count = total;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (strategy_thread_.joinable()) strategy_thread_.join();
    feed_->stop();
    std::cout << "[engine] Stopped.\n";
}

void Engine::stop() {
    running_.store(false, std::memory_order_release);
}

void Engine::strategy_loop() {
    while (running_.load(std::memory_order_relaxed)) {
        auto ev = queue_.pop();
        if (!ev) {
            // Empty — spin a little to reduce latency
            std::this_thread::yield();
            continue;
        }

        events_processed_.fetch_add(1, std::memory_order_relaxed);

        // Dispatch by symbol
        auto it = pairs_.find(ev->symbol);
        if (it == pairs_.end()) {
            std::cerr << "[strategy] Unknown symbol: " << ev->symbol << "\n";
            continue;
        }
        auto& s = it->second;

        // Apply event to order book
        bool ok = false;
        if (ev->kind == EventKind::Snapshot) {
            s.book.apply_snapshot(*ev);
            ok = true;
        } else {
            ok = s.book.apply_delta(*ev);
        }

        if (!ok) {
            std::cerr << "[strategy] Gap detected for " << ev->symbol << ", book may be stale\n";
            continue;
        }

        if (!s.book.initialized()) continue;

        // Update volatility
        auto mid = s.book.mid_price();
        if (mid) {
            s.strategy.update_volatility(*mid);
        }

        // Compute elapsed for A-S time horizon
        double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - s.start_time).count();

        // Reset time horizon cyclically
        if (elapsed >= s.pcfg.T) {
            s.start_time = std::chrono::steady_clock::now();
            elapsed = 0.0;
        }

        // Only compute quotes once volatility is warmed up
        if (!s.strategy.has_data()) continue;

        auto dec = s.strategy.compute_quotes(s.book,
                                              s.order_mgr.inventory_base(),
                                              elapsed);

        s.order_mgr.on_quote_decision(dec, s.book);
    }
}

void Engine::print_status() const {
    for (const auto& [sym, s] : pairs_) {
        auto mid = s.book.mid_price();
        auto bb  = s.book.best_bid();
        auto ba  = s.book.best_ask();
        auto spr = s.book.spread_bps();

        std::cout << std::fixed << std::setprecision(4)
                  << "[status] " << sym << " "
                  << "mid=" << (mid ? *mid : 0.0) << " "
                  << "bid=" << (bb ? from_price(*bb) : 0.0) << " "
                  << "ask=" << (ba ? from_price(*ba) : 0.0) << " "
                  << "spread=" << (spr ? *spr : 0.0) << "bps "
                  << "inv_base="  << s.order_mgr.inventory_base()  << " "
                  << "inv_quote=" << s.order_mgr.inventory_quote() << " "
                  << "pnl="       << s.order_mgr.realized_pnl()    << " "
                  << "sigma="     << s.strategy.sigma()
                  << "\n";
    }
}

} // namespace hfmm
