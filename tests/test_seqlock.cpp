#include <gtest/gtest.h>
#include "OrderBook.h"
#include <thread>
#include <atomic>
#include <vector>

using namespace NanoMatch;

TEST(SeqlockTest, WaitFreeL2Snapshot) {
    OrderBook book;
    std::atomic<bool> keep_running{true};
    std::atomic<int> snapshot_count{0};
    std::atomic<bool> reader_ready{false};

    // Initialize book with some base orders
    book.addLimitOrder(Side::Buy, 100, 500);
    book.addLimitOrder(Side::Sell, 105, 500);

    // Reader Thread: Continuously poll L2 Snapshots
    std::thread reader([&]() {
        reader_ready.store(true, std::memory_order_release);
        while (keep_running.load(std::memory_order_relaxed)) {
            L2Snapshot snap = book.snapshotL2(5);
            
            // Prove data integrity: Top Bid must be <= Top Ask
            if (snap.num_bids > 0 && snap.num_asks > 0) {
                EXPECT_LE(snap.bids[0].price, snap.asks[0].price);
            }

            // Prove data integrity: Quantities must be positive
            for (int i = 0; i < snap.num_bids; ++i) {
                EXPECT_GT(snap.bids[i].qty, 0);
            }
            for (int i = 0; i < snap.num_asks; ++i) {
                EXPECT_GT(snap.asks[i].qty, 0);
            }

            snapshot_count.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Wait for reader to spin up
    while (!reader_ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    // Writer Thread (Main): Simulate extremely fast market activity
    for (int i = 0; i < 100000; ++i) {
        // Add random liquidity
        book.addLimitOrder(Side::Buy, 90 + (i % 10), 10);
        book.addLimitOrder(Side::Sell, 110 - (i % 10), 10);
        
        // Execute trades
        if (i % 5 == 0) {
            book.addLimitOrder(Side::Sell, 95, 5); // Hits a bid
            book.addLimitOrder(Side::Buy, 105, 5); // Hits an ask
        }
    }

    // Stop reader
    keep_running.store(false, std::memory_order_relaxed);
    reader.join();

    // Verify the reader actually managed to take wait-free snapshots while writer was blasting it
    EXPECT_GT(snapshot_count.load(), 0);
}
