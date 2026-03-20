#include "AuraTrade/AuraTrade.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

using namespace AuraTrade;

// ─── Benchmark configuration ────────────────────────────────────────────────

static constexpr std::size_t kNumOrders     = 100'000;
static constexpr std::size_t kOrderPoolSize = 500'000;
static constexpr std::size_t kLevelPoolSize = 10'000;
static constexpr std::size_t kQueueCapacity = 2'048; // must be power-of-two

// ─── Entry point ────────────────────────────────────────────────────────────

int main() {
    std::cout << "Aura-Trade  —  low-latency matching engine benchmark\n"
              << "Orders: " << kNumOrders << "\n\n";

    // Infrastructure (depends on no other component)
    BinaryLogger          logger("audit.log");
    L2MarketData          marketData;

    // Memory pools (zero heap activity on the hot path)
    SlabAllocator<Order,      kOrderPoolSize> orderPool;
    SlabAllocator<LimitLevel, kLevelPoolSize> levelPool;

    // SPSC channel between the network-simulator thread and the engine thread
    SPSCQueue<Order*, kQueueCapacity> queue;

    // Matching engine (receives handlers by reference — Dependency Inversion)
    OrderBook<kOrderPoolSize, kLevelPoolSize> book(orderPool, levelPool,
                                                   marketData, logger);

    std::vector<long long>  latencies;
    latencies.reserve(kNumOrders);
    std::atomic<bool> done{false};

    // ── Engine thread ────────────────────────────────────────────────────────
    auto engineThread = std::thread([&] {
        AffinityManager::pinCurrentThread(1);

        std::size_t processed = 0;
        while (!done.load(std::memory_order_acquire) || processed < kNumOrders) {
            Order* order = nullptr;
            if (!queue.pop(order)) { std::this_thread::yield(); continue; }

            const auto t0 = order->timestamp;
            book.processOrder(order);
            const auto t1 = std::chrono::steady_clock::now();

            latencies.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            ++processed;
        }
    });

    // ── Network-simulator thread ─────────────────────────────────────────────
    auto networkThread = std::thread([&] {
        AffinityManager::pinCurrentThread(2);

        for (uint64_t i = 1; i <= kNumOrders; ++i) {
            const Side        side = (i % 2 == 0) ? Side::Buy : Side::Sell;
            const uint64_t   price = 100 + (i % 5);
            const OrderType   type = (i % 100 == 0) ? OrderType::Market : OrderType::Limit;
            const TimeInForce tif  = (i %  50 == 0) ? TimeInForce::IOC  : TimeInForce::GTC;

            Order* order = orderPool.allocate(i, side, price, 10u, type, tif);
            while (!queue.push(order)) std::this_thread::yield();
        }
        done.store(true, std::memory_order_release);
    });

    networkThread.join();
    engineThread.join();

    // ── Results ───────────────────────────────────────────────────────────────
    std::sort(latencies.begin(), latencies.end());
    const double avg = std::accumulate(latencies.begin(), latencies.end(), 0.0)
                       / static_cast<double>(latencies.size());
    const auto p50  = latencies[latencies.size() * 50 / 100];
    const auto p95  = latencies[latencies.size() * 95 / 100];
    const auto p99  = latencies[latencies.size() * 99 / 100];

    std::cout << std::fixed << std::setprecision(1)
              << "Latency (tick-to-trade, ns)\n"
              << "  min : " << latencies.front() << '\n'
              << "  avg : " << avg               << '\n'
              << "  p50 : " << p50               << '\n'
              << "  p95 : " << p95               << '\n'
              << "  p99 : " << p99               << '\n'
              << "  max : " << latencies.back()  << '\n';

    marketData.printBBO();
}
