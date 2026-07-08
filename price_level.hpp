#pragma once
#include <cstdint>
#include "order.hpp"

// One price level = one FIFO queue of orders (time priority within price).
struct PriceLevel {
    int64_t price;
    int64_t totalQty = 0;   // sum of live order qty at this level, O(1) to read
    int64_t orderCount = 0;
    Order* head = nullptr;   // oldest order (next to match)
    Order* tail = nullptr;   // newest order (append here)

    explicit PriceLevel(int64_t p) : price(p) {}

    void pushBack(Order* o) {
        o->level = this;
        o->prev = tail;
        o->next = nullptr;
        if (tail) tail->next = o;
        tail = o;
        if (!head) head = o;
        totalQty += o->qty;
        ++orderCount;
    }

    // O(1) removal given the node pointer — the whole point of the intrusive DLL.
    void unlink(Order* o) {
        if (o->prev) o->prev->next = o->next;
        else head = o->next;
        if (o->next) o->next->prev = o->prev;
        else tail = o->prev;
        o->prev = o->next = nullptr;
        totalQty -= o->qty;
        --orderCount;
    }

    bool empty() const { return head == nullptr; }
};
