#pragma once

#include "Interfaces.hpp"
#include <iostream>
#include <map>

namespace AuraTrade {

// Maintains a local best-bid/offer view from level-update callbacks.
// Consumers can call printBBO() at any time to inspect the current top of book.
class L2MarketData final : public IMarketDataHandler {
public:
    void onTrade(const ExecutionReport&) override {}

    void onLevelUpdate(Side side, uint64_t price,
                       uint32_t totalQty, uint32_t /*orderCount*/) override {
        if (side == Side::Buy) {
            if (totalQty == 0) bids_.erase(price);
            else               bids_[price] = totalQty;
        } else {
            if (totalQty == 0) asks_.erase(price);
            else               asks_[price] = totalQty;
        }
    }

    void printBBO() const {
        const auto bestBid = bids_.empty() ? 0ULL : bids_.rbegin()->first;
        const auto bestAsk = asks_.empty() ? 0ULL : asks_.begin()->first;
        std::cout << "BBO  bid=" << bestBid << "  ask=" << bestAsk << '\n';
    }

private:
    std::map<uint64_t, uint32_t>                     bids_; // ascending  — highest at rbegin
    std::map<uint64_t, uint32_t, std::greater<>>     asks_; // descending — lowest  at begin
};

} // namespace AuraTrade
