#include "hfmm/feed/coinbase_feed.hpp"
#include <simdjson.h>
#include <charconv>
#include <algorithm>
#include <iostream>
#include <vector>

namespace hfmm {

CoinbaseFeed::CoinbaseFeed(const Config& cfg, BookEventQueue& queue)
    : cfg_(cfg), queue_(queue) {}

CoinbaseFeed::~CoinbaseFeed() {
    stop();
}

void CoinbaseFeed::start() {
    ws_.setUrl(cfg_.ws_endpoint);
    ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        on_message(msg);
    });
    ws_.disableAutomaticReconnection();
    ws_.start();
}

void CoinbaseFeed::stop() {
    ws_.stop();
    connected_.store(false, std::memory_order_relaxed);
}

void CoinbaseFeed::on_message(const ix::WebSocketMessagePtr& msg) {
    switch (msg->type) {
    case ix::WebSocketMessageType::Open: {
        connected_.store(true, std::memory_order_release);
        // Subscribe to level2 channel for the configured product
        std::string sub = R"({"type":"subscribe","product_ids":[")"
                        + cfg_.symbol
                        + R"("],"channel":"level2"})";
        ws_.send(sub);
        break;
    }
    case ix::WebSocketMessageType::Close:
        connected_.store(false, std::memory_order_release);
        break;

    case ix::WebSocketMessageType::Error:
        std::cerr << "[coinbase_feed] WS error: " << msg->errorInfo.reason << "\n";
        break;

    case ix::WebSocketMessageType::Message:
        parse_message(msg->str.data(), msg->str.size());
        break;

    default:
        break;
    }
}

// Coinbase Advanced Trade l2_data message format:
// {
//   "channel": "l2_data",
//   "sequence_num": <uint64>,
//   "events": [{
//     "type": "snapshot" | "update",
//     "product_id": "BTC-USD",
//     "updates": [{"side":"bid"|"offer", "price_level":"...", "new_quantity":"..."}, ...]
//   }]
// }
// new_quantity "0" means remove the price level.
// sequence_num increments by 1 per message — used for gap detection via the
// existing order book spot-fallback path (first_update_id == last_update_id+1).
bool CoinbaseFeed::parse_message(const char* data, std::size_t len) {
    static thread_local simdjson::ondemand::parser parser;
    static thread_local simdjson::padded_string padded;
    padded = simdjson::padded_string(data, len);

    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) return false;

    // Field order in Coinbase message: channel, client_id, timestamp, sequence_num, events
    std::string_view channel;
    if (doc["channel"].get_string().get(channel) != simdjson::SUCCESS) return false;
    if (channel != "l2_data") return true; // subscriptions ack etc. — silently ignore

    uint64_t seq_num = 0;
    doc["sequence_num"].get(seq_num); // best-effort; 0 is safe

    simdjson::ondemand::array events;
    if (doc["events"].get_array().get(events) != simdjson::SUCCESS) return false;

    for (auto event_val : events) {
        // Field order in event: type, product_id, updates
        std::string_view type;
        if (event_val["type"].get_string().get(type) != simdjson::SUCCESS) continue;

        simdjson::ondemand::array updates;
        if (event_val["updates"].get_array().get(updates) != simdjson::SUCCESS) continue;

        if (type == "snapshot") {
            // Collect all levels, sort, truncate to top book_depth per side
            std::vector<PriceLevel> bids, asks;
            bids.reserve(512);
            asks.reserve(512);

            for (auto upd_val : updates) {
                // Field order: side, event_time, price_level, new_quantity
                std::string_view side, price_sv, qty_sv;
                if (upd_val["side"].get_string().get(side) != simdjson::SUCCESS) continue;
                if (upd_val["price_level"].get_string().get(price_sv) != simdjson::SUCCESS) continue;
                if (upd_val["new_quantity"].get_string().get(qty_sv) != simdjson::SUCCESS) continue;

                double price_d = 0.0, qty_d = 0.0;
                std::from_chars(price_sv.data(), price_sv.data() + price_sv.size(), price_d);
                std::from_chars(qty_sv.data(),   qty_sv.data()   + qty_sv.size(),   qty_d);

                PriceLevel lvl{to_price(price_d), to_qty(qty_d)};
                if (side == "bid") bids.push_back(lvl);
                else               asks.push_back(lvl); // "offer"
            }

            // Sort: bids descending (best first), asks ascending (best first)
            std::sort(bids.begin(), bids.end(),
                      [](const PriceLevel& a, const PriceLevel& b) { return a.price > b.price; });
            std::sort(asks.begin(), asks.end(),
                      [](const PriceLevel& a, const PriceLevel& b) { return a.price < b.price; });

            int max_n = std::min(cfg_.book_depth, MAX_LEVELS);

            BookUpdateEvent ev;
            ev.kind           = EventKind::Snapshot;
            ev.last_update_id = seq_num;
            ev.bid_count      = static_cast<int>(std::min(static_cast<int>(bids.size()), max_n));
            ev.ask_count      = static_cast<int>(std::min(static_cast<int>(asks.size()), max_n));
            for (int i = 0; i < ev.bid_count; ++i) ev.bids[i] = bids[i];
            for (int i = 0; i < ev.ask_count; ++i) ev.asks[i] = asks[i];

            while (!queue_.push(ev)) {}

        } else { // "update"
            // Delta: a few price level changes per message
            // Map to EventKind::Delta using sequence_num for gap detection
            // (order book spot-fallback: first_update_id == last_update_id + 1)
            BookUpdateEvent ev;
            ev.kind            = EventKind::Delta;
            ev.first_update_id = seq_num;
            ev.final_update_id = seq_num;
            ev.prev_update_id  = 0; // triggers spot gap-check in OrderBook
            ev.bid_count       = 0;
            ev.ask_count       = 0;

            for (auto upd_val : updates) {
                // Field order: side, price_level, new_quantity (no event_time in updates)
                std::string_view side, price_sv, qty_sv;
                if (upd_val["side"].get_string().get(side) != simdjson::SUCCESS) continue;
                if (upd_val["price_level"].get_string().get(price_sv) != simdjson::SUCCESS) continue;
                if (upd_val["new_quantity"].get_string().get(qty_sv) != simdjson::SUCCESS) continue;

                double price_d = 0.0, qty_d = 0.0;
                std::from_chars(price_sv.data(), price_sv.data() + price_sv.size(), price_d);
                std::from_chars(qty_sv.data(),   qty_sv.data()   + qty_sv.size(),   qty_d);

                PriceLevel lvl{to_price(price_d), to_qty(qty_d)};
                if (side == "bid") {
                    if (ev.bid_count < MAX_LEVELS) ev.bids[ev.bid_count++] = lvl;
                } else {
                    if (ev.ask_count < MAX_LEVELS) ev.asks[ev.ask_count++] = lvl;
                }
            }

            if (ev.bid_count > 0 || ev.ask_count > 0) {
                while (!queue_.push(ev)) {}
            }
        }
    }

    return true;
}

} // namespace hfmm
