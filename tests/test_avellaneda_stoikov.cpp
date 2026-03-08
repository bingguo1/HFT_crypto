#include <gtest/gtest.h>
#include "hfmm/strategy/avellaneda_stoikov.hpp"
#include "hfmm/core/order_book.hpp"

using namespace hfmm;

// Build a simple book with given bid/ask
static OrderBook make_book(double bid, double ask) {
    OrderBook book;
    BookUpdateEvent snap;
    snap.kind            = EventKind::Snapshot;
    snap.last_update_id  = 1;
    snap.bid_count       = 1;
    snap.ask_count       = 1;
    snap.bids[0]         = {to_price(bid), to_qty(1.0)};
    snap.asks[0]         = {to_price(ask), to_qty(1.0)};
    book.apply_snapshot(snap);
    return book;
}

// Helper: build default config
static Config default_cfg() {
    Config cfg;
    cfg.gamma             = 0.1;
    cfg.sigma             = 0.02;
    cfg.k                 = 1.5;
    cfg.T                 = 60.0;
    cfg.base_qty          = 0.001;
    cfg.max_inventory     = 0.01;
    cfg.min_spread_bps    = 0.0; // disable floor for most tests
    cfg.reprice_threshold_bps = 0.5;
    cfg.ema_alpha         = 0.05;
    return cfg;
}

// -----------------------------------------------------------------------
TEST(AvellanedaStoikov, ZeroInventorySymmetric) {
    auto cfg = default_cfg();
    AvellanedaStoikov as(cfg);
    // Feed a few mid prices so has_data() is true
    as.update_volatility(50000.0);
    as.update_volatility(50001.0);

    auto book = make_book(49999.0, 50001.0);
    auto dec  = as.compute_quotes(book, 0.0, 0.0);

    EXPECT_TRUE(dec.valid);

    // At zero inventory, bid and ask should be symmetric around mid
    double mid   = 50000.0;
    double r     = dec.reservation_price;
    EXPECT_NEAR(r, mid, 1.0); // close to mid when q=0

    // Symmetric: bid should be equally far from r as ask
    double bid_d = from_price(dec.bid_price);
    double ask_d = from_price(dec.ask_price);
    EXPECT_NEAR(r - bid_d, ask_d - r, 1e-4);
}

// -----------------------------------------------------------------------
TEST(AvellanedaStoikov, PositiveInventoryBidSkewedDown) {
    auto cfg = default_cfg();
    AvellanedaStoikov as(cfg);
    as.update_volatility(50000.0);
    as.update_volatility(50001.0);

    auto book = make_book(49999.0, 50001.0);

    // Large positive inventory: reservation price should be below mid
    double inv = 0.005; // half of max
    auto dec_pos = as.compute_quotes(book, inv, 0.0);
    auto dec_zero = as.compute_quotes(book, 0.0, 0.0);

    EXPECT_TRUE(dec_pos.valid);
    // Reservation price with positive inv should be lower
    EXPECT_LT(dec_pos.reservation_price, dec_zero.reservation_price);
    // Bid price with positive inv should be lower than zero-inv bid
    EXPECT_LT(from_price(dec_pos.bid_price), from_price(dec_zero.bid_price));
}

// -----------------------------------------------------------------------
TEST(AvellanedaStoikov, SpreadFloor) {
    auto cfg = default_cfg();
    cfg.min_spread_bps = 10.0; // 10 bps floor
    AvellanedaStoikov as(cfg);
    as.update_volatility(50000.0);
    as.update_volatility(50000.5);

    auto book = make_book(49999.0, 50001.0);
    // Use very low sigma to force natural spread < floor
    auto dec = as.compute_quotes(book, 0.0, 59.9); // near end of horizon → small spread

    EXPECT_TRUE(dec.valid);

    double bid_d = from_price(dec.bid_price);
    double ask_d = from_price(dec.ask_price);
    double mid   = 50000.0;
    double spread_bps = (ask_d - bid_d) / mid * 10000.0;
    EXPECT_GE(spread_bps, 10.0 - 1e-6);
}

// -----------------------------------------------------------------------
TEST(AvellanedaStoikov, InventoryClamping) {
    auto cfg = default_cfg();
    AvellanedaStoikov as(cfg);
    as.update_volatility(50000.0);
    as.update_volatility(50001.0);

    auto book = make_book(49999.0, 50001.0);

    // Inventory beyond max
    double inv = 0.02; // 2x max_inventory
    auto dec = as.compute_quotes(book, inv, 0.0);
    EXPECT_TRUE(dec.valid);

    // Bid qty should be at minimum (heavily scaled down)
    double bid_qty = from_qty(dec.bid_qty);
    EXPECT_LE(bid_qty, cfg.base_qty);
    EXPECT_GE(bid_qty, cfg.base_qty * 0.1 - 1e-9);
}

// -----------------------------------------------------------------------
TEST(AvellanedaStoikov, VolatilityUpdate) {
    auto cfg = default_cfg();
    AvellanedaStoikov as(cfg);

    // Initially sigma = cfg.sigma
    EXPECT_DOUBLE_EQ(as.sigma(), cfg.sigma);

    // After first two updates, sigma should update
    as.update_volatility(100.0);
    as.update_volatility(101.0); // ~1% return
    EXPECT_NE(as.sigma(), cfg.sigma);
    EXPECT_GT(as.sigma(), 0.0);
}
