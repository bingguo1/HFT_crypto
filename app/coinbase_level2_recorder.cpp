#include "hfmm/core/types.hpp"
#include "hfmm/feed/coinbase_feed.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

std::atomic<bool> g_shutdown{false};

void signal_handler(int) {
    g_shutdown.store(true, std::memory_order_release);
}

hfmm::Config load_config(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "Cannot open config: " << path << "\n";
        std::exit(1);
    }

    auto j = json::parse(f);
    hfmm::Config cfg;

    if (j.contains("symbol")) cfg.symbol = j["symbol"];
    if (j.contains("ws_endpoint")) cfg.ws_endpoint = j["ws_endpoint"];
    if (j.contains("rest_endpoint")) cfg.rest_endpoint = j["rest_endpoint"];
    if (j.contains("book_depth")) cfg.book_depth = j["book_depth"];

    return cfg;
}

json levels_to_json(const std::array<hfmm::PriceLevel, hfmm::MAX_LEVELS>& levels, int count) {
    json out = json::array();
    for (int i = 0; i < count; ++i) {
        out.push_back({
            {"price", hfmm::from_price(levels[i].price)},
            {"qty", hfmm::from_qty(levels[i].qty)}
        });
    }
    return out;
}

json event_to_json(const hfmm::BookUpdateEvent& ev) {
    return {
        {"kind", ev.kind == hfmm::EventKind::Snapshot ? "snapshot" : "delta"},
        {"first_update_id", ev.first_update_id},
        {"final_update_id", ev.final_update_id},
        {"prev_update_id", ev.prev_update_id},
        {"last_update_id", ev.last_update_id},
        {"bids", levels_to_json(ev.bids, ev.bid_count)},
        {"asks", levels_to_json(ev.asks, ev.ask_count)}
    };
}

} // namespace

int main(int argc, char** argv) {
    std::string config_path = "config/config.json";
    std::string output_path = "coinbase_level2.jsonl";
    std::size_t max_events = 0;

    if (argc > 1) config_path = argv[1];
    if (argc > 2) output_path = argv[2];
    if (argc > 3) {
        try {
            max_events = static_cast<std::size_t>(std::stoull(argv[3]));
        } catch (const std::exception&) {
            std::cerr << "Invalid max_events argument: " << argv[3] << "\n";
            return 1;
        }
    }

    auto cfg = load_config(config_path);
    cfg.exchange = "coinbase";
    if (cfg.symbol.find('-') == std::string::npos) {
        cfg.symbol = "BTC-USD";
    }
    if (cfg.ws_endpoint.find("coinbase") == std::string::npos) {
        cfg.ws_endpoint = "wss://advanced-trade-ws.coinbase.com";
    }

    std::ofstream out(output_path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "Cannot open output file: " << output_path << "\n";
        return 1;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    hfmm::BookEventQueue queue;
    hfmm::CoinbaseFeed feed(cfg, queue);

    std::cout << "[recorder] Connecting Coinbase level2\n"
              << "  symbol:      " << cfg.symbol << "\n"
              << "  ws_endpoint: " << cfg.ws_endpoint << "\n"
              << "  output:      " << output_path << "\n";
    if (max_events > 0) {
        std::cout << "  max_events:  " << max_events << "\n";
    }
    std::cout
              << "Press Ctrl+C to stop.\n";

    feed.start();

    std::size_t written = 0;
    while (!g_shutdown.load(std::memory_order_acquire)) {
        auto evt = queue.pop();
        if (!evt.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        const auto now = std::chrono::system_clock::now();
        const auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();

        json row = {
            {"ts_ms", ts_ms},
            {"exchange", "coinbase"},
            {"symbol", cfg.symbol},
            {"event", event_to_json(*evt)}
        };

        out << row.dump() << '\n';
        ++written;

        if (written % 1000 == 0) {
            out.flush();
            std::cout << "[recorder] events written: " << written << "\n";
        }

        if (max_events > 0 && written >= max_events) {
            g_shutdown.store(true, std::memory_order_release);
        }
    }

    feed.stop();
    out.flush();

    std::cout << "[recorder] stopped, total events: " << written << "\n";
    return 0;
}
