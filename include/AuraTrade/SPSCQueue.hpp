#pragma once

#include <atomic>
#include <array>
#include <cstddef>

namespace AuraTrade {

// Single-Producer Single-Consumer lock-free ring buffer.
//
// Capacity must be a power of two.
// Producer and consumer indices are on separate cache lines to eliminate
// false sharing between the two threads.
template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "SPSCQueue Capacity must be a power of two");

public:
    // Called exclusively by the producer thread.
    bool push(T item) noexcept {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) == Capacity)
            return false; // full
        buffer_[h & kMask] = item;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    // Called exclusively by the consumer thread.
    bool pop(T& item) noexcept {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire))
            return false; // empty
        item = buffer_[t & kMask];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    std::array<T, Capacity> buffer_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

} // namespace AuraTrade
