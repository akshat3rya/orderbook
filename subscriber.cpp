#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "protocol.hpp"

static const char* sideStr(uint8_t s) { return s == 0 ? "BUY" : "SELL"; }
static const char* typeStr(uint8_t t) { return t == 0 ? "LIMIT" : "MARKET"; }

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 5555;
    std::string token = argc > 2 ? argv[2] : "trader1-secret";
    int maxMessages = argc > 3 ? std::atoi(argv[3]) : 20; // bounds the demo so it terminates on its own

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("connect"); return 1;
    }
    std::cout << "connected to server on port " << port << "\n";

    // LOGIN (mandatory on any connection, trading or market-data)
    LoginMsg login{};
    login.type = MsgType::LOGIN;
    std::strncpy(login.token, token.c_str(), sizeof(login.token) - 1);
    sendMsg(fd, login);

    uint8_t typeByte;
    if (!readExact(fd, &typeByte, sizeof(typeByte))) { std::cout << "connection closed during login\n"; return 1; }
    if (static_cast<MsgType>(typeByte) != MsgType::ACK) {
        RejectMsg rej{}; rej.type = static_cast<MsgType>(typeByte);
        readExact(fd, reinterpret_cast<char*>(&rej) + sizeof(MsgType), sizeof(RejectMsg) - sizeof(MsgType));
        std::cout << "LOGIN REJECTED, reason=" << static_cast<int>(rej.reason) << "\n";
        return 1;
    }
    AckMsg ack{}; ack.type = MsgType::ACK;
    readExact(fd, reinterpret_cast<char*>(&ack) + sizeof(MsgType), sizeof(AckMsg) - sizeof(MsgType));
    std::cout << "LOGIN ok, sessionId=" << ack.orderId << "\n";

    // SUBSCRIBE — from here on this connection is market-data-only.
    SubscribeMsg sub{}; sub.type = MsgType::SUBSCRIBE;
    sendMsg(fd, sub);
    std::cout << "SUBSCRIBED — listening for up to " << maxMessages << " market data messages\n\n";

    int received = 0;
    while (received < maxMessages) {
        if (!readExact(fd, &typeByte, sizeof(typeByte))) {
            std::cout << "<connection closed>\n";
            break;
        }
        MsgType type = static_cast<MsgType>(typeByte);
        switch (type) {
            case MsgType::ORDER_ACCEPTED: {
                OrderAcceptedMsg m{}; m.type = type;
                if (!readExact(fd, reinterpret_cast<char*>(&m) + sizeof(MsgType), sizeof(OrderAcceptedMsg) - sizeof(MsgType))) return 1;
                std::cout << "ORDER_ACCEPTED id=" << m.orderId << " " << sideStr(m.side) << " "
                           << typeStr(m.orderType) << " price=" << m.price << " qty=" << m.qty << "\n" << std::flush;
                break;
            }
            case MsgType::TRADE: {
                TradeMsg m{}; m.type = type;
                if (!readExact(fd, reinterpret_cast<char*>(&m) + sizeof(MsgType), sizeof(TradeMsg) - sizeof(MsgType))) return 1;
                std::cout << "TRADE          maker=" << m.makerOrderId << " taker=" << m.takerOrderId
                           << " price=" << m.price << " qty=" << m.qty << " seq=" << m.seq << "\n" << std::flush;
                break;
            }
            case MsgType::BOOK_UPDATE: {
                BookUpdateMsg m{}; m.type = type;
                if (!readExact(fd, reinterpret_cast<char*>(&m) + sizeof(MsgType), sizeof(BookUpdateMsg) - sizeof(MsgType))) return 1;
                std::cout << "BOOK_UPDATE    " << sideStr(m.side) << " price=" << m.price
                           << " newQty=" << m.qtyAtLevel << (m.qtyAtLevel == 0 ? "  (level removed)" : "") << "\n" << std::flush;
                break;
            }
            default:
                std::cout << "<unexpected message type " << static_cast<int>(typeByte) << ">\n";
                close(fd);
                return 1;
        }
        ++received;
    }

    close(fd);
    std::cout << "\ndone (" << received << " messages)\n";
    return 0;
}
