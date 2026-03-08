#pragma once
#include <map>
#include <functional>
#include <optional>
#include "hfmm/core/types.hpp"

namespace hfmm {

class OrderBook {
public:
    // Bids descending, Asks ascending
    using BidMap = std::map<Price, Quantity, std::greater<Price>>;
    using AskMap = std::map<Price, Quantity>;

    OrderBook() = default;

    // Apply a snapshot event — replaces entire book
    void apply_snapshot(const BookUpdateEvent& event);

    // Apply a delta event. Returns false if stale (should re-snapshot).
    // Binance rule: first valid delta needs U <= lastUpdateId+1 <= u
    bool apply_delta(const BookUpdateEvent& event);

    // Reset — used before re-snapshotting
    void reset();

    // Accessors
    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    std::optional<double> mid_price() const;
    std::optional<double> spread_bps() const;

    const BidMap& bids() const { return bids_; }
    const AskMap& asks() const { return asks_; }

    uint64_t last_update_id() const { return last_update_id_; }
    bool     initialized()    const { return initialized_; }

private:
    void apply_levels(const PriceLevel* levels, int count, bool is_bid);

    BidMap   bids_;
    AskMap   asks_;
    uint64_t last_update_id_{0};
    bool     initialized_{false};
    bool     waiting_first_delta_{true};
};

} // namespace hfmm
