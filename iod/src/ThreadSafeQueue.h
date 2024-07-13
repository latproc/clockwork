#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <thread>

template <typename T>
class ThreadSafeQueue {
    static_assert(std::is_copy_constructible<T>::value || std::is_move_constructible<T>::value,
                      "T must be copy or move constructible");
    static_assert(std::is_destructible<T>::value, "T must be destructible");

public:
    void enqueue(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(value));
        cond_var_.notify_one();
    }

    bool try_dequeue(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void wait_and_dequeue(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_var_.wait(lock, [this]() { return !queue_.empty(); });
        value = std::move(queue_.front());
        queue_.pop();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_var_;
};

