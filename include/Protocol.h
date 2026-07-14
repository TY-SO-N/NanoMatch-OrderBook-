#ifndef NANOMATCH_PROTOCOL_H
#define NANOMATCH_PROTOCOL_H

#include <cstdint>

namespace NanoMatch {

// Strict binary alignment for the network protocol
#pragma pack(push, 1)

// ClientMessage is sent from the trading bot to the exchange (Order Entry)
struct ClientMessage {
    uint8_t type;       // 1 = Add Limit Order, 4 = Cancel Order
    uint8_t side;       // 0 = Buy, 1 = Sell
    uint16_t instrument_id; // Maps to stock ticker (e.g., 0=AAPL, 1=MSFT), perfectly preserves 16-byte alignment
    uint32_t qty;       // 4 bytes (Unused for cancel)
    union {
        uint64_t price;     // 8 bytes (Used if type == 1)
        uint64_t order_id;  // 8 bytes (Used if type == 4)
    };
}; // Total: 16 bytes exactly

// ServerMessage is sent from the exchange back to the client (Execution Report)
struct ServerMessage {
    uint8_t type;       // 2 = Order Accepted, 3 = Trade Executed
    uint8_t side;
    uint16_t padding;
    uint32_t matched_qty;
    uint64_t match_price;
    uint64_t order_id;
}; // Total: 24 bytes

#pragma pack(pop)

}

#endif // NANOMATCH_PROTOCOL_H
