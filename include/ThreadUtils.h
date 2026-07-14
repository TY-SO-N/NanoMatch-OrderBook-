#ifndef NANOMATCH_THREAD_UTILS_H
#define NANOMATCH_THREAD_UTILS_H

#include <thread>
#include <iostream>

#if defined(_WIN32) || defined(_MSC_VER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#else
// macOS / other Unix: pthreads available but no thread affinity API
#include <pthread.h>
#endif

namespace NanoMatch {

    inline bool pinThreadToCore(int core_id) {
        #if defined(_WIN32) || defined(_MSC_VER)
            HANDLE thread = GetCurrentThread();
            DWORD_PTR mask = (1ULL << core_id);
            DWORD_PTR result = SetThreadAffinityMask(thread, mask);
            if (result == 0) {
                std::cerr << "Failed to pin thread to core " << core_id << std::endl;
                return false;
            }
            // Set thread priority to TIME_CRITICAL for HFT operations
            SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
            return true;

        #elif defined(__linux__)
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(core_id, &cpuset);
            pthread_t thread = pthread_self();
            int result = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
            if (result != 0) {
                std::cerr << "Failed to pin thread to core " << core_id << std::endl;
                return false;
            }
            // Set SCHED_FIFO real-time scheduling (requires root or CAP_SYS_NICE)
            // This is the Linux equivalent of THREAD_PRIORITY_TIME_CRITICAL
            struct sched_param param;
            param.sched_priority = sched_get_priority_max(SCHED_FIFO);
            if (pthread_setschedparam(thread, SCHED_FIFO, &param) != 0) {
                std::cerr << "[WARN] Could not set SCHED_FIFO (need root or CAP_SYS_NICE)" << std::endl;
                // Non-fatal: engine still works without real-time priority
            }
            return true;

        #else
            // macOS / other Unix: no thread affinity API available
            // (macOS removed pthread_setaffinity_np in 10.5)
            (void)core_id;
            std::cerr << "[WARN] Thread pinning not supported on this OS — running without affinity." << std::endl;
            return true; // Non-fatal
        #endif
    }

} // namespace NanoMatch

#endif // NANOMATCH_THREAD_UTILS_H
