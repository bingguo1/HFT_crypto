#pragma once
#include <atomic>
#include <iostream>
#include <optional>
#include <new>

namespace hfmm {

// Single-Producer Single-Consumer lock-free ring buffer.
// Capacity must be a power of 2.
// Head and tail are on separate cache lines to prevent false sharing.
template <typename T, std::size_t Capacity>
class SpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr std::size_t MASK = Capacity - 1;

    alignas(64) std::atomic<std::size_t> head_{0}; // written by producer
    alignas(64) std::atomic<std::size_t> tail_{0}; // written by consumer

    alignas(64) T slots_[Capacity]{};

public:
    SpscRingBuffer() = default;
    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    // Producer side: returns false if full (non-blocking)
    bool push(const T& item) noexcept {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t next_h = (h + 1) & MASK;
        if (next_h == tail_.load(std::memory_order_acquire)) {
            std::cerr << "Ring buffer full, dropping item\n";
            return false; // full
        }
        slots_[h] = item;
        head_.store(next_h, std::memory_order_release);
        return true;
    }

    bool push(T&& item) noexcept {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t next_h = (h + 1) & MASK;
        if (next_h == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }
        slots_[h] = std::move(item);
        head_.store(next_h, std::memory_order_release);
        return true;
    }

    // Consumer side: returns nullopt if empty (non-blocking)
    std::optional<T> pop() noexcept {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) {
            return std::nullopt; // empty
        }
        T item = std::move(slots_[t]);
        tail_.store((t + 1) & MASK, std::memory_order_release);
        return item;
    }

    bool empty() const noexcept {
        return tail_.load(std::memory_order_acquire) ==
               head_.load(std::memory_order_acquire);
    }

    std::size_t size() const noexcept {
        const std::size_t h = head_.load(std::memory_order_acquire);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        return (h - t + Capacity) & MASK;
    }

    static constexpr std::size_t capacity() noexcept { return Capacity; }
};

} // namespace hfmm
