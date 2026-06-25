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
#else
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
            
            // Set thread priority to Highest for HFT operations
            SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
            return true;
        #else
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(core_id, &cpuset);
            pthread_t thread = pthread_self();
            int result = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
            if (result != 0) {
                std::cerr << "Failed to pin thread to core " << core_id << std::endl;
                return false;
            }
            return true;
        #endif
    }

} // namespace NanoMatch

#endif // NANOMATCH_THREAD_UTILS_H
