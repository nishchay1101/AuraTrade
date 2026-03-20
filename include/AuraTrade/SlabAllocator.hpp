#pragma once

#include <atomic>
#include <vector>
#include <cstddef>

namespace AuraTrade {

// Lock-free slab allocator — zero heap activity on the hot path.
//
// Objects of type T must expose a `T* next` member (used as the free-list link)
// and a `void reset(Args...)` method that re-initialises the object after acquisition.
//
// Thread-safety: allocate() and deallocate() are safe to call concurrently from
// multiple threads (CAS free list).  The pool itself is allocated once in the
// constructor and never reallocated.
template <typename T, std::size_t PoolSize>
class SlabAllocator {
    static_assert(PoolSize > 0, "PoolSize must be > 0");

public:
    SlabAllocator() {
        pool_.resize(PoolSize);
        for (std::size_t i = 0; i + 1 < PoolSize; ++i)
            pool_[i].next = reinterpret_cast<T*>(&pool_[i + 1]);
        free_list_.store(reinterpret_cast<T*>(&pool_[0]), std::memory_order_relaxed);
    }

    SlabAllocator(const SlabAllocator&)            = delete;
    SlabAllocator& operator=(const SlabAllocator&) = delete;

    template <typename... Args>
    [[nodiscard]] T* allocate(Args&&... args) noexcept {
        T* obj = free_list_.load(std::memory_order_acquire);
        while (obj) {
            T* next = reinterpret_cast<T*>(obj->next);
            if (free_list_.compare_exchange_weak(obj, next,
                                                  std::memory_order_release,
                                                  std::memory_order_acquire))
                break;
        }
        if (obj) obj->reset(std::forward<Args>(args)...);
        return obj; // nullptr if pool is exhausted
    }

    void deallocate(T* obj) noexcept {
        if (!obj) return;
        T* head = free_list_.load(std::memory_order_acquire);
        do {
            obj->next = reinterpret_cast<T*>(head);
        } while (!free_list_.compare_exchange_weak(head, obj,
                                                    std::memory_order_release,
                                                    std::memory_order_acquire));
    }

private:
    std::vector<T> pool_;
    alignas(64) std::atomic<T*> free_list_{nullptr};
};

} // namespace AuraTrade
