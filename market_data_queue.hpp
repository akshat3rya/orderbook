#pragma once
#include "guarded_queue.hpp"
#include "protocol.hpp"

// One event from the matching engine's internal event stream. Holds all
// three payload structs rather than a tagged union — same reasoning as
// Command in command_queue.hpp — and reuses the actual wire structs
// (TradeMsg, BookUpdateMsg, OrderAcceptedMsg) directly as payloads, so
// broadcasting one is just sendMsg()'ing the relevant field, no
// translation step.
enum class MarketEventType : uint8_t { ORDER_ACCEPTED, TRADE, BOOK_DELTA };

struct MarketDataEvent {
    MarketEventType type = MarketEventType::TRADE;
    OrderAcceptedMsg orderAccepted{};
    TradeMsg trade{};
    BookUpdateMsg bookUpdate{};
};

// The engine thread is the only producer (it's the only thread that ever
// mutates the book or runs the matching loop); the broadcaster thread is
// the only consumer. Pushing here is cheap in-memory work, which is the
// whole point: it decouples "the book changed" from "writing that change
// out over a socket to N subscribers", so a slow network write never
// stalls the matching engine itself.
using MarketDataQueue = GuardedQueue<MarketDataEvent>;
