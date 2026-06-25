#ifndef NANOMATCH_SEQLOCK_H
#define NANOMATCH_SEQLOCK_H

#include <atomic>
#include <thread>

namespace NanoMatch {

    /**
     * Wait-Free Sequence Lock (Seqlock)
     * 
     * Allows a single Writer (the matching engine) to write data without ever blocking.
     * Readers read the sequence counter before and after reading the data. 
     * If the sequence counter is odd, or if it changed during the read, the reader 
     * discards the data and tries again.
     * 
     * Memory ordering guarantees:
     * - Writer uses memory_order_release to ensure all previous memory writes are visible.
     * - Reader uses memory_order_acquire to ensure reads are not reordered before the sequence check.
     */
    class Seqlock {
    private:
        // Hardware cache-line aligned to prevent false sharing with the protected data
        alignas(64) std::atomic<size_t> sequence{0};

    public:
        // ---------------- WRITER API ----------------

        // Call before modifying the protected data
        inline void writeLock() noexcept {
            // Increment to an odd number.
            // memory_order_release ensures any subsequent writes to the data
            // cannot be reordered BEFORE this sequence increment.
            sequence.fetch_add(1, std::memory_order_release);
        }

        // Call after modifying the protected data
        inline void writeUnlock() noexcept {
            // Increment to an even number.
            // memory_order_release ensures all writes to the data
            // are visible BEFORE this sequence increment is published.
            sequence.fetch_add(1, std::memory_order_release);
        }

        // ---------------- READER API ----------------

        // Call before reading the protected data
        inline size_t readBegin() const noexcept {
            size_t seq;
            while (true) {
                // memory_order_acquire ensures subsequent reads from the data
                // cannot be reordered BEFORE reading the sequence.
                seq = sequence.load(std::memory_order_acquire);
                
                // If sequence is even, no writer is currently active
                if ((seq & 1) == 0) {
                    break;
                }
                // Writer is active, spin-wait
                std::this_thread::yield(); 
            }
            return seq;
        }

        // Call after reading the protected data
        // Returns true if the read data is valid (no writer interfered).
        // If false, the reader MUST discard the data and call readBegin() again.
        inline bool readRetry(size_t seq_begin) const noexcept {
            // memory_order_acquire ensures previous reads from the data
            // cannot be reordered AFTER this sequence check.
            // CPU hardware fence prevents read reordering.
            std::atomic_thread_fence(std::memory_order_acquire);
            
            // Return true if the sequence hasn't changed.
            return sequence.load(std::memory_order_relaxed) == seq_begin;
        }
    };

} // namespace NanoMatch

#endif // NANOMATCH_SEQLOCK_H
