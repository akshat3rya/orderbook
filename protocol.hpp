#pragma once
#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------
// Wire format: every message starts with a 1-byte type tag, followed by a
// fixed-size payload determined entirely by that tag. No length field is
// needed — both sides know the exact size for each type at compile time.
//
// NOTE (deliberate scope decision, not an oversight): multi-byte fields
// are sent in host byte order, not network byte order. This demo only
// runs client and server on the same machine/architecture, so that's a
// correctness non-issue today. Real byte-order normalization (htobe64
// etc. per field) is a small, mechanical change deferred to the
// "production-lite hardening" pass (Day 6).
// ---------------------------------------------------------------------

enum class MsgType : uint8_t {
    NEW = 1,
    CANCEL = 2,
    MODIFY = 3,
    ACK = 4,
    REJECT = 5,
    TRADE = 6,
    BOOK_UPDATE = 7,
    LOGIN = 8,            // client -> server, must be the first message on a new connection
    ORDER_ACCEPTED = 9,   // server -> client, broadcast to market-data subscribers only
    SUBSCRIBE = 10,       // client -> server, opts a connection into the public market-data feed
};

enum class RejectReason : uint8_t {
    DUPLICATE_ID = 1,
    BAD_QTY = 2,
    UNKNOWN_ORDER_ID = 3,
    QTY_MUST_DECREASE = 4,
    AUTH_FAILED = 5,        // LOGIN with an invalid/unknown token
    PROTOCOL_VIOLATION = 6, // any message received before a successful LOGIN
};

#pragma pack(push, 1)

// Client -> server

struct LoginMsg {
    MsgType type;       // = MsgType::LOGIN
    char token[32];     // auth token, NUL-padded if shorter than 32 bytes.
                         // Plaintext, hardcoded server-side token list — fine
                         // for this skeleton, not how real auth would ship
                         // (see README "known limitations").
};

struct NewOrderMsg {
    MsgType type;       // = MsgType::NEW
    uint64_t orderId;
    uint8_t side;         // 0 = BUY, 1 = SELL
    uint8_t orderType;     // 0 = LIMIT, 1 = MARKET
    int64_t price;         // ignored (but still sent) for MARKET orders
    int64_t qty;
};

struct CancelMsg {
    MsgType type;       // = MsgType::CANCEL
    uint64_t orderId;
};

struct ModifyMsg {
    MsgType type;       // = MsgType::MODIFY
    uint64_t orderId;
    int64_t newQty;
};

// Sent as the FIRST message after a successful LOGIN to opt a connection
// into the public market-data feed instead of trading. A connection is one
// or the other for its whole lifetime — see server.cpp connectionThread for
// why (keeping exactly one writer per socket without extra locking). Real
// venues typically split order-entry and market-data onto separate
// gateways/ports for the same reason, so this isn't purely an artificial
// shortcut.
struct SubscribeMsg {
    MsgType type;       // = MsgType::SUBSCRIBE
};

// Server -> client

struct AckMsg {
    MsgType type;       // = MsgType::ACK
    uint64_t orderId;     // for LOGIN acks specifically: this carries the newly
                          // assigned sessionId instead of an order id — reusing
                          // the struct rather than adding a near-identical one.
    uint32_t numTrades;   // count of TradeMsg frames immediately following this Ack.
                          // Always 0 for Login/Cancel/Modify acks. Lets a client
                          // know exactly how many more frames to read without a
                          // separate delimiter.
};

struct RejectMsg {
    MsgType type;       // = MsgType::REJECT
    uint64_t orderId;
    RejectReason reason;
};

struct TradeMsg {
    MsgType type;       // = MsgType::TRADE
    uint64_t makerOrderId;
    uint64_t takerOrderId;
    int64_t price;
    int64_t qty;
    uint64_t seq;
};

struct BookUpdateMsg {
    MsgType type;       // = MsgType::BOOK_UPDATE
    uint8_t side;         // 0 = BUY, 1 = SELL
    int64_t price;
    int64_t qtyAtLevel;    // 0 means the level was fully removed
};

// Broadcast to market-data subscribers when a NEW order is accepted into
// the book, before any resulting matching — mirrors the fields of the
// client's own NewOrderMsg, but this is a distinct server->client type
// since a subscriber didn't send the original request and has no other
// way to correlate one.
struct OrderAcceptedMsg {
    MsgType type;       // = MsgType::ORDER_ACCEPTED
    uint64_t orderId;
    uint8_t side;
    uint8_t orderType;
    int64_t price;
    int64_t qty;
};

#pragma pack(pop)

// Fixed-size sanity checks, spelled out field-by-field rather than as a
// hardcoded literal — if padding ever creeps in (e.g. from removing
// #pragma pack), this fails loudly at compile time instead of silently
// desyncing the wire format between builds.
static_assert(sizeof(LoginMsg) == sizeof(MsgType) + 32,
    "LoginMsg layout changed");
static_assert(sizeof(NewOrderMsg) ==
    sizeof(MsgType) + sizeof(uint64_t) + 2 * sizeof(uint8_t) + 2 * sizeof(int64_t),
    "NewOrderMsg layout changed");
static_assert(sizeof(CancelMsg) == sizeof(MsgType) + sizeof(uint64_t),
    "CancelMsg layout changed");
static_assert(sizeof(ModifyMsg) == sizeof(MsgType) + sizeof(uint64_t) + sizeof(int64_t),
    "ModifyMsg layout changed");
static_assert(sizeof(AckMsg) == sizeof(MsgType) + sizeof(uint64_t) + sizeof(uint32_t),
    "AckMsg layout changed");
static_assert(sizeof(RejectMsg) == sizeof(MsgType) + sizeof(uint64_t) + sizeof(RejectReason),
    "RejectMsg layout changed");
static_assert(sizeof(TradeMsg) == sizeof(MsgType) + 5 * sizeof(uint64_t),
    "TradeMsg layout changed");
static_assert(sizeof(BookUpdateMsg) == sizeof(MsgType) + sizeof(uint8_t) + 2 * sizeof(int64_t),
    "BookUpdateMsg layout changed");
static_assert(sizeof(SubscribeMsg) == sizeof(MsgType),
    "SubscribeMsg layout changed");
static_assert(sizeof(OrderAcceptedMsg) ==
    sizeof(MsgType) + sizeof(uint64_t) + 2 * sizeof(uint8_t) + 2 * sizeof(int64_t),
    "OrderAcceptedMsg layout changed");

// Read/write exactly n bytes on a blocking fd, retrying on EINTR and short
// reads/writes. Returns false on EOF (peer closed) or unrecoverable error.
bool readExact(int fd, void* buf, size_t n);
bool writeExact(int fd, const void* buf, size_t n);

// Every Msg struct above is POD and already wire-shaped (packed, no
// padding), so sending one is just a raw byte write of sizeof(msg).
template <typename Msg>
bool sendMsg(int fd, const Msg& msg) {
    return writeExact(fd, &msg, sizeof(msg));
}
