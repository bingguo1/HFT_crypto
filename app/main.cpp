#include "hfmm/engine/engine.hpp"
#include "hfmm/core/types.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <csignal>
#include <atomic>

using json = nlohmann::json;

static std::atomic<bool> g_shutdown{false};

static void signal_handler(int) {
    g_shutdown.store(true, std::memory_order_release);
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

    // 2. Apply exchange-specific endpoint/symbol defaults (overridden below if explicit)
    if (cfg.exchange == "binance") {
        cfg.symbol        = "BTCUSDT";
        cfg.ws_endpoint   = "wss://fstream.binance.com";
        cfg.rest_endpoint = "https://fapi.binance.com";
    } else if (cfg.exchange == "binance_us") {
        cfg.symbol        = "BTCUSDT";
        cfg.ws_endpoint   = "wss://fstream.binance.us";
        cfg.rest_endpoint = "https://fapi.binance.us";
    }
    // "coinbase" defaults are already set in the Config struct

    // 3. Explicit JSON values override the defaults above
    if (j.contains("symbol"))               cfg.symbol               = j["symbol"];
    if (j.contains("ws_endpoint"))          cfg.ws_endpoint          = j["ws_endpoint"];
    if (j.contains("rest_endpoint"))        cfg.rest_endpoint        = j["rest_endpoint"];
    if (j.contains("api_key"))              cfg.api_key              = j["api_key"];
    if (j.contains("api_secret"))           cfg.api_secret           = j["api_secret"];
    if (j.contains("gamma"))               cfg.gamma                = j["gamma"];
    if (j.contains("sigma"))               cfg.sigma                = j["sigma"];
    if (j.contains("k"))                   cfg.k                    = j["k"];
    if (j.contains("T"))                   cfg.T                    = j["T"];
    if (j.contains("base_qty"))            cfg.base_qty             = j["base_qty"];
    if (j.contains("max_inventory"))       cfg.max_inventory        = j["max_inventory"];
    if (j.contains("min_spread_bps"))      cfg.min_spread_bps       = j["min_spread_bps"];
    if (j.contains("reprice_threshold_bps")) cfg.reprice_threshold_bps = j["reprice_threshold_bps"];
    if (j.contains("ema_alpha"))           cfg.ema_alpha            = j["ema_alpha"];
    if (j.contains("book_depth"))          cfg.book_depth           = j["book_depth"];
    if (j.contains("paper_trading"))       cfg.paper_trading        = j["paper_trading"];
    if (j.contains("status_interval_ms")) cfg.status_interval_ms  = j["status_interval_ms"];
    return cfg;
}

int main(int argc, char** argv) {
    std::string config_path = "config/config.json";
    if (argc > 1) config_path = argv[1];

    auto cfg = load_config(config_path);

    std::cout << "[main] HF Market Maker starting\n"
              << "  exchange:      " << cfg.exchange << "\n"
              << "  symbol:        " << cfg.symbol << "\n"
              << "  paper_trading: " << (cfg.paper_trading ? "yes" : "NO - LIVE") << "\n"
              << "  gamma:         " << cfg.gamma << "\n"
              << "  sigma:         " << cfg.sigma << "\n"
              << "  k:             " << cfg.k << "\n"
              << "  T:             " << cfg.T << "s\n"
              << "  base_qty:      " << cfg.base_qty << "\n"
              << "  max_inventory: " << cfg.max_inventory << "\n";

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    hfmm::Engine engine(cfg);

    // Monitor shutdown flag in a detached approach:
    // Engine::run() blocks; we poll g_shutdown in a thread and call stop().
    std::thread watcher([&engine] {
        while (!g_shutdown.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        std::cout << "\n[main] Shutdown signal received.\n";
        engine.stop();
    });
    watcher.detach();

    engine.run();
    return 0;
}
