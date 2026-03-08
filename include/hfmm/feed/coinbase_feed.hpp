#pragma once
#include <atomic>
#include "hfmm/feed/market_data_feed.hpp"
#include <ixwebsocket/IXWebSocket.h>

namespace hfmm {

// Coinbase Advanced Trade WebSocket feed.
// Endpoint: wss://advanced-trade-ws.coinbase.com
// Channel: level2 — first message is a full book snapshot, subsequent messages
// are incremental updates. Uses cfg.sequence_num for gap detection.
// Symbol format: "BTC-USD" (not "BTCUSDT").
class CoinbaseFeed : public IMarketDataFeed {
public:
    CoinbaseFeed(const Config& cfg, BookEventQueue& queue);
    ~CoinbaseFeed() override;

    void start() override;
    void stop() override;
    bool connected() const override { return connected_.load(std::memory_order_relaxed); }

private:
    void on_message(const ix::WebSocketMessagePtr& msg);
    bool parse_message(const char* data, std::size_t len);

    const Config&     cfg_;
    BookEventQueue&   queue_;
    ix::WebSocket     ws_;
    std::atomic<bool> connected_{false};
    uint64_t          last_sequence_num_{0};
    bool              has_sequence_num_{false};
};

} // namespace hfmm
