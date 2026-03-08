#pragma once
#include <memory>
#include "hfmm/core/types.hpp"
#include "hfmm/core/order_book.hpp"
#include "hfmm/execution/binance_rest.hpp"

namespace hfmm {

class OrderManager {
public:
    explicit OrderManager(const Config& cfg, const PairConfig& pcfg, BinanceRest& rest);

    // Called by strategy loop with new quote decision.
    // Handles repricing (avoid churn) and fill simulation (paper mode).
    void on_quote_decision(const QuoteDecision& decision, const OrderBook& book);

    // Simulate fills against current best bid/ask (paper mode only).
    void simulate_fills(const OrderBook& book);

    // Accessors
    double inventory_base()  const { return inventory_base_; }
    double inventory_quote() const { return inventory_quote_; }
    double realized_pnl()    const { return realized_pnl_; }

    const Order& active_bid() const { return bid_; }
    const Order& active_ask() const { return ask_; }

private:
    bool should_reprice(Price old_price, Price new_price) const;

    void place_bid(Price price, Quantity qty);
    void place_ask(Price price, Quantity qty);
    void cancel_bid();
    void cancel_ask();

    const Config& cfg_;
    PairConfig    pcfg_;
    BinanceRest&  rest_;

    Order    bid_{};
    Order    ask_{};

    double   inventory_base_{0.0};   // base currency (e.g. BTC)
    double   inventory_quote_{0.0};  // quote currency (e.g. USDT)
    double   realized_pnl_{0.0};

    uint64_t next_paper_id_{1};
};

} // namespace hfmm
