// Tests the thing Day 4 is actually about: can many producer threads push
// concurrently while a single consumer drains, with no items lost,
// duplicated, or corrupted? This is deliberately independent of real
// sockets/processes — that gives a fast, deterministic, non-flaky proof of
// the queue itself, separate from the end-to-end socket demo (client.cpp).
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <unordered_set>
#include <vector>

#include "command_queue.hpp"

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
// Many producers push concurrently while one consumer drains concurrently
// (not after the fact) — every pushed item must be popped exactly once.
// ---------------------------------------------------------------------
static void test_multi_producer_single_consumer_no_loss() {
    CommandQueue queue;
    constexpr int kProducers = 8;
    constexpr int kPerProducer = 500;
    constexpr int kTotal = kProducers * kPerProducer;

    std::vector<uint64_t> received;
    received.reserve(kTotal);

    // Single consumer thread — the only thing that ever touches `received`,
    // so no lock is needed around it.
    std::thread consumer([&]() {
        Command cmd;
        while (queue.pop(cmd)) {
            received.push_back(cmd.cancelMsg.orderId);
        }
    });

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&queue, p]() {
            for (int i = 0; i < kPerProducer; ++i) {
                Command cmd;
                cmd.type = CommandType::CANCEL;
                cmd.clientFd = -1;
                cmd.sessionId = static_cast<uint64_t>(p);
                cmd.cancelMsg.type = MsgType::CANCEL;
                // Marker encodes (producer, index) so we can verify every
                // single item pushed was popped exactly once, not just that
                // the count matches.
                cmd.cancelMsg.orderId = static_cast<uint64_t>(p) * 1000000ULL + static_cast<uint64_t>(i);
                queue.push(std::move(cmd));
            }
        });
    }
    for (auto& t : producers) t.join();

    queue.shutdown(); // no more work coming — let the consumer finish and exit
    consumer.join();

    CHECK(received.size() == static_cast<size_t>(kTotal));

    std::unordered_set<uint64_t> uniq(received.begin(), received.end());
    CHECK(uniq.size() == static_cast<size_t>(kTotal)); // no duplicates, nothing double-delivered

    bool allExpectedPresent = true;
    for (int p = 0; p < kProducers && allExpectedPresent; ++p) {
        for (int i = 0; i < kPerProducer; ++i) {
            uint64_t marker = static_cast<uint64_t>(p) * 1000000ULL + static_cast<uint64_t>(i);
            if (uniq.find(marker) == uniq.end()) { allExpectedPresent = false; break; }
        }
    }
    CHECK(allExpectedPresent); // nothing lost
}

// ---------------------------------------------------------------------
// A consumer blocked waiting on an empty queue must wake up and return
// false once shutdown() is called — not hang forever.
// ---------------------------------------------------------------------
static void test_shutdown_wakes_blocked_consumer() {
    CommandQueue queue;
    std::atomic<bool> consumerReturned{false};
    bool popResult = true;

    std::thread consumer([&]() {
        Command cmd;
        popResult = queue.pop(cmd); // queue is empty, so this blocks until shutdown()
        consumerReturned = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // let the consumer reach the blocking wait
    queue.shutdown();
    consumer.join();

    CHECK(consumerReturned.load());
    CHECK(popResult == false); // shutdown with nothing queued -> pop reports "no more work"
}

// ---------------------------------------------------------------------
// shutdown() must not discard work that was already queued before it was
// called — the consumer should drain everything first, then stop.
// ---------------------------------------------------------------------
static void test_shutdown_does_not_drop_queued_work() {
    CommandQueue queue;
    for (int i = 0; i < 10; ++i) {
        Command cmd;
        cmd.type = CommandType::CANCEL;
        cmd.cancelMsg.type = MsgType::CANCEL;
        cmd.cancelMsg.orderId = static_cast<uint64_t>(i);
        queue.push(std::move(cmd));
    }
    queue.shutdown(); // called BEFORE anything has been popped

    int drained = 0;
    Command cmd;
    while (queue.pop(cmd)) ++drained;

    CHECK(drained == 10); // all 10 pre-shutdown items still delivered
}

#define RUN(fn) do { \
    std::cout << "RUN  " #fn "\n"; \
    int before = g_failures; \
    fn(); \
    if (g_failures == before) std::cout << "PASS " #fn "\n"; \
} while (0)

int main() {
    RUN(test_multi_producer_single_consumer_no_loss);
    RUN(test_shutdown_wakes_blocked_consumer);
    RUN(test_shutdown_does_not_drop_queued_work);

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    if (g_failures > 0) {
        std::cout << g_failures << " FAILURE(S)\n";
        return 1;
    }
    std::cout << "ALL TESTS PASSED\n";
    return 0;
}
