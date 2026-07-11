#pragma once
#include <cstdint>
#include <map>
#include <unordered_map>
#include <functional>
#include <optional>
#include "order.hpp"
#include "price_level.hpp"

enum class ModifyResult { OK, NOT_FOUND, QTY_MUST_DECREASE, RESULTED_IN_CANCEL };

// Fires whenever a price level's total resting quantity changes for any
// reason (insert, cancel, modify, or a fill) — newTotalQty == 0 means the
// level was fully removed. This is the single source of truth for "book
// delta" market data events (Day 5): only OrderBook knows, at the exact
// moment of mutation, whether a level survived a partial consumption or
// was wiped out entirely — that can't be reliably reconstructed from
// outside afterward (the PriceLevel may already be deleted).
using LevelChangeListener = std::function<void(Side side, int64_t price, int64_t newTotalQty)>;

class OrderBook {
public:
    ~OrderBook();

    void setLevelChangeListener(LevelChangeListener listener) { levelChangeListener_ = std::move(listener); }

    // Insert a brand new resting order. Returns false if id already exists.
    bool insert(uint64_t id, Side side, int64_t price, int64_t qty);

    // Cancel a live order by id. Returns false if not found.
    bool cancel(uint64_t id);

    // In-place quantity DECREASE only (increasing qty must re-queue in a
    // real exchange, so that's modeled as cancel+insert, not this call).
    // newQty <= 0 cancels the order outright.
    ModifyResult modify(uint64_t id, int64_t newQty);

    // Fill part or all of a resting order in place (used by the matching
    // engine while walking the book). If the fill exhausts the order it is
    // fully removed (unlinked, erased from the index, level cleaned up if
    // now empty) — same cleanup path as cancel(). `order` must be a live
    // pointer currently owned by this book (e.g. from level->head while
    // walking bids()/asks()).
    void applyFill(Order* order, int64_t fillQty);

    bool contains(uint64_t id) const { return orderIndex_.find(id) != orderIndex_.end(); }
    size_t liveOrderCount() const { return orderIndex_.size(); }

    // Best bid / ask price, if book side is non-empty.
    std::optional<int64_t> bestBid() const;
    std::optional<int64_t> bestAsk() const;

    // Debug/CLI dump of the book, top `depth` levels each side.
    void print(int depth = 10) const;

    // Access for the matching engine (Day 2) — walks levels in priority order.
    std::map<int64_t, PriceLevel*, std::greater<int64_t>>& bids() { return bids_; }
    std::map<int64_t, PriceLevel*, std::less<int64_t>>& asks() { return asks_; }

private:
    // Bids: highest price first. Asks: lowest price first.
    std::map<int64_t, PriceLevel*, std::greater<int64_t>> bids_;
    std::map<int64_t, PriceLevel*, std::less<int64_t>> asks_;
    std::unordered_map<uint64_t, Order*> orderIndex_;
    uint64_t seqCounter_ = 0;
    LevelChangeListener levelChangeListener_ = nullptr;

    PriceLevel* getOrCreateLevel(Side side, int64_t price);
    void removeLevelIfEmpty(Side side, PriceLevel* level);
    void notifyLevelChanged(Side side, int64_t price, int64_t newTotalQty) {
        if (levelChangeListener_) levelChangeListener_(side, price, newTotalQty);
    }
};
