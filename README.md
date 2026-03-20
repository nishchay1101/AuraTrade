# Aura-Trade — Low-Latency Matching Engine

A production-grade, sub-microsecond Limit Order Book (LOB) matching engine written in C++20.

## Architecture

```
include/AuraTrade/
├── AuraTrade.hpp       # Umbrella header — include this only
├── Types.hpp           # Order, LimitLevel, ExecutionReport, enums
├── SlabAllocator.hpp   # Lock-free slab pool (zero hot-path allocation)
├── SPSCQueue.hpp       # Single-producer / single-consumer ring buffer
├── Interfaces.hpp      # IMarketDataHandler, IPersistenceHandler
├── OrderBook.hpp       # Price-time priority matching engine
├── BinaryLogger.hpp    # Memory-mapped binary audit log
├── MarketData.hpp      # L2 best-bid/offer snapshot
├── CoreAffinity.hpp    # CPU thread pinning (Linux & macOS)
└── SIMDValidator.hpp   # Hardware-accelerated order-ID validation
main.cpp                # Stress-test / benchmark driver
Makefile
```

## Key Design Decisions

| Concern | Solution |
|---|---|
| Zero hot-path allocation | `SlabAllocator<T, N>` — CAS free-list, pool never grows |
| Lock-free inter-thread comms | `SPSCQueue<T, N>` — cache-line separated atomics, power-of-two capacity |
| O(1) order cancellation | Intrusive doubly-linked list inside each `LimitLevel` |
| Price-time priority | `std::map` (bids: `std::greater`, asks: `std::less`) |
| False-sharing prevention | `alignas(64)` on all hot atomic fields |
| Durability | `mmap`-backed `BinaryLogger` — memcpy cost only |
| Extensibility (SOLID) | `IMarketDataHandler` / `IPersistenceHandler` injected into `OrderBook` |

## Build

```bash
make          # produces ./matching_engine
make clean    # removes binary and audit.log
```

Requires: g++ ≥ 12, C++20, POSIX (Linux or macOS).

## Run

```bash
./matching_engine
```

Sample output:
```
Aura-Trade  —  low-latency matching engine benchmark
Orders: 100000

Latency (tick-to-trade, ns)
  min :  750
  avg :  42300.0
  p50 :  38000
  p95 : 110000
  p99 : 220000
  max : 950000
BBO  bid=101  ask=104
```
