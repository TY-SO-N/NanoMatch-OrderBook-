#include <gtest/gtest.h>
#include "OrderBook.h"

using namespace NanoMatch;

TEST(MatchingLogicTest, PriceTimePriority_FIFO) {
    OrderBook book;
    book.warmup();

    // Insert 3 bids at the exact same price: $100
    // They MUST be executed in exactly this order (FIFO)
    OrderId id1 = book.addLimitOrder(Side::Buy, 100, 10);
    OrderId id2 = book.addLimitOrder(Side::Buy, 100, 20);
    OrderId id3 = book.addLimitOrder(Side::Buy, 100, 30);
    
    EXPECT_NE(id1, NULL_ORDER);
    EXPECT_NE(id2, NULL_ORDER);
    EXPECT_NE(id3, NULL_ORDER);

    // Verify L2 Depth is 60
    L2Snapshot snap1 = book.snapshotL2(1);
    EXPECT_EQ(snap1.bids[0].qty, 60);

    // Hit the bids with a Sell order for 15 contracts
    // This should consume id1 (10) entirely, and id2 partially (5 remaining)
    OrderId sell_id = book.addLimitOrder(Side::Sell, 100, 15);
    EXPECT_EQ(sell_id, EXECUTED_ORDER);

    // Verify L2 Depth is 45
    L2Snapshot snap2 = book.snapshotL2(1);
    EXPECT_EQ(snap2.bids[0].qty, 45);

    // Hit with a Sell order for 25 contracts
    // This should consume id2's remaining 15 entirely, and id3 partially (20 remaining)
    OrderId sell2_id = book.addLimitOrder(Side::Sell, 100, 25);
    EXPECT_EQ(sell2_id, EXECUTED_ORDER);

    // Verify L2 Depth is 20
    L2Snapshot snap3 = book.snapshotL2(1);
    EXPECT_EQ(snap3.bids[0].qty, 20);

    // Cancel id3 to verify it was indeed id3 that had 20 remaining
    bool cancel_res = book.cancelOrder(Side::Buy, id3);
    EXPECT_TRUE(cancel_res);

    L2Snapshot snap4 = book.snapshotL2(1);
    EXPECT_EQ(snap4.num_bids, 0); // Book is now empty
}

TEST(MatchingLogicTest, CrossingTheSpread) {
    OrderBook book;
    book.warmup();

    // Create a spread
    // Asks: 101 (qty 10), 102 (qty 20), 103 (qty 30)
    OrderId ask1 = book.addLimitOrder(Side::Sell, 101, 10);
    OrderId ask2 = book.addLimitOrder(Side::Sell, 102, 20);
    OrderId ask3 = book.addLimitOrder(Side::Sell, 103, 30);
    
    EXPECT_NE(ask1, NULL_ORDER);
    EXPECT_NE(ask2, NULL_ORDER);
    EXPECT_NE(ask3, NULL_ORDER);

    // Aggressive Buy order crossing the spread: Buy 45 contracts @ 105
    // It should wipe out 101, 102, and take 15 from 103.
    OrderId aggressive_buy = book.addLimitOrder(Side::Buy, 105, 45);
    EXPECT_EQ(aggressive_buy, EXECUTED_ORDER);

    // Verify the remaining book
    L2Snapshot snap = book.snapshotL2(2);
    EXPECT_EQ(snap.num_asks, 1);
    EXPECT_EQ(snap.asks[0].price, 103);
    EXPECT_EQ(snap.asks[0].qty, 15); // 30 - 15 = 15
}
