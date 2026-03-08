#pragma once
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include "hfmm/core/types.hpp"
#include "hfmm/core/ring_buffer.hpp"
#include "hfmm/core/order_book.hpp"
#include "hfmm/feed/market_data_feed.hpp"
#include "hfmm/strategy/avellaneda_stoikov.hpp"
#include "hfmm/execution/binance_rest.hpp"
#include "hfmm/execution/order_manager.hpp"

namespace hfmm {

class Engine {
public:
    explicit Engine(const Config& cfg);
    ~Engine();

    // Start all threads and run until stop() is called
    void run();

    // Signal safe stop (call from signal handler or main thread)
    void stop();

private:
    void strategy_loop();
    void print_status() const;

    Config           cfg_;
    BookEventQueue   queue_;
    OrderBook        book_;
    BinanceRest      rest_;
    std::unique_ptr<IMarketDataFeed> feed_;
    AvellanedaStoikov strategy_;
    OrderManager     order_mgr_;

    std::thread      strategy_thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> events_processed_{0};

    // For elapsed time in A-S time horizon
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace hfmm
