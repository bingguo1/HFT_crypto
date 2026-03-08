#include "hfmm/execution/order_manager.hpp"
#include <cmath>
#include <algorithm>

namespace hfmm {

OrderManager::OrderManager(const Config& cfg, BinanceRest& rest)
    : cfg_(cfg), rest_(rest) {
    // Start with some quote currency so we can buy
    inventory_quote_ = 1000.0; // USDT — paper initial balance
}

void OrderManager::on_quote_decision(const QuoteDecision& decision,
                                     const OrderBook& book) {
    if (!decision.valid) return;

    // --- Bid side ---
    if (bid_.active) {
        if (should_reprice(bid_.price, decision.bid_price)) {
            cancel_bid();
            place_bid(decision.bid_price, decision.bid_qty);
        }
    } else {
        place_bid(decision.bid_price, decision.bid_qty);
    }

    // --- Ask side ---
    if (ask_.active) {
        if (should_reprice(ask_.price, decision.ask_price)) {
            cancel_ask();
            place_ask(decision.ask_price, decision.ask_qty);
        }
    } else {
        place_ask(decision.ask_price, decision.ask_qty);
    }

    // Simulate fills in paper mode
    if (cfg_.paper_trading) {
        simulate_fills(book);
    }
}

void OrderManager::simulate_fills(const OrderBook& book) {
    // Fill bid if best ask <= bid price (passive fill scenario)
    if (bid_.active) {
        auto ba = book.best_ask();
        if (ba && *ba <= bid_.price) {
            double qty_d  = from_qty(bid_.orig_qty);
            double price_d = from_price(bid_.price);
            inventory_base_  += qty_d;
            inventory_quote_ -= qty_d * price_d;
            bid_.filled_qty = bid_.orig_qty;
            bid_.status     = OrderStatus::Filled;
            bid_.active     = false;
        }
    }

    // Fill ask if best bid >= ask price
    if (ask_.active) {
        auto bb = book.best_bid();
        if (bb && *bb >= ask_.price) {
            double qty_d   = from_qty(ask_.orig_qty);
            double price_d = from_price(ask_.price);
            inventory_base_  -= qty_d;
            inventory_quote_ += qty_d * price_d;
            ask_.filled_qty = ask_.orig_qty;
            ask_.status     = OrderStatus::Filled;
            ask_.active     = false;

            // Compute realized PnL if we have a round trip estimate
            // Simple: quote gained minus cost of base at current mid
            auto mid = book.mid_price();
            if (mid) {
                realized_pnl_ = inventory_quote_ + inventory_base_ * (*mid) - 1000.0;
            }
        }
    }
}

bool OrderManager::should_reprice(Price old_price, Price new_price) const {
    if (old_price == 0) return true;
    double diff_bps = std::abs(static_cast<double>(new_price - old_price))
                      / static_cast<double>(old_price) * 10000.0;
    return diff_bps > cfg_.reprice_threshold_bps;
}

void OrderManager::place_bid(Price price, Quantity qty) {
    auto resp = rest_.place_order(Side::Buy, price, qty);
    if (resp.success) {
        bid_.order_id  = resp.order_id;
        bid_.side      = Side::Buy;
        bid_.price     = price;
        bid_.orig_qty  = qty;
        bid_.filled_qty = 0;
        bid_.status    = OrderStatus::New;
        bid_.active    = true;
    }
}

void OrderManager::place_ask(Price price, Quantity qty) {
    auto resp = rest_.place_order(Side::Sell, price, qty);
    if (resp.success) {
        ask_.order_id  = resp.order_id;
        ask_.side      = Side::Sell;
        ask_.price     = price;
        ask_.orig_qty  = qty;
        ask_.filled_qty = 0;
        ask_.status    = OrderStatus::New;
        ask_.active    = true;
    }
}

void OrderManager::cancel_bid() {
    if (bid_.active) {
        rest_.cancel_order(bid_.order_id);
        bid_.active = false;
        bid_.status = OrderStatus::Cancelled;
    }
}

void OrderManager::cancel_ask() {
    if (ask_.active) {
        rest_.cancel_order(ask_.order_id);
        ask_.active = false;
        ask_.status = OrderStatus::Cancelled;
    }
}

} // namespace hfmm
