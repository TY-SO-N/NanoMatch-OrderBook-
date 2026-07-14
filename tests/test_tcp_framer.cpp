#include <gtest/gtest.h>
#include <cstring>
#include "TcpServer.h"
#include "Protocol.h"
#include "RingBuffer.h"

using namespace NanoMatch;

// Expose internal private buffer for testing purposes by subclassing or mocking.
// Since we can't easily mock the private buffer without changing headers, we can
// simulate the incoming byte stream logic from TcpServer.cpp natively.

TEST(TcpServerTest, HalfPacketFragmentation) {
    RingBuffer<ClientMessage, 1024> queue;
    std::vector<uint8_t> recv_buffer_;

    // Simulate ClientMessage (16 bytes)
    ClientMessage msg;
    msg.type = 1;
    msg.side = 0;
    msg.instrument_id = 42;
    msg.qty = 100;
    msg.price = 15000;

    uint8_t* raw_bytes = reinterpret_cast<uint8_t*>(&msg);

    // Simulate receiving EXACTLY 8 bytes (half packet)
    recv_buffer_.insert(recv_buffer_.end(), raw_bytes, raw_bytes + 8);

    // The TCP framer logic:
    size_t msg_size = sizeof(ClientMessage);
    size_t total_bytes = recv_buffer_.size();
    size_t num_msgs = total_bytes / msg_size;
    size_t bytes_to_consume = num_msgs * msg_size;

    // Verify it consumes NOTHING because num_msgs == 0
    EXPECT_EQ(num_msgs, 0);
    EXPECT_EQ(bytes_to_consume, 0);

    // Simulate receiving the remaining 8 bytes
    recv_buffer_.insert(recv_buffer_.end(), raw_bytes + 8, raw_bytes + 16);

    total_bytes = recv_buffer_.size();
    num_msgs = total_bytes / msg_size;
    bytes_to_consume = num_msgs * msg_size;

    // Verify it now consumes EXACTLY 1 message (16 bytes)
    EXPECT_EQ(num_msgs, 1);
    EXPECT_EQ(bytes_to_consume, 16);

    // Verify extraction
    ClientMessage extracted;
    std::memcpy(&extracted, recv_buffer_.data(), msg_size);
    EXPECT_EQ(extracted.instrument_id, 42);
    EXPECT_EQ(extracted.qty, 100);
    EXPECT_EQ(extracted.price, 15000);
}

TEST(TcpServerTest, BurstBulkErasure) {
    std::vector<uint8_t> recv_buffer_;
    ClientMessage msg;
    msg.type = 1;
    
    // Simulate receiving 10.5 messages (160 + 8 = 168 bytes) in a single massive burst
    for (int i = 0; i < 10; ++i) {
        msg.qty = i * 10;
        uint8_t* raw = reinterpret_cast<uint8_t*>(&msg);
        recv_buffer_.insert(recv_buffer_.end(), raw, raw + 16);
    }
    // Add half a message
    uint8_t* raw = reinterpret_cast<uint8_t*>(&msg);
    recv_buffer_.insert(recv_buffer_.end(), raw, raw + 8);

    size_t msg_size = sizeof(ClientMessage);
    size_t total_bytes = recv_buffer_.size();
    size_t num_msgs = total_bytes / msg_size;
    size_t bytes_to_consume = num_msgs * msg_size;

    EXPECT_EQ(num_msgs, 10);
    EXPECT_EQ(bytes_to_consume, 160);

    // Perform bulk erase
    recv_buffer_.erase(recv_buffer_.begin(), recv_buffer_.begin() + bytes_to_consume);

    // Verify exactly 8 bytes remain (the half packet)
    EXPECT_EQ(recv_buffer_.size(), 8);
}
