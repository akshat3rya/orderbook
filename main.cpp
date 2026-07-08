#include <iostream>
#include <sstream>
#include <string>
#include "order_book.hpp"

// Commands:
//   insert <id> <B|S> <price> <qty>
//   cancel <id>
//   modify <id> <newQty>
//   book [depth]
//   help
//   quit

static void printHelp() {
    std::cout <<
        "insert <id> <B|S> <price> <qty>   add a resting order\n"
        "cancel <id>                       cancel a live order\n"
        "modify <id> <newQty>              in-place quantity decrease\n"
        "book [depth]                      print bids/asks (default depth 10)\n"
        "help                              show this message\n"
        "quit                              exit\n";
}

int main() {
    OrderBook book;
    std::string line;

    std::cout << "Order book CLI harness. Type 'help' for commands.\n";
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "quit" || cmd == "exit") {
            break;
        } else if (cmd == "help") {
            printHelp();
        } else if (cmd == "insert") {
            uint64_t id; char sideCh; int64_t price, qty;
            if (!(iss >> id >> sideCh >> price >> qty)) {
                std::cout << "usage: insert <id> <B|S> <price> <qty>\n";
                continue;
            }
            Side side = (sideCh == 'B' || sideCh == 'b') ? Side::BUY : Side::SELL;
            if (book.insert(id, side, price, qty))
                std::cout << "ok\n";
            else
                std::cout << "error: duplicate id or bad qty\n";
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
        } else {
            std::cout << "unknown command. type 'help'\n";
        }
    }
    return 0;
}
