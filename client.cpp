#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "protocol.hpp"

static bool login(int fd, const std::string& token) {
    LoginMsg m{};
    m.type = MsgType::LOGIN;
    std::strncpy(m.token, token.c_str(), sizeof(m.token) - 1);
    sendMsg(fd, m);

    uint8_t typeByte;
    if (!readExact(fd, &typeByte, sizeof(typeByte))) {
        std::cout << "  <connection closed during login>\n";
        return false;
    }
    MsgType type = static_cast<MsgType>(typeByte);
    if (type == MsgType::ACK) {
        AckMsg ack{}; ack.type = type;
        if (!readExact(fd, reinterpret_cast<char*>(&ack) + sizeof(MsgType), sizeof(AckMsg) - sizeof(MsgType))) return false;
        std::cout << "LOGIN  ok, sessionId=" << ack.orderId << "\n\n";
        return true;
    } else if (type == MsgType::REJECT) {
        RejectMsg rej{}; rej.type = type;
        if (!readExact(fd, reinterpret_cast<char*>(&rej) + sizeof(MsgType), sizeof(RejectMsg) - sizeof(MsgType))) return false;
        std::cout << "LOGIN  REJECTED, reason=" << static_cast<int>(rej.reason) << "\n";
        return false;
    }
    std::cout << "LOGIN  <unexpected response type " << static_cast<int>(typeByte) << ">\n";
    return false;
}

static bool recvAckOrReject(int fd, uint32_t* numTradesOut = nullptr) {
    uint8_t typeByte;
    if (!readExact(fd, &typeByte, sizeof(typeByte))) {
        std::cout << "  <connection closed while waiting for response>\n";
        return false;
    }
    MsgType type = static_cast<MsgType>(typeByte);

    if (type == MsgType::ACK) {
        AckMsg m{}; m.type = type;
        if (!readExact(fd, reinterpret_cast<char*>(&m) + sizeof(MsgType), sizeof(AckMsg) - sizeof(MsgType))) return false;
        std::cout << "  ACK    orderId=" << m.orderId << " numTrades=" << m.numTrades << "\n";
        if (numTradesOut) *numTradesOut = m.numTrades;
        return true;
    } else if (type == MsgType::REJECT) {
        RejectMsg m{}; m.type = type;
        if (!readExact(fd, reinterpret_cast<char*>(&m) + sizeof(MsgType), sizeof(RejectMsg) - sizeof(MsgType))) return false;
        std::cout << "  REJECT orderId=" << m.orderId << " reason=" << static_cast<int>(m.reason) << "\n";
        if (numTradesOut) *numTradesOut = 0;
        return true;
    }
    std::cout << "  <unexpected message type " << static_cast<int>(typeByte) << ">\n";
    return false;
}

static void recvTrades(int fd, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t typeByte;
        if (!readExact(fd, &typeByte, sizeof(typeByte))) { std::cout << "  <connection closed mid-trade-burst>\n"; return; }
        TradeMsg m{}; m.type = static_cast<MsgType>(typeByte);
        if (!readExact(fd, reinterpret_cast<char*>(&m) + sizeof(MsgType), sizeof(TradeMsg) - sizeof(MsgType))) return;
        std::cout << "  TRADE  maker=" << m.makerOrderId << " taker=" << m.takerOrderId
                   << " price=" << m.price << " qty=" << m.qty << " seq=" << m.seq << "\n";
    }
}

static void sendNew(int fd, uint64_t id, uint8_t side, uint8_t orderType, int64_t price, int64_t qty, const char* label) {
    std::cout << "> NEW " << label << " id=" << id << "\n";
    NewOrderMsg m{};
    m.type = MsgType::NEW; m.orderId = id; m.side = side; m.orderType = orderType; m.price = price; m.qty = qty;
    sendMsg(fd, m);
    uint32_t numTrades = 0;
    if (recvAckOrReject(fd, &numTrades)) recvTrades(fd, numTrades);
}

static void sendCancel(int fd, uint64_t id) {
    std::cout << "> CANCEL id=" << id << "\n";
    CancelMsg m{}; m.type = MsgType::CANCEL; m.orderId = id;
    sendMsg(fd, m);
    recvAckOrReject(fd);
}

static void sendModify(int fd, uint64_t id, int64_t newQty) {
    std::cout << "> MODIFY id=" << id << " newQty=" << newQty << "\n";
    ModifyMsg m{}; m.type = MsgType::MODIFY; m.orderId = id; m.newQty = newQty;
    sendMsg(fd, m);
    recvAckOrReject(fd);
}

int main(int argc, char** argv) {
    int port = 5555;
    if (argc > 1) port = std::atoi(argv[1]);
    std::string token = argc > 2 ? argv[2] : "trader1-secret";
    uint64_t idOffset = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 0;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)); // don't batch our tiny messages, we want to see real framing

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("connect"); return 1;
    }
    std::cout << "connected to server on port " << port << "\n";

    if (!login(fd, token)) {
        close(fd);
        return 1;
    }

    // Scenario: build a two-level ask book, sweep it with a crossing buy,
    // exercise cancel/modify/duplicate-id/unknown-id error paths, then
    // finish with a market order to prove that path too. idOffset lets
    // two instances of this client run concurrently against the same
    // server without their order ids colliding.
    auto id = [idOffset](uint64_t n) { return idOffset + n; };

    sendNew(fd, id(1), /*SELL*/1, /*LIMIT*/0, 100, 30, "sell 100x30");
    sendNew(fd, id(2), /*SELL*/1, /*LIMIT*/0, 101, 20, "sell 101x20");
    sendNew(fd, id(3), /*BUY*/0,  /*LIMIT*/0, 101, 40, "buy 101x40 (should sweep both levels)");

    sendCancel(fd, id(99)); // doesn't exist -> REJECT

    sendModify(fd, id(2), 5); // order 2 had 10 left after the sweep -> decrease to 5, OK

    sendNew(fd, id(4), /*SELL*/1, /*LIMIT*/0, 200, 10, "sell 200x10");
    sendNew(fd, id(4), /*SELL*/1, /*LIMIT*/0, 200, 5, "duplicate id (should REJECT)");

    sendCancel(fd, id(2)); // should succeed
    sendCancel(fd, id(2)); // already gone -> REJECT

    sendNew(fd, id(5), /*BUY*/0, /*MARKET*/1, 0, 100, "market buy 100 (should hit resting id=4)");

    close(fd);
    std::cout << "\ndone\n";
    return 0;
}
