#include "hfmm/feed/binance_feed.hpp"
#include "hfmm/monitoring/market_event_exporter.hpp"
#include <simdjson.h>
#include <charconv>
#include <algorithm>
#include <iostream>

namespace hfmm {

BinanceFeed::BinanceFeed(const Config& cfg,
                         BookEventQueue& queue,
                         MarketEventExporter* telemetry)
    : cfg_(cfg), queue_(queue), telemetry_(telemetry) {}

BinanceFeed::~BinanceFeed() {
    stop();
}

void BinanceFeed::start() {
    // Build combined stream URL: /stream?streams=btcusdt@depth20/ethusdt@depth5
    std::string streams;
    size_t i = 0;
    for (const auto& [sym, pc] : cfg_.pairs) {
        if (i++ > 0) streams += '/';
        std::string sym_lc = pc.symbol;
        std::transform(sym_lc.begin(), sym_lc.end(), sym_lc.begin(), ::tolower);
        streams += sym_lc + "@depth" + std::to_string(pc.book_depth);
    }
    std::string url = cfg_.ws_endpoint + "/stream?streams=" + streams;

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
        if (telemetry_ != nullptr) telemetry_->enqueue(ev);
        break;
    }
    default:
        break;
    }
}

// Binance combined stream message:
// {"stream":"btcusdt@depth20","data":{"u":lastUpdateId,"b":[[price,qty],...],"a":...}}
bool BinanceFeed::parse_snapshot(const char* data, std::size_t len, BookUpdateEvent& out) {
    static thread_local simdjson::ondemand::parser parser;
    static thread_local simdjson::padded_string padded;
    padded = simdjson::padded_string(data, len);

    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) return false;

    // Extract stream name to derive symbol (e.g., "btcusdt@depth20" → "BTCUSDT")
    std::string_view stream_name;
    if (doc["stream"].get_string().get(stream_name) != simdjson::SUCCESS) return false;

    auto at_pos = stream_name.find('@');
    std::string_view sym_lc = (at_pos != std::string_view::npos)
                               ? stream_name.substr(0, at_pos)
                               : stream_name;
    size_t copy_len = std::min(sym_lc.size(), static_cast<size_t>(15));
    for (size_t i = 0; i < copy_len; ++i)
        out.symbol[i] = static_cast<char>(::toupper(static_cast<unsigned char>(sym_lc[i])));
    out.symbol[copy_len] = '\0';

    // Navigate into data object
    simdjson::ondemand::object data_obj;
    if (doc["data"].get_object().get(data_obj) != simdjson::SUCCESS) return false;

    uint64_t u = 0;
    if (data_obj["u"].get(u) != simdjson::SUCCESS) return false;

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
    if (data_obj["b"].get_array().get(bids_arr) == simdjson::SUCCESS)
        parse_levels(bids_arr, out.bids.data(), out.bid_count);
    if (data_obj["a"].get_array().get(asks_arr) == simdjson::SUCCESS)
        parse_levels(asks_arr, out.asks.data(), out.ask_count);

    return true;
}

} // namespace hfmm
