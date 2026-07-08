#pragma once
#include <cstdint>

enum class Side : uint8_t { BUY, SELL };

struct PriceLevel; // fwd decl

// An Order is also the intrusive DLL node for its PriceLevel.
// No separate node allocation — prev/next live right on the order.
struct Order {
    uint64_t id;
    Side side;
    int64_t price;      // integer ticks, not float — avoids fp price bugs
    int64_t qty;         // remaining (live) quantity
    uint64_t seq;         // insertion sequence, used for time priority
    PriceLevel* level;    // owning level, for O(1) unlink on cancel

    Order* prev = nullptr;
    Order* next = nullptr;
};
