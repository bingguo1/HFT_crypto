#include <gtest/gtest.h>
#include "hfmm/core/order_book.hpp"

using namespace hfmm;

// Helper: build a simple BookUpdateEvent
static BookUpdateEvent make_snapshot(uint64_t lastId,
                                     std::initializer_list<std::pair<double,double>> bids,
                                     std::initializer_list<std::pair<double,double>> asks) {
    BookUpdateEvent ev;
    ev.kind           = EventKind::Snapshot;
    ev.last_update_id = lastId;
    ev.bid_count      = 0;
    ev.ask_count      = 0;
    for (auto& [p, q] : bids)
        ev.bids[ev.bid_count++] = {to_price(p), to_qty(q)};
    for (auto& [p, q] : asks)
        ev.asks[ev.ask_count++] = {to_price(p), to_qty(q)};
    return ev;
}

static BookUpdateEvent make_delta(uint64_t U, uint64_t u,
                                   std::initializer_list<std::pair<double,double>> bids,
                                   std::initializer_list<std::pair<double,double>> asks) {
    BookUpdateEvent ev;
    ev.kind             = EventKind::Delta;
    ev.first_update_id  = U;
    ev.final_update_id  = u;
    ev.bid_count        = 0;
    ev.ask_count        = 0;
    for (auto& [p, q] : bids)
        ev.bids[ev.bid_count++] = {to_price(p), to_qty(q)};
    for (auto& [p, q] : asks)
        ev.asks[ev.ask_count++] = {to_price(p), to_qty(q)};
    return ev;
}

// -----------------------------------------------------------------------
TEST(OrderBook, SnapshotApply) {
    OrderBook book;
    auto snap = make_snapshot(100,
        {{50000.0, 1.0}, {49999.0, 2.0}},
        {{50001.0, 1.5}, {50002.0, 0.5}});
    book.apply_snapshot(snap);

    EXPECT_TRUE(book.initialized());
    EXPECT_EQ(book.last_update_id(), 100u);

    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(book.best_bid().value(), to_price(50000.0));

    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(book.best_ask().value(), to_price(50001.0));

    ASSERT_TRUE(book.mid_price().has_value());
    EXPECT_NEAR(*book.mid_price(), 50000.5, 1e-4);

    ASSERT_TRUE(book.spread_bps().has_value());
    EXPECT_NEAR(*book.spread_bps(), 1.0 / 50000.5 * 10000.0, 1e-3);
}

// -----------------------------------------------------------------------
TEST(OrderBook, DeltaApply) {
    OrderBook book;
    auto snap = make_snapshot(100, {{50000.0, 1.0}}, {{50001.0, 1.0}});
    book.apply_snapshot(snap);

    // Valid first delta: U <= 101 <= u
    auto delta = make_delta(100, 101, {{50000.0, 2.0}}, {});
    EXPECT_TRUE(book.apply_delta(delta));
    EXPECT_EQ(book.last_update_id(), 101u);

    // Check qty updated
    EXPECT_EQ(book.bids().begin()->second, to_qty(2.0));
}

// -----------------------------------------------------------------------
TEST(OrderBook, StaleReject) {
    OrderBook book;
    auto snap = make_snapshot(100, {{50000.0, 1.0}}, {{50001.0, 1.0}});
    book.apply_snapshot(snap);

    // Stale delta: final_update_id <= last_update_id (100)
    auto stale = make_delta(90, 95, {{50000.0, 3.0}}, {});
    EXPECT_TRUE(book.apply_delta(stale)); // skipped silently (already have)
    // Book unchanged
    EXPECT_EQ(book.bids().begin()->second, to_qty(1.0));
}

// -----------------------------------------------------------------------
TEST(OrderBook, GapReject) {
    OrderBook book;
    auto snap = make_snapshot(100, {{50000.0, 1.0}}, {{50001.0, 1.0}});
    book.apply_snapshot(snap);

    // Gap: U > last_update_id + 1 (U=103 > 101)
    auto gap = make_delta(103, 110, {{50000.0, 5.0}}, {});
    EXPECT_FALSE(book.apply_delta(gap));
}

// -----------------------------------------------------------------------
TEST(OrderBook, QtyZeroRemoval) {
    OrderBook book;
    auto snap = make_snapshot(100, {{50000.0, 1.0}, {49999.0, 2.0}}, {{50001.0, 1.0}});
    book.apply_snapshot(snap);

    // Remove 50000 level (qty=0)
    auto delta = make_delta(100, 101, {{50000.0, 0.0}}, {});
    EXPECT_TRUE(book.apply_delta(delta));

    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(book.best_bid().value(), to_price(49999.0));
    EXPECT_EQ(book.bids().size(), 1u);
}

// -----------------------------------------------------------------------
TEST(OrderBook, MidAndSpread) {
    OrderBook book;
    auto snap = make_snapshot(1, {{100.0, 1.0}}, {{102.0, 1.0}});
    book.apply_snapshot(snap);

    ASSERT_TRUE(book.mid_price().has_value());
    EXPECT_NEAR(*book.mid_price(), 101.0, 1e-6);

    ASSERT_TRUE(book.spread_bps().has_value());
    EXPECT_NEAR(*book.spread_bps(), 2.0 / 101.0 * 10000.0, 1e-3);
}

// -----------------------------------------------------------------------
TEST(OrderBook, EmptyBook) {
    OrderBook book;
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_FALSE(book.mid_price().has_value());
    EXPECT_FALSE(book.spread_bps().has_value());
}
