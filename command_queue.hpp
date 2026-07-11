#pragma once
#include <cstdint>
#include "guarded_queue.hpp"
#include "protocol.hpp"

// One request from one client connection, queued up for the engine thread.
// Carries its own response fd so the engine thread can reply directly —
// no separate response-routing table needed. Holds all three payload
// structs rather than a tagged union/variant; they're tiny and this keeps
// the queue's element type simple and copyable.
enum class CommandType : uint8_t { NEW, CANCEL, MODIFY };

struct Command {
    CommandType type = CommandType::NEW;
    int clientFd = -1;
    uint64_t sessionId = 0; // for logging/traceability, not used for auth here
    NewOrderMsg newMsg{};
    CancelMsg cancelMsg{};
    ModifyMsg modifyMsg{};
};

// Multiple connection threads push (producers); exactly one engine thread
// pops (consumer). This is what keeps order-book mutation single-threaded
// — and therefore lock-free inside OrderBook/MatchingEngine — even though
// many client sockets are being read concurrently.
using CommandQueue = GuardedQueue<Command>;
