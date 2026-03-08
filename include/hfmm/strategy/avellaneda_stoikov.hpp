#pragma once
#include <cmath>
#include "hfmm/core/types.hpp"
#include "hfmm/core/order_book.hpp"

namespace hfmm {

class AvellanedaStoikov {
public:
    explicit AvellanedaStoikov(const PairConfig& pcfg);

    // Update EMA volatility from a new mid-price observation
    void update_volatility(double mid_price);

    // Compute quote decision given current book and inventory
    // inventory: net base position (positive = long)
    // elapsed_seconds: seconds elapsed in current time horizon
    QuoteDecision compute_quotes(const OrderBook& book,
                                 double inventory,
                                 double elapsed_seconds) const;

    double sigma()     const { return sigma_; }
    bool   has_data()  const { return mid_count_ >= 2; }

private:
    PairConfig    pcfg_;
    double        sigma_;         // current EMA volatility estimate
    double        prev_mid_{0.0}; // previous mid for log-return
    int           mid_count_{0};  // number of observations so far
};

} // namespace hfmm
