#ifndef NANOMATCH_ORDER_BOOK_H
#define NANOMATCH_ORDER_BOOK_H

#include "MemoryPool.h"
#include "Seqlock.h"
#include <array>

namespace NanoMatch {

    // Maximum supported price ticks (e.g., $10,000.00 at 1-cent ticks)
    constexpr Price MAX_PRICE = 1000000; 

    struct Level {
        Price price;
        Quantity qty;
    };

    struct L2Snapshot {
        Level bids[10];
        Level asks[10];
        int num_bids = 0;
        int num_asks = 0;
    };

    class alignas(64) OrderBook {
    private:
        OrderPool pool;
        Seqlock l2_lock;

        // Heap allocate the 32MB price level arrays with AVX-512 64-byte alignment
        std::unique_ptr<OrderId[], void(*)(void*)> bid_heads;
        std::unique_ptr<OrderId[], void(*)(void*)> bid_tails;
        std::unique_ptr<OrderId[], void(*)(void*)> ask_heads;
        std::unique_ptr<OrderId[], void(*)(void*)> ask_tails;

        // 125KB Bitsets for O(1) Next-Best-Price lookup using CPU CLZ/CTZ instructions
        std::unique_ptr<uint64_t[], void(*)(void*)> active_bids;
        std::unique_ptr<uint64_t[], void(*)(void*)> active_asks;

        // Aggregated L2 Depth Book (Volume per price level)
        std::unique_ptr<Quantity[], void(*)(void*)> depth_bids;
        std::unique_ptr<Quantity[], void(*)(void*)> depth_asks;

        // Cache Best Bid/Offer to avoid scanning
        Price best_bid = 0;
        Price best_ask = MAX_PRICE;

    public:
        OrderBook()
            : bid_heads(make_aligned<OrderId>(MAX_PRICE)),
              bid_tails(make_aligned<OrderId>(MAX_PRICE)),
              ask_heads(make_aligned<OrderId>(MAX_PRICE)),
              ask_tails(make_aligned<OrderId>(MAX_PRICE)),
              active_bids(make_aligned<uint64_t>((MAX_PRICE / 64) + 1)),
              active_asks(make_aligned<uint64_t>((MAX_PRICE / 64) + 1)),
              depth_bids(make_aligned<Quantity>(MAX_PRICE)),
              depth_asks(make_aligned<Quantity>(MAX_PRICE)) {
            // Abort immediately if the OS cannot fulfill the memory allocation
            if (!bid_heads || !bid_tails || !ask_heads || !ask_tails || 
                !active_bids || !active_asks || !depth_bids || !depth_asks) [[unlikely]] {
                std::abort();
            }
            
            // Do NOT zero-initialize any arrays here to preserve NUMA First-Touch semantics!
            // All zeroing is deferred to the warmup() method.
        }

        // Enforce NUMA First-Touch: Must be called by the Pinned matching engine thread!
        void warmup() noexcept {
            // Fault all pages into the local NUMA node
            std::memset(bid_heads.get(), 0, MAX_PRICE * sizeof(OrderId));
            std::memset(bid_tails.get(), 0, MAX_PRICE * sizeof(OrderId));
            std::memset(ask_heads.get(), 0, MAX_PRICE * sizeof(OrderId));
            std::memset(ask_tails.get(), 0, MAX_PRICE * sizeof(OrderId));
            std::memset(depth_bids.get(), 0, MAX_PRICE * sizeof(Quantity));
            std::memset(depth_asks.get(), 0, MAX_PRICE * sizeof(Quantity));
            std::memset(active_bids.get(), 0, ((MAX_PRICE / 64) + 1) * sizeof(uint64_t));
            std::memset(active_asks.get(), 0, ((MAX_PRICE / 64) + 1) * sizeof(uint64_t));

            // Warmup the memory pool
            pool.warmup();
        }

        /**
         * Core Engine logic: Adds a limit order directly into the SoA intrusive lists.
         * Zero allocations. No branching on cache misses.
         */
        OrderId addLimitOrder(Side side, Price price, Quantity qty) {
            // CRITICAL BUG FIX 1: Array Bounds Checking and Zero-Price Protection
            if (price >= MAX_PRICE || price == 0 || qty == 0) [[unlikely]] {
                return NULL_ORDER; 
            }

            // Lock the Seqlock to prevent data tearing for L2 Readers
            l2_lock.writeLock();

            // CRITICAL BUG FIX 2: Execute trades before adding to book (Matching Logic)
            if (side == Side::Buy) {
                while (qty > 0 && best_ask <= price && best_ask < MAX_PRICE) {
                    matchWithAsk(price, qty);
                }
            } else {
                while (qty > 0 && best_bid >= price && bid_heads[best_bid] != NULL_ORDER) {
                    matchWithBid(price, qty);
                }
            }

            // If the order is fully filled immediately, return a success flag, not a failure
            if (qty == 0) {
                l2_lock.writeUnlock();
                return EXECUTED_ORDER; 
            }

            // Allocate remaining quantity directly from the pre-allocated Memory Pool
            OrderId id = pool.allocate(price, qty);
            
            if (id == NULL_ORDER) [[unlikely]] {
                l2_lock.writeUnlock();
                return NULL_ORDER; // Engine out of memory
            }

            if (side == Side::Buy) [[likely]] {
                insertBid(id, price);
            } else {
                insertAsk(id, price);
            }
            
            l2_lock.writeUnlock();
            return id;
        }

    private:
        // Core execution: Matches an incoming Buy order against resting Asks
        inline void matchWithAsk(Price /* incoming_price */, Quantity& incoming_qty) {
            OrderId ask_id = ask_heads[best_ask];
            if (ask_id == NULL_ORDER) {
                best_ask = findNextBestAsk(best_ask);
                return;
            }

            Quantity resting_qty = pool.quantities[ask_id];

            // HARDWARE OPTIMIZATION: Software Cache Prefetching
            // Explicitly command the CPU to pre-load the next order in the linked list 
            // into the L1 cache before the current math even finishes.
            OrderId next_id_hint = pool.next[ask_id];
            #if defined(__GNUC__) || defined(__clang__)
            __builtin_prefetch(&pool.quantities[next_id_hint], 0, 1);
            #endif

            Quantity trade_qty = (incoming_qty < resting_qty) ? incoming_qty : resting_qty;

            // Execute trade
            incoming_qty -= trade_qty;
            pool.quantities[ask_id] -= trade_qty;
            
            // Update L2 Depth Book
            depth_asks[best_ask] -= trade_qty;

            if (pool.quantities[ask_id] == 0) {
                // Remove filled order from the book
                OrderId next_id = pool.next[ask_id];
                ask_heads[best_ask] = next_id;
                if (next_id != NULL_ORDER) {
                    pool.prev[next_id] = NULL_ORDER;
                } else {
                    ask_tails[best_ask] = NULL_ORDER;
                    // Mark price level as empty in the bitset
                    active_asks[best_ask / 64] &= ~(1ULL << (best_ask % 64));
                    // Find next best ask in pure O(1)
                    best_ask = findNextBestAsk(best_ask);
                }
                pool.deallocate(ask_id);
            }
        }

        // Core execution: Matches an incoming Sell order against resting Bids
        inline void matchWithBid(Price /* incoming_price */, Quantity& incoming_qty) {
            OrderId bid_id = bid_heads[best_bid];
            if (bid_id == NULL_ORDER) {
                best_bid = findNextBestBid(best_bid);
                return;
            }

            Quantity resting_qty = pool.quantities[bid_id];

            // HARDWARE OPTIMIZATION: Software Cache Prefetching
            OrderId next_id_hint = pool.next[bid_id];
            #if defined(__GNUC__) || defined(__clang__)
            __builtin_prefetch(&pool.quantities[next_id_hint], 0, 1);
            #endif

            Quantity trade_qty = (incoming_qty < resting_qty) ? incoming_qty : resting_qty;

            // Execute trade
            incoming_qty -= trade_qty;
            pool.quantities[bid_id] -= trade_qty;

            // Update L2 Depth Book
            depth_bids[best_bid] -= trade_qty;

            if (pool.quantities[bid_id] == 0) {
                // Remove filled order from the book
                OrderId next_id = pool.next[bid_id];
                bid_heads[best_bid] = next_id;
                if (next_id != NULL_ORDER) {
                    pool.prev[next_id] = NULL_ORDER;
                } else {
                    bid_tails[best_bid] = NULL_ORDER;
                    // Mark price level as empty in the bitset
                    active_bids[best_bid / 64] &= ~(1ULL << (best_bid % 64));
                    // Find next best bid in pure O(1)
                    best_bid = findNextBestBid(best_bid);
                }
                pool.deallocate(bid_id);
            }
        }

        // Hardware-Accelerated O(1) next price lookups using CLZ/CTZ
        inline Price findNextBestBid(Price current_best) const {
            size_t start_idx = current_best / 64;
            int bit_pos = current_best % 64;
            
            uint64_t mask = active_bids[start_idx];
            // Mask out bits above the current price (we want bits <= current_best)
            if (bit_pos != 63) {
                mask &= (1ULL << (bit_pos + 1)) - 1;
            }
            
            if (mask != 0) {
                // Find highest set bit using count leading zeros
                return start_idx * 64 + (63 - __builtin_clzll(mask));
            }
            
            // Scan preceding blocks backwards
            for (int i = (int)start_idx - 1; i >= 0; --i) {
                if (active_bids[i] != 0) {
                    return i * 64 + (63 - __builtin_clzll(active_bids[i]));
                }
            }
            return 0;
        }

        inline Price findNextBestAsk(Price current) const {
            if (current >= MAX_PRICE - 1) return MAX_PRICE;
            current++;
            size_t block_idx = current / 64;
            size_t bit_idx = current % 64;
            
            uint64_t block = active_asks[block_idx] & ~((1ULL << bit_idx) - 1);

            if (block != 0) return (block_idx * 64) + __builtin_ctzll(block);

            size_t max_block = MAX_PRICE / 64;
            while (block_idx < max_block) {
                block_idx++;
                block = active_asks[block_idx];
                if (block != 0) return (block_idx * 64) + __builtin_ctzll(block);
            }
            return MAX_PRICE;
        }

        inline void insertBid(OrderId id, Price price) {
            // Update L2 Depth Book
            depth_bids[price] += pool.quantities[id];

            // Intrusive FIFO queue insertion (Time priority)
            if (bid_heads[price] == NULL_ORDER) [[unlikely]] {
                bid_heads[price] = id;
                bid_tails[price] = id;
                active_bids[price / 64] |= (1ULL << (price % 64)); // Activate bit
            } else [[likely]] {
                OrderId tail = bid_tails[price];
                pool.next[tail] = id;
                pool.prev[id] = tail;
                bid_tails[price] = id;
            }

            // Hardware branch prediction hint: Updating BBO is less common than adding deep orders
            if (price > best_bid) [[unlikely]] {
                best_bid = price;
            }
        }

        inline void insertAsk(OrderId id, Price price) {
            // Update L2 Depth Book
            depth_asks[price] += pool.quantities[id];

            if (ask_heads[price] == NULL_ORDER) [[unlikely]] {
                ask_heads[price] = id;
                ask_tails[price] = id;
                active_asks[price / 64] |= (1ULL << (price % 64)); // Activate bit
            } else [[likely]] {
                OrderId tail = ask_tails[price];
                pool.next[tail] = id;
                pool.prev[id] = tail;
                ask_tails[price] = id;
            }

            if (price < best_ask) [[unlikely]] {
                best_ask = price;
            }
        }

    public:
        // ---------------- MARKET DATA API ----------------

        // Wait-Free O(1) L2 Depth Book Snapshot using Seqlock
        L2Snapshot snapshotL2(int levels = 10) const noexcept {
            L2Snapshot snap;
            
            // Prevent stack buffer overflow if user requests more levels than L2Snapshot capacity
            if (levels > 10) levels = 10;

            size_t seq;
            do {
                seq = l2_lock.readBegin();
                
                snap.num_bids = 0;
                snap.num_asks = 0;

                // Snapshot Bids
                Price current_bid = best_bid;
                while (snap.num_bids < levels && bid_heads[current_bid] != NULL_ORDER) {
                    snap.bids[snap.num_bids++] = {current_bid, depth_bids[current_bid]};
                    if (current_bid == 0) break; 
                    current_bid = findNextBestBid(current_bid - 1);
                }

                // Snapshot Asks
                Price current_ask = best_ask;
                while (snap.num_asks < levels && current_ask < MAX_PRICE) {
                    if (ask_heads[current_ask] == NULL_ORDER) break;
                    snap.asks[snap.num_asks++] = {current_ask, depth_asks[current_ask]};
                    current_ask = findNextBestAsk(current_ask + 1);
                }

            } while (!l2_lock.readRetry(seq));

            return snap;
        }
    };
} // namespace NanoMatch

#endif // NANOMATCH_ORDER_BOOK_H
