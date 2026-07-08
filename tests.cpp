// Minimal hand-rolled test runner — no external deps (no gtest), matches
// the "keep it simple" scope. Add a test by writing a void test_xxx() that
// uses CHECK(), then register it in main()'s RUN() list.
#include <iostream>
#include <string>
#include "order_book.hpp"
#include "matching_engine.hpp"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond) do { \
    ++g_checks; \
    if (!(cond)) { \
        ++g_failures; \
        std::cerr << "  FAIL: " << #cond << "  (" << __FILE__ << ":" << __LINE__ << ")\n"; \
    } \
} while (0)

// ---------------------------------------------------------------------
// 1. Partial fills: a resting order bigger than the incoming order should
//    fill exactly the incoming qty and leave the rest resting.
// ---------------------------------------------------------------------
static void test_partial_fill() {
    OrderBook book;
    MatchingEngine engine(book);

    engine.submit(1, Side::SELL, OrderType::LIMIT, 100, 30); // resting ask: 100 x 30

    auto r = engine.submit(2, Side::BUY, OrderType::LIMIT, 100, 50); // buy 50 @ 100

    CHECK(r.accepted);
    CHECK(r.trades.size() == 1);
    CHECK(r.trades[0].makerOrderId == 1);
    CHECK(r.trades[0].takerOrderId == 2);
    CHECK(r.trades[0].qty == 30);
    CHECK(r.filledQty == 30);
    CHECK(r.remainingQty == 20);
    CHECK(r.restedOnBook == true);

    CHECK(!book.contains(1));      // maker fully consumed, removed from book
    CHECK(book.contains(2));       // taker's leftover 20 now resting
    CHECK(book.bestAsk() == std::nullopt); // ask side now empty
    CHECK(book.bestBid().value() == 100);
}

// ---------------------------------------------------------------------
// 2. Cancel after a partial fill: a resting order that survives a partial
//    match (qty reduced in place) must still be cancellable cleanly.
// ---------------------------------------------------------------------
static void test_cancel_after_partial_fill() {
    OrderBook book;
    MatchingEngine engine(book);

    engine.submit(1, Side::SELL, OrderType::LIMIT, 100, 50); // resting ask: 100 x 50
    auto r = engine.submit(2, Side::BUY, OrderType::MARKET, 0, 20); // hits 20 of it

    CHECK(r.filledQty == 20);
    CHECK(book.contains(1));
    // order 1 should now show 30 remaining — check via the book dump path
    // indirectly: cancel must succeed and leave the book empty afterward.
    CHECK(book.bestAsk().value() == 100);

    bool cancelled = book.cancel(1);
    CHECK(cancelled);
    CHECK(!book.contains(1));
    CHECK(book.bestAsk() == std::nullopt); // level cleaned up, no dangling entry
    CHECK(book.liveOrderCount() == 0);
}

// ---------------------------------------------------------------------
// 3. Modify keeps priority: an in-place qty decrease must NOT push the
//    order to the back of its price level's time-priority queue.
// ---------------------------------------------------------------------
static void test_modify_keeps_priority() {
    OrderBook book;
    MatchingEngine engine(book);

    book.insert(1, Side::SELL, 100, 50); // first in queue at 100
    book.insert(2, Side::SELL, 100, 50); // second in queue at 100

    auto modResult = book.modify(1, 10); // shrink order 1 down to 10
    CHECK(modResult == ModifyResult::OK);

    // Buy enough to eat order 1 entirely and dip into order 2. If modify
    // had lost priority (e.g. cancel+reinsert), order 2 would fill first.
    auto r = engine.submit(3, Side::BUY, OrderType::LIMIT, 100, 15);

    CHECK(r.trades.size() == 2);
    CHECK(r.trades[0].makerOrderId == 1);
    CHECK(r.trades[0].qty == 10);
    CHECK(r.trades[1].makerOrderId == 2);
    CHECK(r.trades[1].qty == 5);
    CHECK(!book.contains(1));  // order 1 fully consumed
    CHECK(book.contains(2));   // order 2 partially filled, 45 left resting
}

// ---------------------------------------------------------------------
// 4. Multi-level sweep: a large incoming order should walk multiple price
//    levels in strict price order, best price first.
// ---------------------------------------------------------------------
static void test_multi_level_sweep() {
    OrderBook book;
    MatchingEngine engine(book);

    engine.submit(1, Side::SELL, OrderType::LIMIT, 101, 10);
    engine.submit(2, Side::SELL, OrderType::LIMIT, 102, 10);
    engine.submit(3, Side::SELL, OrderType::LIMIT, 103, 10);

    // Buy 25 with a limit high enough to cross all three levels.
    auto r = engine.submit(4, Side::BUY, OrderType::LIMIT, 103, 25);

    CHECK(r.trades.size() == 3);
    CHECK(r.trades[0].price == 101 && r.trades[0].qty == 10);
    CHECK(r.trades[1].price == 102 && r.trades[1].qty == 10);
    CHECK(r.trades[2].price == 103 && r.trades[2].qty == 5);
    CHECK(r.filledQty == 25);
    CHECK(r.remainingQty == 0);
    CHECK(r.restedOnBook == false); // fully filled, nothing left to rest

    CHECK(!book.contains(1));
    CHECK(!book.contains(2));
    CHECK(book.contains(3));        // level 103 has 5 left resting
    CHECK(book.bestAsk().value() == 103);
}

// ---------------------------------------------------------------------
// 5. Empty-book edge cases: matching against nothing shouldn't crash and
//    should behave sensibly for both order types.
// ---------------------------------------------------------------------
static void test_empty_book_edge_cases() {
    OrderBook book;
    MatchingEngine engine(book);

    // Market order into an empty book: no liquidity, nothing to rest.
    auto r1 = engine.submit(1, Side::BUY, OrderType::MARKET, 0, 100);
    CHECK(r1.accepted);
    CHECK(r1.trades.empty());
    CHECK(r1.filledQty == 0);
    CHECK(r1.remainingQty == 100);
    CHECK(r1.restedOnBook == false); // market orders never rest, even unfilled
    CHECK(book.liveOrderCount() == 0);

    // Limit order into an empty book: nothing to match, all of it rests.
    auto r2 = engine.submit(2, Side::SELL, OrderType::LIMIT, 100, 40);
    CHECK(r2.trades.empty());
    CHECK(r2.filledQty == 0);
    CHECK(r2.remainingQty == 40);
    CHECK(r2.restedOnBook == true);
    CHECK(book.contains(2));

    // Duplicate id should be rejected outright, not silently matched.
    auto r3 = engine.submit(2, Side::BUY, OrderType::LIMIT, 100, 10);
    CHECK(r3.accepted == false);
    CHECK(r3.trades.empty());
}

// ---------------------------------------------------------------------
#define RUN(fn) do { \
    std::cout << "RUN  " #fn "\n"; \
    int before = g_failures; \
    fn(); \
    if (g_failures == before) std::cout << "PASS " #fn "\n"; \
} while (0)

int main() {
    RUN(test_partial_fill);
    RUN(test_cancel_after_partial_fill);
    RUN(test_modify_keeps_priority);
    RUN(test_multi_level_sweep);
    RUN(test_empty_book_edge_cases);

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    if (g_failures > 0) {
        std::cout << g_failures << " FAILURE(S)\n";
        return 1;
    }
    std::cout << "ALL TESTS PASSED\n";
    return 0;
}
