#pragma once

#include "Types.hpp"
#include "SlabAllocator.hpp"
#include "Interfaces.hpp"
#include "SIMDValidator.hpp"
#include <map>
#include <unordered_map>
#include <algorithm>
#include <chrono>

namespace AuraTrade {

// Price-time priority limit order book with O(1) insertion, cancellation,
// and amortised O(N) matching per order (bounded by matched levels).
//
// Template parameters
//   OrderPoolSize — capacity of the Order slab pool.
//   LevelPoolSize — capacity of the LimitLevel slab pool.
template <std::size_t OrderPoolSize, std::size_t LevelPoolSize = 10'000>
class OrderBook {
public:
    OrderBook(SlabAllocator<Order, OrderPoolSize>&       orderAlloc,
              SlabAllocator<LimitLevel, LevelPoolSize>&  levelAlloc,
              IMarketDataHandler&                        mdHandler,
              IPersistenceHandler&                       persistHandler) noexcept
        : orders_(orderAlloc)
        , levels_(levelAlloc)
        , md_(mdHandler)
        , log_(persistHandler) {}

    OrderBook(const OrderBook&)            = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    // Submit an order. Ownership of `incoming` passes to the book; the caller
    // must not touch the pointer after this call.
    void processOrder(Order* incoming) {
        if (!SIMDValidator::validateOrderID(incoming->id)) {
            emitReport(incoming->id, 0, 0, incoming->side, OrderStatus::Rejected);
            orders_.deallocate(incoming);
            return;
        }

        if (incoming->type == OrderType::Market) {
            matchMarket(incoming);
            orders_.deallocate(incoming);
        } else {
            matchLimit(incoming);
        }
    }

    // O(1) cancellation via the order-map lookup + doubly-linked-list splice.
    // Returns false if the order ID is unknown or not resting in the book.
    bool cancelOrder(uint64_t orderId) {
        auto it = orderMap_.find(orderId);
        if (it == orderMap_.end()) return false;

        Order* order = it->second;
        removeFromLevel(order);
        emitReport(order->id, order->price, order->quantity,
                   order->side, OrderStatus::Canceled);
        orderMap_.erase(it);
        orders_.deallocate(order);
        return true;
    }

    std::size_t restingOrderCount() const noexcept { return orderMap_.size(); }

private:
    // ── Limit order flow ────────────────────────────────────────────────────

    void matchLimit(Order* in) {
        if (in->side == Side::Buy)  match(in, asks_, bids_);
        else                        match(in, bids_, asks_);

        if (in->quantity == 0) {
            in->status = OrderStatus::Filled;
            orders_.deallocate(in);
        } else if (in->tif == TimeInForce::IOC || in->tif == TimeInForce::FOK) {
            in->status = OrderStatus::Canceled;
            orders_.deallocate(in);
        } else {
            addToBook(in);
        }
    }

    // ── Market order flow ───────────────────────────────────────────────────

    void matchMarket(Order* in) {
        if (in->side == Side::Buy)  sweepBook(in, asks_);
        else                        sweepBook(in, bids_);
    }

    // ── Matching kernels ────────────────────────────────────────────────────

    template <typename OppBook, typename OwnBook>
    void match(Order* in, OppBook& opp, OwnBook& /*own*/) {
        auto it = opp.begin();
        while (it != opp.end() && in->quantity > 0) {
            const uint64_t lvlPrice = it->first;
            const bool cross = (in->side == Side::Buy) ? (in->price >= lvlPrice)
                                                        : (in->price <= lvlPrice);
            if (!cross) break;

            it = matchLevel(in, it, opp);
        }
    }

    template <typename Book>
    void sweepBook(Order* in, Book& book) {
        auto it = book.begin();
        while (it != book.end() && in->quantity > 0)
            it = matchLevel(in, it, book);
    }

    // Returns the next valid iterator after consuming / partially consuming the level.
    template <typename Book>
    typename Book::iterator matchLevel(Order* in, typename Book::iterator it, Book& book) {
        LimitLevel* level = it->second;
        Order* resting = level->head;

        while (resting && in->quantity > 0) {
            const uint32_t qty = std::min(in->quantity, resting->quantity);
            in->quantity      -= qty;
            resting->quantity -= qty;
            level->totalQuantity -= qty;

            const Side makerSide = (in->side == Side::Buy) ? Side::Sell : Side::Buy;
            emitReport(in->id, level->price, qty, in->side, OrderStatus::PartiallyFilled);
            md_.onLevelUpdate(makerSide, level->price,
                              level->totalQuantity, level->orderCount);

            if (resting->quantity == 0) {
                Order* next = resting->next;
                level->removeOrder(resting);
                orderMap_.erase(resting->id);
                orders_.deallocate(resting);
                resting = next;
            } else {
                resting->status = OrderStatus::PartiallyFilled;
                break;
            }
        }

        if (level->totalQuantity == 0) {
            levels_.deallocate(level);
            return book.erase(it);
        }
        return std::next(it);
    }

    // ── Book management ─────────────────────────────────────────────────────

    void addToBook(Order* order) {
        if (order->side == Side::Buy) {
            auto [it, ins] = bids_.emplace(order->price, nullptr);
            if (ins) it->second = levels_.allocate(order->price);
            it->second->addOrder(order);
            md_.onLevelUpdate(Side::Buy, order->price,
                              it->second->totalQuantity, it->second->orderCount);
        } else {
            auto [it, ins] = asks_.emplace(order->price, nullptr);
            if (ins) it->second = levels_.allocate(order->price);
            it->second->addOrder(order);
            md_.onLevelUpdate(Side::Sell, order->price,
                              it->second->totalQuantity, it->second->orderCount);
        }
        orderMap_[order->id] = order;
    }

    void removeFromLevel(Order* order) {
        if (order->side == Side::Buy) {
            auto it = bids_.find(order->price);
            if (it == bids_.end()) return;
            it->second->removeOrder(order);
            md_.onLevelUpdate(Side::Buy, order->price,
                              it->second->totalQuantity, it->second->orderCount);
            if (it->second->totalQuantity == 0) {
                levels_.deallocate(it->second);
                bids_.erase(it);
            }
        } else {
            auto it = asks_.find(order->price);
            if (it == asks_.end()) return;
            it->second->removeOrder(order);
            md_.onLevelUpdate(Side::Sell, order->price,
                              it->second->totalQuantity, it->second->orderCount);
            if (it->second->totalQuantity == 0) {
                levels_.deallocate(it->second);
                asks_.erase(it);
            }
        }
    }

    // ── Reporting ───────────────────────────────────────────────────────────

    void emitReport(uint64_t id, uint64_t price, uint32_t qty,
                    Side side, OrderStatus status) {
        const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const ExecutionReport rpt{id, price, qty, side, status, now_ns};
        log_.persist(rpt);
        md_.onTrade(rpt);
    }

    // ── Data members ────────────────────────────────────────────────────────

    SlabAllocator<Order, OrderPoolSize>&      orders_;
    SlabAllocator<LimitLevel, LevelPoolSize>& levels_;
    IMarketDataHandler&                       md_;
    IPersistenceHandler&                      log_;

    // Bids: highest price first  (std::greater comparator).
    std::map<uint64_t, LimitLevel*, std::greater<uint64_t>> bids_;
    // Asks: lowest price first   (default std::less comparator).
    std::map<uint64_t, LimitLevel*>                         asks_;

    std::unordered_map<uint64_t, Order*> orderMap_;
};

} // namespace AuraTrade
