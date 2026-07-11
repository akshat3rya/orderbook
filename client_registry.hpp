#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

// Tracks which sessions are currently connected, their socket fd, and
// which of them are subscribed to the public market-data feed (Day 5).
// A session is added on successful LOGIN and removed on disconnect;
// subscription is a separate, later opt-in (see server.cpp
// connectionThread) since not every connected session wants the feed.
class ClientRegistry {
public:
    void add(uint64_t sessionId, int fd) {
        std::lock_guard<std::mutex> lock(mtx_);
        sessions_[sessionId] = fd;
    }

    // Also clears any subscription — a disconnected session shouldn't
    // linger in the broadcast list.
    void remove(uint64_t sessionId) {
        std::lock_guard<std::mutex> lock(mtx_);
        sessions_.erase(sessionId);
        subscribed_.erase(sessionId);
    }

    void subscribe(uint64_t sessionId) {
        std::lock_guard<std::mutex> lock(mtx_);
        subscribed_.insert(sessionId);
    }

    void unsubscribe(uint64_t sessionId) {
        std::lock_guard<std::mutex> lock(mtx_);
        subscribed_.erase(sessionId);
    }

    size_t count() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return sessions_.size();
    }

    size_t subscriberCount() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return subscribed_.size();
    }

    // fn(sessionId, fd) is called for every connected session while the
    // registry's lock is held — keep fn fast and don't call back into
    // this registry from within it.
    void forEach(const std::function<void(uint64_t, int)>& fn) const {
        std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& [sessionId, fd] : sessions_) fn(sessionId, fd);
    }

    // Same as forEach, but only for sessions that called SUBSCRIBE. This
    // is what the Day 5 broadcaster thread iterates.
    //
    // KNOWN LIMITATION: fn is called while holding the registry lock, so a
    // slow/stuck subscriber's write can delay delivery to everyone else in
    // this call AND block other threads' add()/remove()/subscribe() calls
    // meanwhile. A per-client outbound queue would fix this properly;
    // deferred as a hardening item (see README).
    void forEachSubscriber(const std::function<void(uint64_t, int)>& fn) const {
        std::lock_guard<std::mutex> lock(mtx_);
        for (uint64_t sessionId : subscribed_) {
            auto it = sessions_.find(sessionId);
            if (it != sessions_.end()) fn(sessionId, it->second);
        }
    }

private:
    mutable std::mutex mtx_;
    std::unordered_map<uint64_t, int> sessions_;
    std::unordered_set<uint64_t> subscribed_;
};
