#pragma once

#include "Types.hpp"

namespace AuraTrade {

// Callback interface for real-time L2 market data updates.
class IMarketDataHandler {
public:
    virtual ~IMarketDataHandler() = default;
    virtual void onTrade(const ExecutionReport& report) = 0;
    virtual void onLevelUpdate(Side side, uint64_t price,
                               uint32_t totalQty, uint32_t orderCount) = 0;
};

// Callback interface for binary audit-trail persistence.
class IPersistenceHandler {
public:
    virtual ~IPersistenceHandler() = default;
    virtual void persist(const ExecutionReport& report) = 0;
};

} // namespace AuraTrade
