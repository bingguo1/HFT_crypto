#include "hfmm/core/order_book.hpp"
#include <cmath>
#include <iostream>

namespace hfmm {

void OrderBook::reset() {
    bids_.clear();
    asks_.clear();
    last_update_id_ = 0;
    initialized_    = false;
    waiting_first_delta_ = true;
}

void OrderBook::apply_snapshot(const BookUpdateEvent& event) {
    bids_.clear();
    asks_.clear();
    last_update_id_ = event.last_update_id;
    apply_levels(event.bids.data(), event.bid_count, true);
    apply_levels(event.asks.data(), event.ask_count, false);
    initialized_         = true;
    waiting_first_delta_ = true;
}

bool OrderBook::apply_delta(const BookUpdateEvent& event) {
    bool result = true;
    if (!initialized_) return false;

    // Discard stale deltas that arrived before the snapshot
    if (event.final_update_id <= last_update_id_) return true; // already have, skip

    if (waiting_first_delta_) {
        // Binance rule: U <= lastUpdateId+1 <= u
        if (event.first_update_id > last_update_id_ + 1) {
            std::cout<<"waiting first delta, first_update_id="<<event.first_update_id<<" > last_update_id+1="<<last_update_id_ + 1<<"\n";
            result = false;
        }
        waiting_first_delta_ = false;
    } else {
        // Futures: pu must equal the previous event's u.
        // Spot fallback (pu==0): check U == last_update_id_ + 1.
        if (event.prev_update_id != 0) {
            if (event.prev_update_id != last_update_id_) {
                std::cout<<"[order_book] Stale delta detected !!! (prev_update_id="<<event.prev_update_id<<" != last_update_id="<<last_update_id_<<"), need to re-snapshot\n";
                result = false;
            }
        } else {
            if (event.first_update_id != last_update_id_ + 1) 
            {
                std::cout<< "[order_book] Stale delta detected !!! (first_update_id="<<event.first_update_id<<" < last_update_id+1="<<last_update_id_ + 1<<"), need to re-snapshot\n";
                result = false;
            }
        }
    }

    apply_levels(event.bids.data(), event.bid_count, true);
    apply_levels(event.asks.data(), event.ask_count, false);
    last_update_id_ = event.final_update_id;
    return result;
}

void OrderBook::apply_levels(const PriceLevel* levels, int count, bool is_bid) {
    for (int i = 0; i < count; ++i) {
        const auto& lvl = levels[i];
        if (is_bid) {
            if (lvl.qty == 0) bids_.erase(lvl.price);
            else              bids_[lvl.price] = lvl.qty;
        } else {
            if (lvl.qty == 0) asks_.erase(lvl.price);
            else              asks_[lvl.price] = lvl.qty;
        }
    }
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::optional<double> OrderBook::mid_price() const {
    auto bb = best_bid();
    auto ba = best_ask();
    if (!bb || !ba) return std::nullopt;
    return 0.5 * (from_price(*bb) + from_price(*ba));
}

std::optional<double> OrderBook::spread_bps() const {
    auto bb = best_bid();
    auto ba = best_ask();
    if (!bb || !ba) return std::nullopt;
    double mid = 0.5 * (from_price(*bb) + from_price(*ba));
    if (mid <= 0.0) return std::nullopt;
    return (from_price(*ba) - from_price(*bb)) / mid * 10000.0;
}

} // namespace hfmm
