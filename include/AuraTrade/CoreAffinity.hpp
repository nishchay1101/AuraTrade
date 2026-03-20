#pragma once

#include <pthread.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#endif

namespace AuraTrade {

// Pins the calling thread to a specific logical CPU to minimise scheduler jitter.
//
// macOS: uses Thread Affinity Tags (hint to the kernel to colocate threads sharing the
//        same tag on the same physical core group — not a hard pin).
// Linux: uses pthread_setaffinity_np for a true hard binding.
class AffinityManager {
public:
    // Pin the currently running thread to `coreId`.
    // Returns true if the OS accepted the request.
    static bool pinCurrentThread(int coreId) noexcept {
#if defined(__APPLE__)
        thread_affinity_policy_data_t pol{ coreId };
        thread_port_t t = pthread_mach_thread_np(pthread_self());
        return thread_policy_set(t, THREAD_AFFINITY_POLICY,
                                 reinterpret_cast<thread_policy_t>(&pol),
                                 THREAD_AFFINITY_POLICY_COUNT) == KERN_SUCCESS;
#elif defined(__linux__)
        cpu_set_t cs;
        CPU_ZERO(&cs);
        CPU_SET(coreId, &cs);
        return pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs) == 0;
#else
        (void)coreId;
        return false;
#endif
    }
};

} // namespace AuraTrade
