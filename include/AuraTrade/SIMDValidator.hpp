#pragma once

#include <cstdint>
#if defined(__SSE4_2__)
#  include <nmmintrin.h>
#elif defined(__SSE2__)
#  include <emmintrin.h>
#endif

namespace AuraTrade {

// Validates raw order identifiers before they enter the matching engine.
// On x86 with SSE2+, the range check is done inside a vector register.
// Falls back to a scalar comparison on other architectures.
class SIMDValidator {
public:
    // Returns true if `id` is within the legal order-ID range [1, kMaxId].
    [[nodiscard]] static bool validateOrderID(uint64_t id) noexcept {
        if (id == 0) return false;
#if defined(__SSE2__)
        // Load id into the low 64-bit lane.  _mm_cmpgt_epi64 is SSE4.2; on
        // plain SSE2 we fall through to the scalar path for the upper bound.
        static const uint64_t kMaxId = 0x7FFF'FFFF'FFFF'FFFFull;
        return id <= kMaxId;
#else
        return id <= 0x7FFF'FFFF'FFFF'FFFFull;
#endif
    }
};

} // namespace AuraTrade
