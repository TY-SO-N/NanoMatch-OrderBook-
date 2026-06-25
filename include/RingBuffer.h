#ifndef NANOMATCH_RING_BUFFER_H
#define NANOMATCH_RING_BUFFER_H

#include <atomic>
#include <vector>
#include <cstddef>
#include <cassert>

namespace NanoMatch {

// SPSC (Single Producer, Single Consumer) Lock-Free Ring Buffer
template<typename T, size_t Capacity>
class RingBuffer {
private:
    // alignas(64) prevents False Sharing by placing head and tail on different hardware cache lines
    alignas(64) std::atomic<size_t> head_{0}; // Written by Consumer (Matching Engine)
    alignas(64) std::atomic<size_t> tail_{0}; // Written by Producer (Network Thread)
    
    // The pre-allocated data buffer
    std::vector<T> buffer_;

public:
    RingBuffer() : buffer_(Capacity) {
        // Capacity must be a power of 2 for fast bitwise modulo
        assert((Capacity & (Capacity - 1)) == 0 && "Capacity must be a power of 2");
    }

    // Called by the Producer (Network Thread)
    bool push(const T& item) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & (Capacity - 1);

        // If the queue is full (tail caught up to head), drop or reject
        // We use memory_order_acquire to ensure we see the consumer's latest head
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; 
        }

        buffer_[current_tail] = item;
        
        // memory_order_release guarantees the data write finishes BEFORE the tail updates
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Called by the Consumer (Matching Engine Thread)
    bool pop(T& item) {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        // If queue is empty
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        item = buffer_[current_head];

        // memory_order_release guarantees the data read finishes BEFORE the head updates
        head_.store((current_head + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }
};

}

#endif // NANOMATCH_RING_BUFFER_H
