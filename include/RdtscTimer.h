#ifndef NANOMATCH_RDTSC_TIMER_H
#define NANOMATCH_RDTSC_TIMER_H

#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>
#endif

namespace NanoMatch {

    class RdtscTimer {
    public:
        // Force inline for absolute zero overhead
        [[nodiscard]] static inline uint64_t now() noexcept {
            // Read CPU Time-Stamp Counter directly from hardware
            // Use __rdtscp to prevent CPU instruction reordering
            unsigned int aux;
            return __rdtscp(&aux);
        }
    };

}

#endif // NANOMATCH_RDTSC_TIMER_H
