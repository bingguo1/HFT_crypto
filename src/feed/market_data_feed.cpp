#include "hfmm/feed/market_data_feed.hpp"
#include "hfmm/feed/binance_feed.hpp"
#include "hfmm/feed/coinbase_feed.hpp"

namespace hfmm {

std::unique_ptr<IMarketDataFeed> make_feed(const Config& cfg,
                                           BookEventQueue& queue,
                                           MarketEventExporter* telemetry) {
    if (cfg.exchange == "binance" || cfg.exchange == "binance_us") {
        return std::make_unique<BinanceFeed>(cfg, queue, telemetry);
    }
    return std::make_unique<CoinbaseFeed>(cfg, queue, telemetry);
}

} // namespace hfmm
