#include "hfmm/feed/binance_feed.hpp"
#include <simdjson.h>
#include <charconv>
#include <algorithm>
#include <iostream>

namespace hfmm {

BinanceFeed::BinanceFeed(const Config& cfg, BookEventQueue& queue)
    : cfg_(cfg), queue_(queue) {}

BinanceFeed::~BinanceFeed() {
    stop();
}

void BinanceFeed::start() {
    std::string symbol_lc = cfg_.symbol;
    std::transform(symbol_lc.begin(), symbol_lc.end(), symbol_lc.begin(), ::tolower);
    // Partial book depth stream — delivers a full top-N snapshot every 250 ms
    std::string url = cfg_.ws_endpoint + "/ws/" + symbol_lc
                      + "@depth" + std::to_string(cfg_.book_depth);

    ws_.setUrl(url);
    ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        on_message(msg);
    });
    ws_.disableAutomaticReconnection();
    ws_.start();
}

void BinanceFeed::stop() {
    ws_.stop();
    connected_.store(false, std::memory_order_relaxed);
}

void BinanceFeed::on_message(const ix::WebSocketMessagePtr& msg) {
    switch (msg->type) {
    case ix::WebSocketMessageType::Open:
        connected_.store(true, std::memory_order_release);
        break;

    case ix::WebSocketMessageType::Close:
        connected_.store(false, std::memory_order_release);
        break;

    case ix::WebSocketMessageType::Error:
        std::cerr << "[binance_feed] WS error: " << msg->errorInfo.reason << "\n";
        break;

    case ix::WebSocketMessageType::Message: {
        BookUpdateEvent ev;
        if (!parse_snapshot(msg->str.data(), msg->str.size(), ev)) break;
        while (!queue_.push(ev)) {} // spin-wait — should rarely happen
        break;
    }
    default:
        break;
    }
}

// Binance partial book depth stream: {"u":lastUpdateId, "b":[[price,qty],...], "a":...}
bool BinanceFeed::parse_snapshot(const char* data, std::size_t len, BookUpdateEvent& out) {
    static thread_local simdjson::ondemand::parser parser;
    static thread_local simdjson::padded_string padded;
    padded = simdjson::padded_string(data, len);

    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) return false;

    uint64_t u = 0;
    if (doc["u"].get(u) != simdjson::SUCCESS) return false;

    out.kind           = EventKind::Snapshot;
    out.last_update_id = u;
    out.bid_count      = 0;
    out.ask_count      = 0;

    auto parse_levels = [](simdjson::ondemand::array arr,
                           PriceLevel* levels, int& count) {
        count = 0;
        for (auto entry : arr) {
            if (count >= MAX_LEVELS) break;
            simdjson::ondemand::array pair;
            if (entry.get_array().get(pair) != simdjson::SUCCESS) continue;

            auto it = pair.begin();
            if (it == pair.end()) continue;
            std::string_view price_sv;
            if ((*it).get_string().get(price_sv) != simdjson::SUCCESS) continue;
            ++it;
            if (it == pair.end()) continue;
            std::string_view qty_sv;
            if ((*it).get_string().get(qty_sv) != simdjson::SUCCESS) continue;

            double price_d = 0.0, qty_d = 0.0;
            std::from_chars(price_sv.data(), price_sv.data() + price_sv.size(), price_d);
            std::from_chars(qty_sv.data(),   qty_sv.data()   + qty_sv.size(),   qty_d);

            levels[count].price = to_price(price_d);
            levels[count].qty   = to_qty(qty_d);
            ++count;
        }
    };

    simdjson::ondemand::array bids_arr, asks_arr;
    if (doc["b"].get_array().get(bids_arr) == simdjson::SUCCESS)
        parse_levels(bids_arr, out.bids.data(), out.bid_count);
    if (doc["a"].get_array().get(asks_arr) == simdjson::SUCCESS)
        parse_levels(asks_arr, out.asks.data(), out.ask_count);

    return true;
}

} // namespace hfmm
