#include <arpa/inet.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <thread>
#include <unordered_set>
#include <unistd.h>

#include "client_registry.hpp"
#include "command_queue.hpp"
#include "market_data_queue.hpp"
#include "matching_engine.hpp"
#include "order_book.hpp"
#include "protocol.hpp"

// Hardcoded for this skeleton — a real deployment would load hashed
// credentials from config/secrets storage behind TLS, not bake plaintext
// tokens into the binary. Documented simplification, not an oversight
// (see README "known limitations").
static const std::unordered_set<std::string> kValidTokens = {
    "trader1-secret", "trader2-secret", "admin-secret",
};

static std::mutex g_logMutex;
static void logLine(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::cout << msg << "\n";
}

// ---------------------------------------------------------------------
// Response helpers — called ONLY from the engine thread (see engineLoop).
// ---------------------------------------------------------------------

static void sendReject(int fd, uint64_t orderId, RejectReason reason) {
    RejectMsg m{};
    m.type = MsgType::REJECT;
    m.orderId = orderId;
    m.reason = reason;
    sendMsg(fd, m);
}

static void handleNew(int fd, const NewOrderMsg& msg, OrderBook& book, MatchingEngine& engine, MarketDataQueue& marketData) {
    if (msg.qty <= 0) {
        sendReject(fd, msg.orderId, RejectReason::BAD_QTY);
        return;
    }
    if (book.contains(msg.orderId)) {
        sendReject(fd, msg.orderId, RejectReason::DUPLICATE_ID);
        return;
    }

    Side side = msg.side == 0 ? Side::BUY : Side::SELL;
    OrderType type = msg.orderType == 0 ? OrderType::LIMIT : OrderType::MARKET;

    // Broadcast "order accepted" BEFORE matching, so subscribers see
    // acceptance ordered ahead of any resulting trades/book deltas — those
    // get pushed onto `marketData` automatically as a side effect of
    // engine.submit() below, via OrderBook's level-change listener (set up
    // once in main()) and the trade-broadcast loop right after.
    MarketDataEvent accepted;
    accepted.type = MarketEventType::ORDER_ACCEPTED;
    accepted.orderAccepted.type = MsgType::ORDER_ACCEPTED;
    accepted.orderAccepted.orderId = msg.orderId;
    accepted.orderAccepted.side = msg.side;
    accepted.orderAccepted.orderType = msg.orderType;
    accepted.orderAccepted.price = msg.price;
    accepted.orderAccepted.qty = msg.qty;
    marketData.push(std::move(accepted));

    MatchResult result = engine.submit(msg.orderId, side, type, msg.price, msg.qty);
    if (!result.accepted) { // shouldn't happen, pre-checked above
        sendReject(fd, msg.orderId, RejectReason::BAD_QTY);
        return;
    }

    AckMsg ack{};
    ack.type = MsgType::ACK;
    ack.orderId = msg.orderId;
    ack.numTrades = static_cast<uint32_t>(result.trades.size());
    if (!sendMsg(fd, ack)) {
        return;
    }

    for (const auto& t : result.trades) {
        // Private copy to the submitter, over the trading connection...
        TradeMsg tm{};
        tm.type = MsgType::TRADE;
        tm.makerOrderId = t.makerOrderId;
        tm.takerOrderId = t.takerOrderId;
        tm.price = t.price;
        tm.qty = t.qty;
        tm.seq = t.seq;
        if (!sendMsg(fd, tm)) {
            return;
        }

        // ...and a public copy to every market-data subscriber. Same data,
        // different channel — a real exchange separates order-entry
        // confirmations from the public tape the same way.
        MarketDataEvent tradeEvent;
        tradeEvent.type = MarketEventType::TRADE;
        tradeEvent.trade = tm;
        marketData.push(std::move(tradeEvent));
    }
}

static void handleCancel(int fd, const CancelMsg& msg, OrderBook& book) {
    if (book.cancel(msg.orderId)) {
        AckMsg ack{};
        ack.type = MsgType::ACK;
        ack.orderId = msg.orderId;
        ack.numTrades = 0;
        sendMsg(fd, ack);
    } else {
        sendReject(fd, msg.orderId, RejectReason::UNKNOWN_ORDER_ID);
    }
}

static void handleModify(int fd, const ModifyMsg& msg, OrderBook& book) {
    ModifyResult r = book.modify(msg.orderId, msg.newQty);
    switch (r) {
        case ModifyResult::OK:
        case ModifyResult::RESULTED_IN_CANCEL: {
            AckMsg ack{};
            ack.type = MsgType::ACK;
            ack.orderId = msg.orderId;
            ack.numTrades = 0;
            sendMsg(fd, ack);
            break;
        }
        case ModifyResult::NOT_FOUND:
            sendReject(fd, msg.orderId, RejectReason::UNKNOWN_ORDER_ID);
            break;
        case ModifyResult::QTY_MUST_DECREASE:
            sendReject(fd, msg.orderId, RejectReason::QTY_MUST_DECREASE);
            break;
    }
}

// ---------------------------------------------------------------------
// Engine thread — the ONLY thread that ever touches `book` or `engine`.
// Every client connection thread only ever pushes onto `commands`; that
// serialization is what keeps the matching engine itself single-threaded
// and lock-free, exactly as the plan requires "for correctness". It's
// also the sole producer for `marketData` — either directly (ORDER_ACCEPTED,
// TRADE, from handleNew) or indirectly via OrderBook's level-change
// listener (BOOK_DELTA, wired up once in main()).
// ---------------------------------------------------------------------

static void engineLoop(CommandQueue& commands, OrderBook& book, MatchingEngine& engine, MarketDataQueue& marketData) {
    Command cmd;
    while (commands.pop(cmd)) {
        switch (cmd.type) {
            case CommandType::NEW:
                handleNew(cmd.clientFd, cmd.newMsg, book, engine, marketData);
                break;
            case CommandType::CANCEL:
                handleCancel(cmd.clientFd, cmd.cancelMsg, book);
                break;
            case CommandType::MODIFY:
                handleModify(cmd.clientFd, cmd.modifyMsg, book);
                break;
        }
    }
}

// ---------------------------------------------------------------------
// Broadcaster thread — the ONLY thread that ever writes to a subscriber's
// fd. Drains `marketData` and fans each event out to every currently
// subscribed session. Deliberately separate from the engine thread: a
// slow network write to one subscriber must never stall order matching.
// ---------------------------------------------------------------------

static void broadcastLoop(MarketDataQueue& marketData, ClientRegistry& registry) {
    MarketDataEvent ev;
    while (marketData.pop(ev)) {
        switch (ev.type) {
            case MarketEventType::ORDER_ACCEPTED:
                registry.forEachSubscriber([&](uint64_t, int fd) { sendMsg(fd, ev.orderAccepted); });
                break;
            case MarketEventType::TRADE:
                registry.forEachSubscriber([&](uint64_t, int fd) { sendMsg(fd, ev.trade); });
                break;
            case MarketEventType::BOOK_DELTA:
                registry.forEachSubscriber([&](uint64_t, int fd) { sendMsg(fd, ev.bookUpdate); });
                break;
        }
        // A failed sendMsg() here (subscriber gone) is intentionally
        // ignored: that connection's own thread will independently notice
        // the closed socket via its next readExact() and clean up via
        // registry.remove(). No need to duplicate that bookkeeping here.
    }
}

// ---------------------------------------------------------------------
// Connection threads — one per client. Each thread only ever reads its own
// fd and only ever writes to it indirectly (via Commands the engine thread
// answers). The engine thread is the only writer of any fd, so there's no
// need to guard the sockets themselves with a lock — reader and writer are
// always different, fixed threads per connection.
// ---------------------------------------------------------------------

// Reads and validates the mandatory first LOGIN message. Returns the
// assigned sessionId on success, 0 on failure (caller closes the socket).
static uint64_t doLoginHandshake(int fd, std::atomic<uint64_t>& nextSessionId) {
    uint8_t typeByte;
    if (!readExact(fd, &typeByte, sizeof(typeByte))) { // disconnected before logging in
        return 0;
    }

    if (static_cast<MsgType>(typeByte) != MsgType::LOGIN) {
        sendReject(fd, 0, RejectReason::PROTOCOL_VIOLATION);
        return 0;
    }

    LoginMsg msg{};
    msg.type = MsgType::LOGIN;
    if (!readExact(fd, reinterpret_cast<char*>(&msg) + sizeof(MsgType), sizeof(LoginMsg) - sizeof(MsgType))) {
        return 0;
    }

    std::string token(msg.token, strnlen(msg.token, sizeof(msg.token)));
    if (kValidTokens.find(token) == kValidTokens.end()) {
        sendReject(fd, 0, RejectReason::AUTH_FAILED);
        return 0;
    }

    uint64_t sessionId = nextSessionId.fetch_add(1);
    AckMsg ack{};
    ack.type = MsgType::ACK;
    ack.orderId = sessionId; // repurposed for LOGIN — see protocol.hpp
    ack.numTrades = 0;
    if (!sendMsg(fd, ack)) {
        return 0;
    }
    return sessionId;
}

static void connectionThread(int fd, std::string peerLabel, CommandQueue& commands, ClientRegistry& registry, std::atomic<uint64_t>& nextSessionId) {
    uint64_t sessionId = doLoginHandshake(fd, nextSessionId);
    if (sessionId == 0) {
        logLine("[" + peerLabel + "] login failed — closing connection");
        close(fd);
        return;
    }

    registry.add(sessionId, fd);
    logLine("[" + peerLabel + "] session " + std::to_string(sessionId) + " authenticated (" + std::to_string(registry.count()) + " connected)");

    // The first message after LOGIN decides this connection's role for its
    // whole lifetime: SUBSCRIBE makes it a market-data-only feed (receives
    // broadcasts from the broadcaster thread, never trades); anything else
    // is treated as the first trading command in the normal dispatch loop.
    // A connection can't switch roles mid-stream — see protocol.hpp
    // SubscribeMsg for why that's fine, not just a shortcut.
    uint8_t typeByte;
    if (!readExact(fd, &typeByte, sizeof(typeByte))) {
        registry.remove(sessionId);
        close(fd);
        logLine("[" + peerLabel + "] session " + std::to_string(sessionId) + " disconnected (" + std::to_string(registry.count()) + " connected)");
        return;
    }

    if (static_cast<MsgType>(typeByte) == MsgType::SUBSCRIBE) {
        registry.subscribe(sessionId);
        logLine("[" + peerLabel + "] session " + std::to_string(sessionId) + " subscribed to market data (" + std::to_string(registry.subscriberCount()) + " subscribers)");

        // A subscriber is expected to only ever receive from here on — the
        // broadcaster thread is the sole writer of this fd now. This loop
        // just reads (blocking) to detect disconnect; anything a
        // subscriber sends after SUBSCRIBE is a protocol violation.
        if (readExact(fd, &typeByte, sizeof(typeByte))) {
            logLine("[" + peerLabel + "] session " + std::to_string(sessionId) + " (subscriber) sent unexpected message — closing");
        }

        registry.remove(sessionId);
        close(fd);
        logLine("[" + peerLabel + "] session " + std::to_string(sessionId) + " disconnected");
        return;
    }

    // Not a SUBSCRIBE — this is a trading connection. Re-enter the normal
    // dispatch loop, treating the byte already read as its first message.
    while (true) {
        MsgType type = static_cast<MsgType>(typeByte);
        Command cmd;
        cmd.clientFd = fd;
        cmd.sessionId = sessionId;

        bool ok = true;
        switch (type) {
            case MsgType::NEW:
                cmd.type = CommandType::NEW;
                cmd.newMsg.type = type;
                ok = readExact(fd, reinterpret_cast<char*>(&cmd.newMsg) + sizeof(MsgType), sizeof(NewOrderMsg) - sizeof(MsgType));
                break;
            case MsgType::CANCEL:
                cmd.type = CommandType::CANCEL;
                cmd.cancelMsg.type = type;
                ok = readExact(fd, reinterpret_cast<char*>(&cmd.cancelMsg) + sizeof(MsgType), sizeof(CancelMsg) - sizeof(MsgType));
                break;
            case MsgType::MODIFY:
                cmd.type = CommandType::MODIFY;
                cmd.modifyMsg.type = type;
                ok = readExact(fd, reinterpret_cast<char*>(&cmd.modifyMsg) + sizeof(MsgType), sizeof(ModifyMsg) - sizeof(MsgType));
                break;
            default:
                logLine("[" + peerLabel + "] session " + std::to_string(sessionId) + " sent unexpected type " + std::to_string(static_cast<int>(typeByte)) + " — closing");
                ok = false;
                break;
        }
        if (!ok) {
            break;
        }

        // Hand off and immediately go back to reading — this thread never
        // waits for the engine's response. The engine thread writes the
        // Ack/Reject/Trade reply straight to `fd` once it's this command's
        // turn in the shared queue.
        commands.push(std::move(cmd));

        if (!readExact(fd, &typeByte, sizeof(typeByte))) { // read the NEXT message's tag
            break;
        }
    }

    registry.remove(sessionId);
    close(fd);
    logLine("[" + peerLabel + "] session " + std::to_string(sessionId) + " disconnected (" + std::to_string(registry.count()) + " connected)");
}

int main(int argc, char** argv) {
    int port = 5555;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    signal(SIGPIPE, SIG_IGN); // a client vanishing mid-send() shouldn't kill the server

    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(listenFd, 64) < 0) {
        perror("listen");
        return 1;
    }

    OrderBook book;
    MatchingEngine engine(book);
    CommandQueue commands;
    MarketDataQueue marketData;
    ClientRegistry registry;
    std::atomic<uint64_t> nextSessionId{1};

    // Every book mutation (insert/cancel/modify/fill) reports the affected
    // level's new total qty here — this is the BOOK_DELTA event source.
    // Set before the accept loop starts, so no mutation is ever missed.
    // Only ever invoked from the engine thread (OrderBook is only ever
    // touched there), so pushing onto marketData from inside this lambda
    // needs no extra locking beyond what MarketDataQueue already provides.
    book.setLevelChangeListener([&marketData](Side side, int64_t price, int64_t qty) {
        MarketDataEvent ev;
        ev.type = MarketEventType::BOOK_DELTA;
        ev.bookUpdate.type = MsgType::BOOK_UPDATE;
        ev.bookUpdate.side = static_cast<uint8_t>(side);
        ev.bookUpdate.price = price;
        ev.bookUpdate.qtyAtLevel = qty;
        marketData.push(std::move(ev));
    });

    std::thread engineThread(engineLoop, std::ref(commands), std::ref(book), std::ref(engine), std::ref(marketData));
    std::thread broadcastThread(broadcastLoop, std::ref(marketData), std::ref(registry));

    logLine("matching engine server listening on port " + std::to_string(port) + " (thread-per-connection, single engine thread, single broadcaster thread)");

    while (true) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = accept(listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (clientFd < 0) {
            perror("accept");
            continue;
        }

        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
        std::string peerLabel = std::string(ipStr) + ":" + std::to_string(ntohs(clientAddr.sin_port));
        logLine("[" + peerLabel + "] connected, awaiting login");

        // Detached: this skeleton doesn't track connection threads for a
        // graceful shutdown yet — that's explicitly Day 6 scope. The
        // engine thread and its queue are what actually need to be clean;
        // a detached reader thread just exits when its socket closes.
        std::thread(connectionThread, clientFd, peerLabel, std::ref(commands), std::ref(registry), std::ref(nextSessionId)).detach();
    }

    // Unreachable while the accept loop runs forever, but documents the
    // intended shutdown sequence for when Day 6 wires up SIGINT handling.
    commands.shutdown();
    engineThread.join();
    marketData.shutdown();
    broadcastThread.join();
    close(listenFd);
    return 0;
}