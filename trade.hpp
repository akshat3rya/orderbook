#pragma once
#include <cstdint>

// One match between a resting (maker) order and an incoming (taker) order.
struct Trade {
    uint64_t makerOrderId;
    uint64_t takerOrderId;
    int64_t price;   // always the resting order's price (maker sets the price)
    int64_t qty;
    uint64_t seq;     // trade sequence number, for ordering/logging
};
