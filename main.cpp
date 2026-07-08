#include <iostream>
#include <sstream>
#include <string>
#include "order_book.hpp"
#include "matching_engine.hpp"

static void printHelp() {
    std::cout <<
        "insert <id> <B|S> <price> <qty>   add a resting order directly, no matching\n"
        "limit  <id> <B|S> <price> <qty>   submit a limit order (matches, then may rest)\n"
        "market <id> <B|S> <qty>           submit a market order (matches, never rests)\n"
        "cancel <id>                       cancel a live order\n"
        "modify <id> <newQty>              in-place quantity decrease\n"
        "book [depth]                      print bids/asks (default depth 10)\n"
        "help                              show this message\n"
        "quit                              exit\n";
}

static void printMatchResult(const MatchResult& r) {
    if (!r.accepted) {
        std::cout << "error: rejected (duplicate id or qty <= 0)\n";
        return;
    }
    for (const auto& t : r.trades)
        std::cout << "  trade: maker=" << t.makerOrderId << " taker=" << t.takerOrderId << " price=" << t.price << " qty=" << t.qty << "\n";
        
    std::cout << "filled=" << r.filledQty << " remaining=" << r.remainingQty << " rested=" << (r.restedOnBook ? "yes" : "no") << "\n";
}

int main() {
    OrderBook book;
    MatchingEngine engine(book);
    std::string line;

    std::cout << "Order book CLI harness. Type 'help' for commands.\n";
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "quit" || cmd == "exit") break;
        else if (cmd == "help") printHelp();
        else if (cmd == "insert") {
            uint64_t id; char sideCh; int64_t price, qty;
            if (!(iss >> id >> sideCh >> price >> qty)) {
                std::cout << "usage: insert <id> <B|S> <price> <qty>\n";
                continue;
            }
            Side side = (sideCh == 'B' || sideCh == 'b') ? Side::BUY : Side::SELL;
            if (book.insert(id, side, price, qty)) std::cout << "ok\n";
            else std::cout << "error: duplicate id or bad qty\n";
        } else if (cmd == "limit") {
            uint64_t id; char sideCh; int64_t price, qty;
            if (!(iss >> id >> sideCh >> price >> qty)) {
                std::cout << "usage: limit <id> <B|S> <price> <qty>\n";
                continue;
            }
            Side side = (sideCh == 'B' || sideCh == 'b') ? Side::BUY : Side::SELL;
            printMatchResult(engine.submit(id, side, OrderType::LIMIT, price, qty));
        } else if (cmd == "market") {
            uint64_t id; char sideCh; int64_t qty;
            if (!(iss >> id >> sideCh >> qty)) {
                std::cout << "usage: market <id> <B|S> <qty>\n";
                continue;
            }
            Side side = (sideCh == 'B' || sideCh == 'b') ? Side::BUY : Side::SELL;
            printMatchResult(engine.submit(id, side, OrderType::MARKET, 0, qty));
        } else if (cmd == "cancel") {
            uint64_t id;
            if (!(iss >> id)) { std::cout << "usage: cancel <id>\n"; continue; }
            std::cout << (book.cancel(id) ? "ok\n" : "error: not found\n");
        } else if (cmd == "modify") {
            uint64_t id; int64_t newQty;
            if (!(iss >> id >> newQty)) { std::cout << "usage: modify <id> <newQty>\n"; continue; }
            switch (book.modify(id, newQty)) {
                case ModifyResult::OK: std::cout << "ok\n"; break;
                case ModifyResult::NOT_FOUND: std::cout << "error: not found\n"; break;
                case ModifyResult::QTY_MUST_DECREASE: std::cout << "error: new qty must be less than current qty\n"; break;
                case ModifyResult::RESULTED_IN_CANCEL: std::cout << "ok (qty <= 0, order cancelled)\n"; break;
            }
        } else if (cmd == "book") {
            int depth = 10;
            iss >> depth;
            book.print(depth);
        } else std::cout << "unknown command. type 'help'\n";
    }
    return 0;
}