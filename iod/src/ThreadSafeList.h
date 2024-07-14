#pragma once

#include <list>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <thread>
#include <boost/thread.hpp>

template <typename T>
class ThreadSafeList {
public:
    void push_back(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        list_.push_back(value);
    }

    void push_front(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        list_.push_front(value);
    }

    bool try_pop_back(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (list_.empty()) {
            return false;
        }
        value = list_.back();
        list_.pop_back();
        return true;
    }

    bool try_pop_front(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (list_.empty()) {
            return false;
        }
        value = list_.front();
        list_.pop_front();
        return true;
    }

    void remove(T &value) {
        std::lock_guard<std::mutex> lock(mutex_);
        list_.remove(value);
    }

    void print() const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& item : list_) {
            std::cout << item << " ";
        }
        std::cout << std::endl;
    }

    std::list<T> &lock() {
        std::lock_guard<std::mutex> lock(mutex_);
        return list_;
    }

    void unlock() {
        mutex_.unlock();
    }

private:
    std::list<T> list_;
    mutable std::mutex mutex_;
};

#include <list>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <iostream>
#include <thread>
#include <vector>

template <typename T>
class SharedThreadSafeList {
public:
    SharedThreadSafeList(boost::condition_variable_any& cv_any, boost::shared_mutex& cv_mutex)
        : cond_var_any_(cv_any), cond_var_mutex_(cv_mutex) {}

    void push_back(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            list_.push_back(std::move(value));
        }
        cond_var_any_.notify_all();
    }

    bool try_pop_front(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (list_.empty()) {
            return false;
        }
        value = std::move(list_.front());
        list_.pop_front();
        return true;
    }

    bool is_empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return list_.empty();
    }

    boost::shared_mutex& get_cond_var_mutex() {
        return cond_var_mutex_;
    }

    boost::condition_variable_any& get_cond_var_any() {
        return cond_var_any_;
    }

private:
    std::list<T> list_;
    mutable std::mutex mutex_;
    boost::condition_variable_any& cond_var_any_;
    boost::shared_mutex& cond_var_mutex_;
};

