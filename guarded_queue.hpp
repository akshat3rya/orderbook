#pragma once
#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>

// Generic thread-safe blocking queue: many producers, one (or more)
// consumers. Same thread-safety pattern is used for two different jobs in
// this project — feeding client commands to the matching engine (Day 4)
// and feeding market-data events to the broadcaster (Day 5) — so it's
// factored out once instead of duplicated.
template <typename T>
class GuardedQueue {
public:
    void push(T item) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.push_back(std::move(item));
        }
        cv_.notify_one();
    }

    // Blocks until an item is available, or returns false once shutdown()
    // has been called AND the queue has been fully drained. That two-part
    // condition matters: shutdown() must not discard work queued before it
    // was called.
    bool pop(T& out) {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return !queue_.empty() || shuttingDown_; });
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            shuttingDown_ = true;
        }
        cv_.notify_all(); // wake any consumer blocked on an empty queue
    }

private:
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<T> queue_;
    bool shuttingDown_ = false;
};
