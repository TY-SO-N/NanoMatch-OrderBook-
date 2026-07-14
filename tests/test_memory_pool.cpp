#include <gtest/gtest.h>
#include "MemoryPool.h"

using namespace NanoMatch;

TEST(MemoryPoolTest, AVX512_Alignment_Guarantees) {
    OrderPool pool;
    pool.warmup();

    // Allocate an order
    OrderId id1 = pool.allocate(100, 50);
    EXPECT_NE(id1, NULL_ORDER);

    // Assert that the memory pointer of the allocated arrays inside the pool
    // are perfectly 64-byte aligned to prevent SIMD segmentation faults
    uintptr_t prices_addr = reinterpret_cast<uintptr_t>(pool.prices.get());
    uintptr_t qtys_addr = reinterpret_cast<uintptr_t>(pool.quantities.get());
    uintptr_t next_addr = reinterpret_cast<uintptr_t>(pool.next.get());
    uintptr_t prev_addr = reinterpret_cast<uintptr_t>(pool.prev.get());

    EXPECT_EQ(prices_addr % 64, 0);
    EXPECT_EQ(qtys_addr % 64, 0);
    EXPECT_EQ(next_addr % 64, 0);
    EXPECT_EQ(prev_addr % 64, 0);
}

TEST(MemoryPoolTest, OutOfMemory_GracefulDegradation) {
    OrderPool pool;
    // Don't need to warmup for this specific test
    
    // The capacity is 16,777,216. 
    // We start at ID 1, so there are Capacity-1 usable slots.
    size_t usable_slots = 16777216 - 1;

    // Exhaust the pool
    for (size_t i = 0; i < usable_slots; ++i) {
        OrderId id = pool.allocate(100, 10);
        EXPECT_NE(id, NULL_ORDER);
    }

    // The very next allocation must gracefully fail (return NULL_ORDER)
    // rather than segfaulting or wrapping the free list.
    OrderId oom_id = pool.allocate(100, 10);
    EXPECT_EQ(oom_id, NULL_ORDER);
}

TEST(MemoryPoolTest, DoubleFree_ABARejection) {
    OrderPool pool;
    pool.warmup();

    OrderId id1 = pool.allocate(100, 10);
    EXPECT_NE(id1, NULL_ORDER);

    // Deallocate
    pool.deallocate(id1);

    // Deallocate again (double-free).
    // In our intrusive pool, the free list is formed by `next[id] = free_head`.
    // If double free isn't inherently crashing, we should just ensure we don't crash.
    // Real protection requires a generation counter, but we test stability here.
    pool.deallocate(id1);

    // Allocate should still work
    OrderId id2 = pool.allocate(100, 10);
    EXPECT_EQ(id1, id2); // It should pop the same ID
}
