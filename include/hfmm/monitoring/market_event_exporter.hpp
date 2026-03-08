#pragma once

#include <atomic>
#include <netinet/in.h>
#include <string>
#include <thread>

#include "hfmm/core/ring_buffer.hpp"
#include "hfmm/core/types.hpp"

namespace hfmm {

class MarketEventExporter {
public:
    explicit MarketEventExporter(const Config& cfg);
    ~MarketEventExporter();

    void start();
    void stop();

    // Non-blocking; drops on backpressure.
    void enqueue(const BookUpdateEvent& ev);

    uint64_t sent_count() const { return sent_count_.load(std::memory_order_relaxed); }
    uint64_t dropped_count() const { return dropped_count_.load(std::memory_order_relaxed); }
    std::size_t queue_size() const { return queue_.size(); }

private:
    bool init_socket();
    void close_socket();
    void run_loop();
    std::string serialize_event(const BookUpdateEvent& ev, uint64_t ts_ns) const;

    const Config& cfg_;
    SpscRingBuffer<BookUpdateEvent, 4096> queue_;

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> sent_count_{0};
    std::atomic<uint64_t> dropped_count_{0};

    int socket_fd_{-1};
    ::sockaddr_in addr_in_{};
    bool has_addr_{false};
};

} // namespace hfmm
