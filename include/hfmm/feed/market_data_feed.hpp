#pragma once
#include <memory>
#include "hfmm/core/types.hpp"
#include "hfmm/core/ring_buffer.hpp"

namespace hfmm {

using BookEventQueue = SpscRingBuffer<BookUpdateEvent, 1024>;

class IMarketDataFeed {
public:
    virtual ~IMarketDataFeed() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool connected() const = 0;
};

// Factory: creates CoinbaseFeed, BinanceFeed, or BinanceUSFeed based on cfg.exchange
std::unique_ptr<IMarketDataFeed> make_feed(const Config& cfg, BookEventQueue& queue);

} // namespace hfmm
