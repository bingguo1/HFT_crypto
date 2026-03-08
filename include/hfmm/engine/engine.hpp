#pragma once
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include <unordered_map>
#include "hfmm/core/types.hpp"
#include "hfmm/core/ring_buffer.hpp"
#include "hfmm/core/order_book.hpp"
#include "hfmm/feed/market_data_feed.hpp"
#include "hfmm/monitoring/market_event_exporter.hpp"
#include "hfmm/strategy/avellaneda_stoikov.hpp"
#include "hfmm/execution/binance_rest.hpp"
#include "hfmm/execution/order_manager.hpp"

namespace hfmm {

struct PairState {
    PairConfig         pcfg;
    OrderBook          book;
    AvellanedaStoikov  strategy;
    OrderManager       order_mgr;
    std::chrono::steady_clock::time_point start_time;

    PairState(const PairConfig& pc, const Config& gc, BinanceRest& rest)
        : pcfg(pc)
        , strategy(pc)
        , order_mgr(gc, pc, rest)
        , start_time(std::chrono::steady_clock::now()) {}
};

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
    BinanceRest      rest_;
    MarketEventExporter telemetry_;
    std::unique_ptr<IMarketDataFeed> feed_;
    std::unordered_map<std::string, PairState> pairs_;

    std::thread      strategy_thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> events_processed_{0};
};

} // namespace hfmm
