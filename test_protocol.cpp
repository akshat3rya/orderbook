// Tests for the thing Day 3 is actually about: does readExact/writeExact
// correctly handle a message that arrives in pieces? A round-trip over a
// socketpair where everything shows up in one recv() wouldn't prove that —
// so test_fragmented_write below deliberately trickles bytes in from a
// background thread to force readExact to loop across multiple recv()s.
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <thread>
#include <chrono>
#include <unistd.h>

#include "protocol.hpp"

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
// 1. Every message struct should survive a plain sendMsg -> readExact
//    round trip byte-for-byte. Cheap regression test: if anyone changes a
//    struct's field order/types and breaks packing, this catches it.
// ---------------------------------------------------------------------
static void test_roundtrip_all_message_types() {
    int fds[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    NewOrderMsg newMsg{};
    newMsg.type = MsgType::NEW; newMsg.orderId = 42; newMsg.side = 1;
    newMsg.orderType = 0; newMsg.price = 12345; newMsg.qty = 10;
    CHECK(sendMsg(fds[0], newMsg));
    NewOrderMsg newOut{};
    CHECK(readExact(fds[1], &newOut, sizeof(newOut)));
    CHECK(std::memcmp(&newMsg, &newOut, sizeof(newMsg)) == 0);

    CancelMsg cancelMsg{}; cancelMsg.type = MsgType::CANCEL; cancelMsg.orderId = 7;
    CHECK(sendMsg(fds[0], cancelMsg));
    CancelMsg cancelOut{};
    CHECK(readExact(fds[1], &cancelOut, sizeof(cancelOut)));
    CHECK(std::memcmp(&cancelMsg, &cancelOut, sizeof(cancelMsg)) == 0);

    ModifyMsg modMsg{}; modMsg.type = MsgType::MODIFY; modMsg.orderId = 7; modMsg.newQty = 3;
    CHECK(sendMsg(fds[0], modMsg));
    ModifyMsg modOut{};
    CHECK(readExact(fds[1], &modOut, sizeof(modOut)));
    CHECK(std::memcmp(&modMsg, &modOut, sizeof(modMsg)) == 0);

    AckMsg ackMsg{}; ackMsg.type = MsgType::ACK; ackMsg.orderId = 7; ackMsg.numTrades = 2;
    CHECK(sendMsg(fds[0], ackMsg));
    AckMsg ackOut{};
    CHECK(readExact(fds[1], &ackOut, sizeof(ackOut)));
    CHECK(std::memcmp(&ackMsg, &ackOut, sizeof(ackMsg)) == 0);

    RejectMsg rejMsg{}; rejMsg.type = MsgType::REJECT; rejMsg.orderId = 99; rejMsg.reason = RejectReason::UNKNOWN_ORDER_ID;
    CHECK(sendMsg(fds[0], rejMsg));
    RejectMsg rejOut{};
    CHECK(readExact(fds[1], &rejOut, sizeof(rejOut)));
    CHECK(std::memcmp(&rejMsg, &rejOut, sizeof(rejMsg)) == 0);

    TradeMsg tradeMsg{}; tradeMsg.type = MsgType::TRADE; tradeMsg.makerOrderId = 1;
    tradeMsg.takerOrderId = 2; tradeMsg.price = 100; tradeMsg.qty = 5; tradeMsg.seq = 9;
    CHECK(sendMsg(fds[0], tradeMsg));
    TradeMsg tradeOut{};
    CHECK(readExact(fds[1], &tradeOut, sizeof(tradeOut)));
    CHECK(std::memcmp(&tradeMsg, &tradeOut, sizeof(tradeMsg)) == 0);

    BookUpdateMsg buMsg{}; buMsg.type = MsgType::BOOK_UPDATE; buMsg.side = 0; buMsg.price = 100; buMsg.qtyAtLevel = 40;
    CHECK(sendMsg(fds[0], buMsg));
    BookUpdateMsg buOut{};
    CHECK(readExact(fds[1], &buOut, sizeof(buOut)));
    CHECK(std::memcmp(&buMsg, &buOut, sizeof(buMsg)) == 0);

    close(fds[0]);
    close(fds[1]);
}

// ---------------------------------------------------------------------
// 2. The real Day 3 proof: a message trickled in one byte at a time from
//    another thread must still be reassembled correctly by a single
//    readExact call on the receiving end.
// ---------------------------------------------------------------------
static void test_fragmented_write_reassembled() {
    int fds[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    NewOrderMsg msg{};
    msg.type = MsgType::NEW; msg.orderId = 555; msg.side = 0;
    msg.orderType = 1; msg.price = 777; msg.qty = 88;

    std::thread writer([&]() {
        const char* p = reinterpret_cast<const char*>(&msg);
        for (size_t i = 0; i < sizeof(msg); ++i) {
            send(fds[0], p + i, 1, 0); // one byte per send() call
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    NewOrderMsg received{};
    bool ok = readExact(fds[1], &received, sizeof(received));
    writer.join();

    CHECK(ok);
    CHECK(std::memcmp(&msg, &received, sizeof(msg)) == 0);

    close(fds[0]);
    close(fds[1]);
}

// ---------------------------------------------------------------------
// 3. Clean EOF before any bytes, and a peer closing mid-message, must both
//    be reported as failure — never a false "success" with garbage data.
// ---------------------------------------------------------------------
static void test_eof_handling() {
    {
        int fds[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
        close(fds[0]); // close before sending anything
        char buf[8];
        CHECK(readExact(fds[1], buf, sizeof(buf)) == false);
        close(fds[1]);
    }
    {
        int fds[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
        char partial[4] = {1, 2, 3, 4};
        CHECK(send(fds[0], partial, sizeof(partial), 0) == 4);
        close(fds[0]); // closed after only 4 of the 8 expected bytes
        char buf[8];
        CHECK(readExact(fds[1], buf, sizeof(buf)) == false);
        close(fds[1]);
    }
}

#define RUN(fn) do { \
    std::cout << "RUN  " #fn "\n"; \
    int before = g_failures; \
    fn(); \
    if (g_failures == before) std::cout << "PASS " #fn "\n"; \
} while (0)

int main() {
    RUN(test_roundtrip_all_message_types);
    RUN(test_fragmented_write_reassembled);
    RUN(test_eof_handling);

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    if (g_failures > 0) {
        std::cout << g_failures << " FAILURE(S)\n";
        return 1;
    }
    std::cout << "ALL TESTS PASSED\n";
    return 0;
}
