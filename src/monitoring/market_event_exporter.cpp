#include "hfmm/monitoring/market_event_exporter.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace hfmm {

MarketEventExporter::MarketEventExporter(const Config& cfg)
    : cfg_(cfg) {}

MarketEventExporter::~MarketEventExporter() {
    stop();
}

void MarketEventExporter::start() {
    if (!cfg_.telemetry.enabled || running_.load(std::memory_order_relaxed)) return;

    if (!init_socket()) {
        std::cerr << "[telemetry] Failed to initialize exporter socket. Export disabled.\n";
        return;
    }

    running_.store(true, std::memory_order_release);
    worker_ = std::thread([this] { run_loop(); });
    std::cout << "[telemetry] exporter enabled: " << cfg_.telemetry.transport
              << "://" << cfg_.telemetry.host << ':' << cfg_.telemetry.port << "\n";
}

void MarketEventExporter::stop() {
    running_.store(false, std::memory_order_release);
    if (worker_.joinable()) worker_.join();
    close_socket();
}

void MarketEventExporter::enqueue(const BookUpdateEvent& ev) {
    if (!cfg_.telemetry.enabled) return;
    if (!queue_.push(BookUpdateEvent(ev))) {
        dropped_count_.fetch_add(1, std::memory_order_relaxed);
    }
}

bool MarketEventExporter::init_socket() {
    if (cfg_.telemetry.transport != "udp") {
        std::cerr << "[telemetry] Unsupported transport: " << cfg_.telemetry.transport
                  << " (only udp is available currently)\n";
        return false;
    }

    socket_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) return false;

    addr_in_ = {};
    addr_in_.sin_family = AF_INET;
    addr_in_.sin_port = htons(static_cast<uint16_t>(cfg_.telemetry.port));
    if (::inet_pton(AF_INET, cfg_.telemetry.host.c_str(), &addr_in_.sin_addr) != 1) {
        close_socket();
        return false;
    }

    has_addr_ = true;
    return true;
}

void MarketEventExporter::close_socket() {
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
    has_addr_ = false;
}

void MarketEventExporter::run_loop() {
    const int sleep_ms = std::max(cfg_.telemetry.flush_interval_ms, 1);

    while (running_.load(std::memory_order_acquire) || !queue_.empty()) {
        auto ev = queue_.pop();
        if (!ev) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            continue;
        }

        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto ts_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());

        const auto payload = serialize_event(*ev, ts_ns);
        if (!has_addr_) {
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        const auto sent = ::sendto(socket_fd_, payload.data(), payload.size(), 0,
                       reinterpret_cast<const sockaddr*>(&addr_in_),
                                   sizeof(sockaddr_in));
        if (sent < 0) {
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        sent_count_.fetch_add(1, std::memory_order_relaxed);
    }
}

std::string MarketEventExporter::serialize_event(const BookUpdateEvent& ev, uint64_t ts_ns) const {
    std::ostringstream out;
    out << "{\"ts_ns\":" << ts_ns
        << ",\"symbol\":\"" << ev.symbol
        << "\",\"kind\":\"" << (ev.kind == EventKind::Snapshot ? "snapshot" : "delta")
        << "\",\"first_update_id\":" << ev.first_update_id
        << ",\"final_update_id\":" << ev.final_update_id
        << ",\"last_update_id\":" << ev.last_update_id
        << ",\"bid_count\":" << ev.bid_count
        << ",\"ask_count\":" << ev.ask_count
        << "}";
    return out.str();
}

} // namespace hfmm
