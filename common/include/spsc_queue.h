#pragma once

#include <atomic>
#include <vector>
#include <optional>
#include <cstddef>

template <typename T, size_t Capacity>
class SPSCQueue {
public:
    SPSCQueue() : head_(0), tail_(0) {
        buffer_.resize(Capacity + 1);
    }

    ~SPSCQueue() = default;

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    bool Push(const T& item) {
        const size_t currentTail = tail_.load(std::memory_order_relaxed);
        const size_t currentHead = head_.load(std::memory_order_acquire);

        size_t nextTail = (currentTail + 1) % buffer_.size();
        if (nextTail == currentHead) {
            return false; 
        }

        buffer_[currentTail] = item;
        tail_.store(nextTail, std::memory_order_release);
        return true;
    }

    bool Push(T&& item) {
        const size_t currentTail = tail_.load(std::memory_order_relaxed);
        const size_t currentHead = head_.load(std::memory_order_acquire);

        size_t nextTail = (currentTail + 1) % buffer_.size();
        if (nextTail == currentHead) {
            return false;
        }

        buffer_[currentTail] = std::move(item);
        tail_.store(nextTail, std::memory_order_release);
        return true;
    }

    bool Pop(T& item) {
        const size_t currentHead = head_.load(std::memory_order_relaxed);
        const size_t currentTail = tail_.load(std::memory_order_acquire);

        if (currentHead == currentTail) {
            return false; 
        }

        item = std::move(buffer_[currentHead]);
        head_.store((currentHead + 1) % buffer_.size(), std::memory_order_release);
        return true;
    }

    bool Empty() const {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

private:
    std::vector<T> buffer_;
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
};