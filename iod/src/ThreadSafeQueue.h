#pragma once

#include <queue>
#include <mutex>
#include <boost/thread.hpp>
#include <condition_variable>
#include <thread>
#include <boost/optional/optional.hpp>

template <typename T>
class ThreadSafeQueue {
public:
    void enqueue(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }
        cond_var_.notify_one();
    }

    bool is_empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
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

    boost::optional<T> try_dequeue() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return boost::none;
        }
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_var_.wait(lock, [this] { return !queue_.empty(); });
    }

    void wait_and_dequeue(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_var_.wait(lock, [this] { return !queue_.empty(); });
        value = std::move(queue_.front());
        queue_.pop();
    }

    bool wait_for_dequeue(T& value, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cond_var_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
            return false; // Timeout
        }
        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_var_;
};

template <typename T>
class SharedThreadSafeQueue {
public:
    SharedThreadSafeQueue(
                    boost::condition_variable_any& cv_any,
                    boost::shared_mutex& cv_mutex)
        : cond_var_any_(cv_any), cond_var_mutex_(cv_mutex) {}

    void enqueue(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }
        cond_var_any_.notify_all();
    }

    bool is_empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
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

    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_var_any_.wait(lock, [this] { return !queue_.empty(); });
    }

    void wait_and_dequeue(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            cond_var_any_.wait(lock, [this] { return !queue_.empty(); });
        }
        value = std::move(queue_.front());
        queue_.pop();
    }

    boost::shared_mutex& get_cond_var_mutex() {
        return cond_var_mutex_;
    }

    boost::condition_variable_any& get_cond_var_any() {
        return cond_var_any_;
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    boost::condition_variable_any& cond_var_any_;
    boost::shared_mutex& cond_var_mutex_;
};

