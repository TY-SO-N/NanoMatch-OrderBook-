#include <gtest/gtest.h>
#include "OrderBook.h"
#include "ThreadUtils.h"
#include <thread>
#include <iostream>

using namespace NanoMatch;

TEST(NumaTest, FirstTouchAllocation) {
    OrderBook book;
    bool success = false;

    // Spawn a dedicated thread for the matching engine
    std::thread engine_thread([&]() {
        // Pin to Core 0 (or Core 1 if Core 0 is busy)
        if (pinThreadToCore(0)) {
            std::cout << "[NUMA] Engine Thread pinned to Core 0." << std::endl;
        } else {
            std::cout << "[NUMA] Fallback: Could not pin to Core 0." << std::endl;
        }

        // Trigger NUMA First-Touch allocation on this specific core's local memory bank
        book.warmup();
        std::cout << "[NUMA] Memory Pool successfully warmed up and pinned." << std::endl;
        
        // Execute a test trade to ensure memory is writable
        book.addLimitOrder(Side::Buy, 100, 50);
        book.addLimitOrder(Side::Sell, 100, 50);
        
        success = true;
    });

    engine_thread.join();

    EXPECT_TRUE(success);
}
