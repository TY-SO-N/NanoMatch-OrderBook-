#ifndef NANOMATCH_MEMORY_POOL_H
#define NANOMATCH_MEMORY_POOL_H

#include "types.h"
#include "LinuxTuning.h"    // make_hugepage (Linux huge pages), cpu_pause
#include <cstddef>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <iostream>

#if defined(_WIN32) || defined(_MSC_VER)
#include <malloc.h>
#endif

namespace NanoMatch {

    // Custom Aligned Allocator to guarantee 64-byte cache alignment for AVX-512
    // On Linux: first attempts 2MB Huge Pages (reduces TLB misses by 99.8% for the 512MB pool),
    // then falls back to posix_memalign if huge pages are unavailable.
    // On Windows: uses _aligned_malloc with 64-byte alignment.
    template<typename T>
    inline std::unique_ptr<T[], void(*)(void*)> make_aligned(size_t count) {
        #if defined(_WIN32) || defined(_MSC_VER)
            size_t bytes = count * sizeof(T);
            bytes = (bytes + 63) & ~63;
            void* ptr = _aligned_malloc(bytes, 64);
            // Do NOT zero-initialize here to preserve NUMA First-Touch semantics
            return { static_cast<T*>(ptr), [](void* p) { _aligned_free(p); } };

        #elif defined(__linux__)
            // Try huge pages first (2MB pages, best for large allocations)
            auto hp = make_hugepage<T>(count);
            if (hp) return hp;
            // Fallback: standard 64-byte aligned allocation
            size_t bytes = count * sizeof(T);
            bytes = (bytes + 63) & ~63;
            void* ptr = nullptr;
            if (posix_memalign(&ptr, 64, bytes) != 0) ptr = nullptr;
            // Do NOT zero-initialize here to preserve NUMA First-Touch semantics
            return { static_cast<T*>(ptr), [](void* p) { std::free(p); } };

        #else
            // Generic Unix (macOS, etc.)
            size_t bytes = count * sizeof(T);
            bytes = (bytes + 63) & ~63;
            void* ptr = nullptr;
            if (posix_memalign(&ptr, 64, bytes) != 0) ptr = nullptr;
            // Do NOT zero-initialize here to preserve NUMA First-Touch semantics
            return { static_cast<T*>(ptr), [](void* p) { std::free(p); } };
        #endif
    }

    // Pre-allocate ~16.7 million orders (2^24) for bitwise masking instead of modulo
    constexpr size_t POOL_SIZE = 16777216; 
    constexpr size_t POOL_MASK = POOL_SIZE - 1;

    // Zero for OrderId means "Null" or "Invalid" because 0 is our list terminator

    /**
     * SoA (Structure of Arrays) Object Pool
     * By separating the properties into parallel arrays, we maximize CPU cache line density 
     * and allow AVX-512 SIMD vectorization across the `prices` and `quantities` arrays.
     */
    struct alignas(64) OrderPool {
        
        // Heap allocate the massive arrays with 64-byte AVX-512 alignment
        std::unique_ptr<Price[], void(*)(void*)> prices;
        std::unique_ptr<Quantity[], void(*)(void*)> quantities;
        
        // Intrusive Doubly-Linked List pointers
        std::unique_ptr<OrderId[], void(*)(void*)> next;
        std::unique_ptr<OrderId[], void(*)(void*)> prev;

        OrderId free_list_head;

        OrderPool() 
            : prices(make_aligned<Price>(POOL_SIZE)),
              quantities(make_aligned<Quantity>(POOL_SIZE)),
              next(make_aligned<OrderId>(POOL_SIZE)),
              prev(make_aligned<OrderId>(POOL_SIZE)) {
            
            // Abort immediately if the OS cannot fulfill the 536MB memory allocation
            if (!prices || !quantities || !next || !prev) [[unlikely]] {
                std::abort();
            }

            // Initialize the free list to point to the next available slot
            free_list_head = 1; // 0 is reserved for NULL_ORDER
            for (size_t i = 1; i < POOL_SIZE - 1; ++i) {
                next[i] = i + 1;
                prev[i] = NULL_ORDER;
            }
            next[POOL_SIZE - 1] = NULL_ORDER;
        }

        // Enforce NUMA First-Touch: Must be called by the Pinned matching engine thread!
        inline void warmup() noexcept {
            std::memset(prices.get(), 0, POOL_SIZE * sizeof(Price));
            std::memset(quantities.get(), 0, POOL_SIZE * sizeof(Quantity));
            std::memset(next.get(), 0, POOL_SIZE * sizeof(OrderId));
            std::memset(prev.get(), 0, POOL_SIZE * sizeof(OrderId));

            // Initialize the free list to point to the next available slot
            free_list_head = 1; // 0 is reserved for NULL_ORDER
            for (size_t i = 1; i < POOL_SIZE - 1; ++i) {
                next[i] = i + 1;
                prev[i] = NULL_ORDER;
            }
            next[POOL_SIZE - 1] = NULL_ORDER;
        }

        // Allocates a node in pure O(1) time without OS intervention
        [[nodiscard]] inline OrderId allocate(Price p, Quantity q) {
            if (free_list_head == NULL_ORDER) [[unlikely]] {
                return NULL_ORDER; // Out of memory
            }

            OrderId id = free_list_head;
            free_list_head = next[id]; // Pop from free list

            prices[id] = p;
            quantities[id] = q;
            next[id] = NULL_ORDER;
            prev[id] = NULL_ORDER;

            return id;
        }

        // Recycles a node in pure O(1) time
        inline void deallocate(OrderId id) {
            // Push back onto the free list
            next[id] = free_list_head;
            free_list_head = id;
        }
    };

}

#endif // NANOMATCH_MEMORY_POOL_H
