#include "hfmm/engine/engine.hpp"
#include "hfmm/core/types.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <csignal>
#include <atomic>
#include <memory>

using json = nlohmann::json;

static std::atomic<bool> g_shutdown{false};

static void signal_handler(int) {
    g_shutdown.store(true, std::memory_order_release);
}

static hfmm::PairConfig parse_pair_config(const json& j, const hfmm::PairConfig& defaults) {
    hfmm::PairConfig pc = defaults;
    if (j.contains("symbol"))               pc.symbol               = j["symbol"];
    if (j.contains("gamma"))               pc.gamma                = j["gamma"];
    if (j.contains("sigma"))               pc.sigma                = j["sigma"];
    if (j.contains("k"))                   pc.k                    = j["k"];
    if (j.contains("T"))                   pc.T                    = j["T"];
    if (j.contains("base_qty"))            pc.base_qty             = j["base_qty"];
    if (j.contains("max_inventory"))       pc.max_inventory        = j["max_inventory"];
    if (j.contains("min_spread_bps"))      pc.min_spread_bps       = j["min_spread_bps"];
    if (j.contains("reprice_threshold_bps")) pc.reprice_threshold_bps = j["reprice_threshold_bps"];
    if (j.contains("ema_alpha"))           pc.ema_alpha            = j["ema_alpha"];
    if (j.contains("book_depth"))          pc.book_depth           = j["book_depth"];
    return pc;
}

static hfmm::Config load_config(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "Cannot open config: " << path << "\n";
        std::exit(1);
    }
    auto j = json::parse(f);

    hfmm::Config cfg;

    // 1. Read exchange selector first
    if (j.contains("exchange")) cfg.exchange = j["exchange"];

    // 2. Apply exchange-specific endpoint defaults (overridden below if explicit)
    if (cfg.exchange == "binance") {
        cfg.ws_endpoint   = "wss://fstream.binance.com";
        cfg.rest_endpoint = "https://fapi.binance.com";
    } else if (cfg.exchange == "binance_us") {
        cfg.ws_endpoint   = "wss://fstream.binance.us";
        cfg.rest_endpoint = "https://fapi.binance.us";
    }
    // "coinbase" defaults are already set in the Config struct

    // 3. Explicit JSON values override the defaults above
    if (j.contains("ws_endpoint"))          cfg.ws_endpoint          = j["ws_endpoint"];
    if (j.contains("rest_endpoint"))        cfg.rest_endpoint        = j["rest_endpoint"];
    if (j.contains("api_key"))              cfg.api_key              = j["api_key"];
    if (j.contains("api_secret"))           cfg.api_secret           = j["api_secret"];
    if (j.contains("paper_trading"))        cfg.paper_trading        = j["paper_trading"];
    if (j.contains("status_interval_ms"))   cfg.status_interval_ms   = j["status_interval_ms"];
    if (j.contains("telemetry") && j["telemetry"].is_object()) {
        const auto& t = j["telemetry"];
        if (t.contains("enabled"))           cfg.telemetry.enabled = t["enabled"];
        if (t.contains("transport"))         cfg.telemetry.transport = t["transport"];
        if (t.contains("host"))              cfg.telemetry.host = t["host"];
        if (t.contains("port"))              cfg.telemetry.port = t["port"];
        if (t.contains("flush_interval_ms")) cfg.telemetry.flush_interval_ms = t["flush_interval_ms"];
    }

    // 4. Parse pairs array or fall back to backwards-compat single-pair fields
    if (j.contains("pairs") && j["pairs"].is_array()) {
        hfmm::PairConfig defaults;
        for (const auto& pair_j : j["pairs"]) {
            auto pc = parse_pair_config(pair_j, defaults);
            cfg.pairs[pc.symbol] = pc;
        }
    } else {
        // Backwards-compat: single pair from top-level fields
        hfmm::PairConfig pc;
        if (cfg.exchange == "binance" || cfg.exchange == "binance_us")
            pc.symbol = "BTCUSDT";
        else
            pc.symbol = "BTC-USD";
        auto parsed = parse_pair_config(j, pc);
        cfg.pairs[parsed.symbol] = parsed;
    }

    if (cfg.pairs.empty()) {
        std::cerr << "[main] Config must have at least one pair.\n";
        std::exit(1);
    }

    return cfg;
}

int main(int argc, char** argv) {
    std::string config_path = "config/config.json";
    if (argc > 1) config_path = argv[1];

    auto cfg = load_config(config_path);

    std::cout << "[main] HF Market Maker starting\n"
              << "  exchange:      " << cfg.exchange << "\n"
              << "  paper_trading: " << (cfg.paper_trading ? "yes" : "NO - LIVE") << "\n"
              << "  telemetry:     " << (cfg.telemetry.enabled ? "enabled" : "disabled") << "\n"
              << "  pairs:         " << cfg.pairs.size() << "\n";
    for (const auto& [sym, pc] : cfg.pairs) {
        std::cout << "    " << pc.symbol
                  << " gamma=" << pc.gamma
                  << " sigma=" << pc.sigma
                  << " base_qty=" << pc.base_qty
                  << " max_inv=" << pc.max_inventory
                  << "\n";
    }

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    auto engine = std::make_unique<hfmm::Engine>(cfg);

    // Monitor shutdown flag in a detached approach:
    // Engine::run() blocks; we poll g_shutdown in a thread and call stop().
    std::thread watcher([&engine] {
        while (!g_shutdown.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        std::cout << "\n[main] Shutdown signal received.\n";
        engine->stop();
    });
    watcher.detach();

    engine->run();
    return 0;
}
