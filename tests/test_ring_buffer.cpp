#include <gtest/gtest.h>
#include "hfmm/core/ring_buffer.hpp"
#include <thread>
#include <vector>
#include <atomic>

using namespace hfmm;

// -----------------------------------------------------------------------
TEST(SpscRingBuffer, PushPop) {
    SpscRingBuffer<int, 8> buf;
    EXPECT_TRUE(buf.empty());

    EXPECT_TRUE(buf.push(42));
    EXPECT_FALSE(buf.empty());

    auto val = buf.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 42);
    EXPECT_TRUE(buf.empty());
}

// -----------------------------------------------------------------------
TEST(SpscRingBuffer, FullQueueReturnsFalse) {
    SpscRingBuffer<int, 4> buf; // capacity 4, holds 3 items (1 slot reserved)
    EXPECT_TRUE(buf.push(1));
    EXPECT_TRUE(buf.push(2));
    EXPECT_TRUE(buf.push(3));
    EXPECT_FALSE(buf.push(4)); // full

    // Pop one and push again
    auto v = buf.pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 1);
    EXPECT_TRUE(buf.push(4));
}

// -----------------------------------------------------------------------
TEST(SpscRingBuffer, EmptyReturnsNullopt) {
    SpscRingBuffer<int, 8> buf;
    EXPECT_FALSE(buf.pop().has_value());
}

// -----------------------------------------------------------------------
TEST(SpscRingBuffer, WrapAround) {
    SpscRingBuffer<int, 4> buf;
    for (int round = 0; round < 3; ++round) {
        for (int i = 1; i <= 3; ++i) {
            EXPECT_TRUE(buf.push(i * 10));
        }
        for (int i = 1; i <= 3; ++i) {
            auto v = buf.pop();
            ASSERT_TRUE(v.has_value());
            EXPECT_EQ(*v, i * 10);
        }
    }
}

// -----------------------------------------------------------------------
TEST(SpscRingBuffer, SpscThreaded) {
    constexpr int N = 100'000;
    SpscRingBuffer<int, 1024> buf;
    std::atomic<bool> done{false};
    std::vector<int> received;
    received.reserve(N);

    std::thread producer([&] {
        for (int i = 0; i < N; ++i) {
            while (!buf.push(i)) {
                std::this_thread::yield();
            }
        }
        done.store(true, std::memory_order_release);
    });

    while (!done.load(std::memory_order_acquire) || !buf.empty()) {
        auto v = buf.pop();
        if (v) received.push_back(*v);
    }
    producer.join();

    ASSERT_EQ(static_cast<int>(received.size()), N);
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(received[i], i);
    }
}

// -----------------------------------------------------------------------
TEST(SpscRingBuffer, CapacityIsPowerOfTwo) {
    SpscRingBuffer<int, 16> buf;
    EXPECT_EQ(buf.capacity(), 16u);
}
