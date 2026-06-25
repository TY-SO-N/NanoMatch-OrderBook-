#ifndef NANOMATCH_TYPES_H
#define NANOMATCH_TYPES_H

#include <cstdint>
#include <limits>

namespace NanoMatch {

    // Using 64-bit integer for fixed-point math to avoid IEEE-754 floats
    using Price = uint64_t;
    using Quantity = uint64_t;
    using OrderId = uint64_t;

    enum class Side : uint8_t {
        Buy = 0,
        Sell = 1
    };

    enum class OrderType : uint8_t {
        Limit = 0,
        Market = 1,
        IOC = 2,    // Immediate or Cancel
        FOK = 3     // Fill or Kill
    };

    // Constant for an invalid price/quantity
    constexpr Price INVALID_PRICE = std::numeric_limits<Price>::max();
    constexpr Quantity INVALID_QUANTITY = 0;

    // Special OrderId constants
    constexpr OrderId NULL_ORDER = 0;
    constexpr OrderId EXECUTED_ORDER = std::numeric_limits<OrderId>::max();
}

#endif // NANOMATCH_TYPES_H
