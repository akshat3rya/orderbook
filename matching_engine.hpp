#pragma once
#include <cstdint>
#include <vector>
#include "order_book.hpp"
#include "trade.hpp"

enum class OrderType : uint8_t { LIMIT, MARKET };

struct MatchResult {
    bool accepted = false;      // false only on duplicate id or qty <= 0 — a reject, not a fill outcome
    std::vector<Trade> trades;
    int64_t filledQty = 0;
    int64_t remainingQty = 0;
    bool restedOnBook = false;  // true iff unfilled remainder was added to the book (limit orders only)
};

// Sits on top of an OrderBook and implements price-time priority matching.
// The book itself has no matching logic — it's a pure data structure. This
// class is what turns "insert into a map" into "an exchange".
class MatchingEngine {
public:
    explicit MatchingEngine(OrderBook& book) : book_(book) {}

    // Submit a new incoming order. Walks the opposite side of the book,
    // generating trades while price crosses (always, for MARKET orders).
    // Any unfilled remainder: rests on the book if LIMIT, is dropped if
    // MARKET (market orders never add liquidity).
    MatchResult submit(uint64_t id, Side side, OrderType type, int64_t price, int64_t qty);

private:
    OrderBook& book_;
    uint64_t tradeSeq_ = 0;

    void matchBuy(uint64_t takerId, OrderType type, int64_t price, int64_t& remaining, std::vector<Trade>& trades);
    void matchSell(uint64_t takerId, OrderType type, int64_t price, int64_t& remaining, std::vector<Trade>& trades);
};