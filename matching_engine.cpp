#include "matching_engine.hpp"
#include <algorithm>

MatchResult MatchingEngine::submit(uint64_t id, Side side, OrderType type, int64_t price, int64_t qty) {
    MatchResult result;

    if (qty <= 0 || book_.contains(id)) {
        return result; // accepted stays false: reject (bad qty or duplicate id)
    }
    result.accepted = true;

    int64_t remaining = qty;
    if (side == Side::BUY) matchBuy(id, type, price, remaining, result.trades);
    else                    matchSell(id, type, price, remaining, result.trades);

    result.filledQty = qty - remaining;
    result.remainingQty = remaining;

    // Only LIMIT orders add liquidity. A MARKET order's unfilled remainder
    // (ran out of book to hit) is simply dropped — that's the "empty-book /
    // insufficient-liquidity" edge case.
    if (remaining > 0 && type == OrderType::LIMIT) {
        book_.insert(id, side, price, remaining);
        result.restedOnBook = true;
    }

    return result;
}

void MatchingEngine::matchBuy(uint64_t takerId, OrderType type, int64_t price,
                               int64_t& remaining, std::vector<Trade>& trades) {
    auto& asks = book_.asks(); // ascending price: best (lowest) ask first
    while (remaining > 0) {
        auto it = asks.begin();
        if (it == asks.end()) break;                              // empty-book edge case
        if (type == OrderType::LIMIT && it->first > price) break;  // no more crossing levels

        PriceLevel* level = it->second;
        Order* resting = level->head; // oldest order at this price = time priority

        int64_t fillQty = std::min(remaining, resting->qty);
        trades.push_back(Trade{resting->id, takerId, resting->price, fillQty, tradeSeq_++});

        remaining -= fillQty;
        book_.applyFill(resting, fillQty); // shrinks/removes resting order, cleans up empty level
        // Loop re-reads asks.begin() next iteration — correctly reflects
        // level removal or the next order now at the front of the level.
    }
}

void MatchingEngine::matchSell(uint64_t takerId, OrderType type, int64_t price,
                                int64_t& remaining, std::vector<Trade>& trades) {
    auto& bids = book_.bids(); // descending price: best (highest) bid first
    while (remaining > 0) {
        auto it = bids.begin();
        if (it == bids.end()) break;
        if (type == OrderType::LIMIT && it->first < price) break;

        PriceLevel* level = it->second;
        Order* resting = level->head;

        int64_t fillQty = std::min(remaining, resting->qty);
        trades.push_back(Trade{resting->id, takerId, resting->price, fillQty, tradeSeq_++});

        remaining -= fillQty;
        book_.applyFill(resting, fillQty);
    }
}
