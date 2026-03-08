#include "hfmm/engine/engine.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>

namespace hfmm {

Engine::Engine(const Config& cfg)
    : cfg_(cfg)
    , rest_(cfg_)
    , feed_(make_feed(cfg_, queue_))
    , strategy_(cfg_)
    , order_mgr_(cfg_, rest_) {}

Engine::~Engine() {
    stop();
}

void Engine::run() {
    running_.store(true, std::memory_order_release);
    start_time_ = std::chrono::steady_clock::now();

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
    std::cout << "[engine] Connected. Starting strategy loop.\n";

    // Launch strategy thread
    strategy_thread_ = std::thread([this] { strategy_loop(); });

    // Main thread: periodic status print
    auto last_status = std::chrono::steady_clock::now();
    while (running_.load(std::memory_order_relaxed)) {
        auto now = std::chrono::steady_clock::now();
        auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - last_status).count();
        if (ms >= cfg_.status_interval_ms) {
            print_status();
            last_status = now;
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

        // Apply event to order book
        bool ok = false;
        if (ev->kind == EventKind::Snapshot) {
            book_.apply_snapshot(*ev);
            ok = true;
        } else {
            ok = book_.apply_delta(*ev);
        }

        if (!ok) {
            // Stale/gap detected — need re-snapshot (simplified: just log)
            std::cerr << "[strategy] Gap detected, book may be stale\n";
            continue;
        }

        if (!book_.initialized()) continue;

        // Update volatility
        auto mid = book_.mid_price();
        if (mid) {
            strategy_.update_volatility(*mid);
        }

        // Compute elapsed for A-S time horizon
        double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start_time_).count();

        // Reset time horizon cyclically
        if (elapsed >= cfg_.T) {
            start_time_ = std::chrono::steady_clock::now();
            elapsed = 0.0;
        }

        // Only compute quotes once volatility is warmed up
        if (!strategy_.has_data()) continue;

        auto dec = strategy_.compute_quotes(book_,
                                             order_mgr_.inventory_base(),
                                             elapsed);

        order_mgr_.on_quote_decision(dec, book_);
    }
}

void Engine::print_status() const {
    auto mid = book_.mid_price();
    auto bb  = book_.best_bid();
    auto ba  = book_.best_ask();
    auto spr = book_.spread_bps();

    std::cout << std::fixed << std::setprecision(4)
              << "[status] "
              << "mid=" << (mid ? *mid : 0.0) << " "
              << "bid=" << (bb ? from_price(*bb) : 0.0) << " "
              << "ask=" << (ba ? from_price(*ba) : 0.0) << " "
              << "spread=" << (spr ? *spr : 0.0) << "bps "
              << "inv_base="  << order_mgr_.inventory_base()  << " "
              << "inv_quote=" << order_mgr_.inventory_quote() << " "
              << "pnl="       << order_mgr_.realized_pnl()    << " "
              << "sigma="     << strategy_.sigma()
              << "\n";
}

} // namespace hfmm
