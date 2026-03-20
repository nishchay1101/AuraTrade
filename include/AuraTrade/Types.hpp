#pragma once

#include <cstdint>
#include <chrono>

namespace AuraTrade {

enum class Side : uint8_t { Buy, Sell };

enum class OrderType : uint8_t {
    Limit,
    Market
};

// Time-In-Force flags that govern a limit order's remaining-quantity behaviour.
enum class TimeInForce : uint8_t {
    GTC, // Good-Till-Cancel  — rests on the book if not immediately matched
    IOC, // Immediate-Or-Cancel — any unmatched remainder is discarded
    FOK  // Fill-Or-Kill       — must fill entirely or be rejected outright
};

enum class OrderStatus : uint8_t {
    New,
    PartiallyFilled,
    Filled,
    Canceled,
    Rejected
};

// Intrusive node used inside LimitLevel's doubly-linked queue.
// The `next` pointer is reused by SlabAllocator as a free-list link.
struct Order {
    uint64_t  id;
    uint64_t  price;
    uint32_t  quantity;
    Side      side;
    OrderType type;
    TimeInForce tif;
    OrderStatus status;
    std::chrono::time_point<std::chrono::steady_clock> timestamp;

    Order* next = nullptr;
    Order* prev = nullptr;

    void reset(uint64_t id_, Side side_, uint64_t price_, uint32_t qty_,
               OrderType type_ = OrderType::Limit,
               TimeInForce tif_ = TimeInForce::GTC) noexcept {
        id        = id_;
        side      = side_;
        price     = price_;
        quantity  = qty_;
        type      = type_;
        tif       = tif_;
        status    = OrderStatus::New;
        timestamp = std::chrono::steady_clock::now();
        next      = nullptr;
        prev      = nullptr;
    }
};

// Price level in the book — FIFO queue of orders at a single price.
// The `next` pointer is reused by SlabAllocator as a free-list link.
struct LimitLevel {
    uint64_t  price             = 0;
    uint32_t  totalQuantity     = 0;
    uint32_t  orderCount        = 0;
    Order*    head              = nullptr;
    Order*    tail              = nullptr;
    LimitLevel* next            = nullptr;

    void reset(uint64_t price_) noexcept {
        price         = price_;
        totalQuantity = 0;
        orderCount    = 0;
        head          = nullptr;
        tail          = nullptr;
        next          = nullptr;
    }

    void addOrder(Order* o) noexcept {
        o->prev = tail;
        o->next = nullptr;
        if (tail) tail->next = o; else head = o;
        tail = o;
        totalQuantity += o->quantity;
        ++orderCount;
    }

    void removeOrder(Order* o) noexcept {
        if (o->prev) o->prev->next = o->next; else head = o->next;
        if (o->next) o->next->prev = o->prev; else tail = o->prev;
        totalQuantity -= o->quantity;
        --orderCount;
    }
};

// POD execution report written to the binary audit log.
struct ExecutionReport {
    uint64_t    orderId;
    uint64_t    price;
    uint32_t    quantity;
    Side        side;
    OrderStatus status;
    int64_t     timestamp_ns;
};

} // namespace AuraTrade
