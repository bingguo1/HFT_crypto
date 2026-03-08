#pragma once
#include <atomic>
#include "hfmm/feed/market_data_feed.hpp"
#include <ixwebsocket/IXWebSocket.h>

namespace hfmm {

class MarketEventExporter;

// Supports both Binance international (wss://fstream.binance.com) and
// Binance US (wss://fstream.binance.us) — endpoint is taken from cfg.ws_endpoint.
// Connects to the partial book depth stream (<symbol>@depth<N>) which delivers
// a full top-N snapshot every 250 ms — no REST snapshot/delta sequencing needed.
class BinanceFeed : public IMarketDataFeed {
public:
    BinanceFeed(const Config& cfg, BookEventQueue& queue, MarketEventExporter* telemetry = nullptr);
    ~BinanceFeed() override;

    void start() override;
    void stop() override;
    bool connected() const override { return connected_.load(std::memory_order_relaxed); }

private:
    void on_message(const ix::WebSocketMessagePtr& msg);
    bool parse_snapshot(const char* data, std::size_t len, BookUpdateEvent& out);

    const Config&     cfg_;
    BookEventQueue&   queue_;
    MarketEventExporter* telemetry_{nullptr};
    ix::WebSocket     ws_;
    std::atomic<bool> connected_{false};
};

} // namespace hfmm
