#include <gtest/gtest.h>
#include "OrderBook.h"

using namespace NanoMatch;

TEST(OrderBookTest, BitShiftUndefinedBehaviorBoundary) {
    OrderBook book;
    book.warmup();
    
    // Add bids at bitset boundary (63) and just below it (62)
    book.addLimitOrder(Side::Buy, 63, 100);
    book.addLimitOrder(Side::Buy, 62, 100);
    
    // Match and completely fill the bid at 63
    book.addLimitOrder(Side::Sell, 63, 100);
    
    // The next best bid MUST now be 62.
    // If the UB occurs (shift by 64), it will return 0 instead of 62.
    // We test this by placing an ask at 62. It should immediately execute.
    OrderId match_id = book.addLimitOrder(Side::Sell, 62, 50);
    
    // Since it executes against the resting bid at 62, the incoming ask is fully filled (returns EXECUTED_ORDER)
    EXPECT_EQ(match_id, EXECUTED_ORDER);
}

#include <random>

TEST(OrderBookTest, MassiveIntegrationAndL2DepthIntegrity) {
    OrderBook book;
    book.warmup();

    std::mt19937 gen(1337);
    std::uniform_int_distribution<Price> price_dist(100, 200);
    std::uniform_int_distribution<Quantity> qty_dist(10, 50);
    std::uniform_int_distribution<int> side_dist(0, 1);

    // Blast 10,000 randomized crossing and non-crossing orders
    for (int i = 0; i < 10000; ++i) {
        Side side = side_dist(gen) == 0 ? Side::Buy : Side::Sell;
        book.addLimitOrder(side, price_dist(gen), qty_dist(gen));
    }

    // Take an L2 Snapshot
    L2Snapshot snap = book.snapshotL2(10);

    // 1. Verify Top of Book Integrity
    if (snap.num_bids > 0 && snap.num_asks > 0) {
        // The highest bid must be strictly less than the lowest ask, 
        // otherwise they would have crossed and matched!
        EXPECT_LT(snap.bids[0].price, snap.asks[0].price);
    }

    // 2. Verify sorted order (Bids descending, Asks ascending)
    for (int i = 1; i < snap.num_bids; ++i) {
        EXPECT_LT(snap.bids[i].price, snap.bids[i-1].price);
        EXPECT_GT(snap.bids[i].qty, 0);
    }
    for (int i = 1; i < snap.num_asks; ++i) {
        EXPECT_GT(snap.asks[i].price, snap.asks[i-1].price);
        EXPECT_GT(snap.asks[i].qty, 0);
    }
}

TEST(OrderBookTest, AddLimitOrder) {
    OrderBook book;
    
    // Add a Buy order at $150.25 (15025 ticks)
    OrderId id1 = book.addLimitOrder(Side::Buy, 15025, 100);
    EXPECT_NE(id1, NULL_ORDER);

    // Add a Sell order at $150.30 (15030 ticks)
    OrderId id2 = book.addLimitOrder(Side::Sell, 15030, 50);
    EXPECT_NE(id2, NULL_ORDER);

    // TODO: Verify best_bid and best_ask when accessor methods are added
}

TEST(OrderBookTest, CancelOrder_MiddleOfQueue) {
    OrderBook book;
    book.warmup();

    // Add three bids at the same price
    OrderId id1 = book.addLimitOrder(Side::Buy, 100, 10);
    OrderId id2 = book.addLimitOrder(Side::Buy, 100, 20);
    OrderId id3 = book.addLimitOrder(Side::Buy, 100, 30);
    EXPECT_NE(id1, NULL_ORDER);
    EXPECT_NE(id3, NULL_ORDER);

    L2Snapshot snap1 = book.snapshotL2(1);
    EXPECT_EQ(snap1.num_bids, 1);
    EXPECT_EQ(snap1.bids[0].qty, 60);

    // Cancel the middle order
    bool result = book.cancelOrder(Side::Buy, id2);
    EXPECT_TRUE(result);

    L2Snapshot snap2 = book.snapshotL2(1);
    EXPECT_EQ(snap2.bids[0].qty, 40); // 10 + 30

    // Match the remaining orders to verify queue linkage (id1 then id3)
    book.addLimitOrder(Side::Sell, 100, 10); // Should fill id1 entirely
    book.addLimitOrder(Side::Sell, 100, 30); // Should fill id3 entirely

    L2Snapshot snap3 = book.snapshotL2(1);
    EXPECT_EQ(snap3.num_bids, 0); // Book should be empty
}

TEST(OrderBookTest, CancelOrder_HeadClearsBestBid) {
    OrderBook book;
    book.warmup();

    OrderId bid1 = book.addLimitOrder(Side::Buy, 100, 10);
    OrderId bid2 = book.addLimitOrder(Side::Buy, 99, 10);
    EXPECT_NE(bid2, NULL_ORDER);

    L2Snapshot snap1 = book.snapshotL2(2);
    EXPECT_EQ(snap1.bids[0].price, 100);
    EXPECT_EQ(snap1.bids[1].price, 99);

    // Cancel the best bid
    book.cancelOrder(Side::Buy, bid1);

    // The next best bid should now be 99
    L2Snapshot snap2 = book.snapshotL2(2);
    EXPECT_EQ(snap2.num_bids, 1);
    EXPECT_EQ(snap2.bids[0].price, 99);
}
