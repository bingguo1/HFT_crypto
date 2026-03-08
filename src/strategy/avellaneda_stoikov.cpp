#include "hfmm/strategy/avellaneda_stoikov.hpp"
#include <cmath>
#include <algorithm>

namespace hfmm {

AvellanedaStoikov::AvellanedaStoikov(const PairConfig& pcfg)
    : pcfg_(pcfg), sigma_(pcfg.sigma) {}

void AvellanedaStoikov::update_volatility(double mid_price) {
    if (mid_count_ == 0) {
        prev_mid_ = mid_price;
        ++mid_count_;
        return;
    }
    if (prev_mid_ > 0.0) {
        double log_ret = std::log(mid_price / prev_mid_);
        double sq      = log_ret * log_ret;
        // EMA of squared log-returns
        sigma_ = std::sqrt(pcfg_.ema_alpha * sq + (1.0 - pcfg_.ema_alpha) * sigma_ * sigma_);
    }
    prev_mid_ = mid_price;
    ++mid_count_;
}

QuoteDecision AvellanedaStoikov::compute_quotes(const OrderBook& book,
                                                double inventory,
                                                double elapsed_seconds) const {
    QuoteDecision dec;

    auto mid_opt = book.mid_price();
    if (!mid_opt) return dec;

    const double s    = *mid_opt;
    const double q    = inventory;
    const double gamma = pcfg_.gamma;
    const double sigma = sigma_;
    const double k    = pcfg_.k;
    const double tau  = std::max(pcfg_.T - elapsed_seconds, 0.001); // (T-t)

    // Reservation price: r = s - q * γ * σ² * τ
    const double r = s - q * gamma * sigma * sigma * tau;

    // Optimal spread: δ = γ * σ² * τ + (2/γ) * ln(1 + γ/k)
    const double delta = gamma * sigma * sigma * tau + (2.0 / gamma) * std::log(1.0 + gamma / k);

    // Bid = r - δ/2, Ask = r + δ/2
    double bid_d = r - delta / 2.0;
    double ask_d = r + delta / 2.0;

    // Spread floor
    const double min_spread = s * pcfg_.min_spread_bps * 1e-4;
    if ((ask_d - bid_d) < min_spread) {
        const double adj = (min_spread - (ask_d - bid_d)) / 2.0;
        bid_d -= adj;
        ask_d += adj;
    }

    // Inventory scaling: reduce qty as |q| approaches max_inventory
    double inv_ratio = std::abs(q) / pcfg_.max_inventory;
    inv_ratio = std::min(inv_ratio, 1.0);
    double qty_scale = 1.0 - inv_ratio;

    // If long, prefer to sell → lower bid qty; if short, prefer to buy → lower ask qty
    double bid_qty_d = pcfg_.base_qty * (q >= 0.0 ? qty_scale : 1.0);
    double ask_qty_d = pcfg_.base_qty * (q <= 0.0 ? qty_scale : 1.0);

    // Ensure minimum qty
    const double min_qty = pcfg_.base_qty * 0.1;
    bid_qty_d = std::max(bid_qty_d, min_qty);
    ask_qty_d = std::max(ask_qty_d, min_qty);

    dec.bid_price        = to_price(bid_d);
    dec.bid_qty          = to_qty(bid_qty_d);
    dec.ask_price        = to_price(ask_d);
    dec.ask_qty          = to_qty(ask_qty_d);
    dec.reservation_price = r;
    dec.optimal_spread    = delta;
    dec.sigma             = sigma;
    dec.valid             = true;

    return dec;
}

} // namespace hfmm
