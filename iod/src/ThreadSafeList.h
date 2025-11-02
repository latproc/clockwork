// This header defines two related containers:
// 1) ThreadSafeList<T>: owns its own mutex and protects access to an internal std::list<T>.
//    It is a self-contained utility for push/pop/check-empty operations in a single process.
//    All synchronization is internal to the object.
// 2) SharedThreadSafeList<T>: similar list operations, but it is meant to participate in
//    a *shared* synchronization scheme. It does NOT own its condition variable or shared mutex;
//    instead, they are injected from outside (by reference) so that several components/threads
//    can wait/notify on the same objects. Use this one when multiple lists or other data
//    structures must coordinate via the same condition variable / mutex.

#pragma once

#include <list>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <boost/thread.hpp>

#ifdef CW_DEBUG_LIST_REENTRY
#include <vector>
#include <algorithm>

#include <execinfo.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

inline void dump_stack_trace() {
    constexpr int MAX_FRAMES = 64;
    void *frames[MAX_FRAMES];
    int count = backtrace(frames, MAX_FRAMES);

    // Print to stderr
    fprintf(stderr, "\n========== ThreadSafeList Reentry Detected ==========\n");
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    fprintf(stderr, "=====================================================\n\n");
}

struct ThreadSafeListReentryProbe {
    static thread_local std::vector<const void*> stack;
    const void* obj;

    explicit ThreadSafeListReentryProbe(const void* o) : obj(o) {
        auto it = std::find(stack.begin(), stack.end(), obj);
        // re-entry from the same thread into the same object
        if (it != stack.end()) {
            fprintf(stderr, "Reentrant call into ThreadSafeList @%p detected!\n", obj);
            dump_stack_trace();
            std::abort();
        }
        stack.push_back(obj);
    }

    ~ThreadSafeListReentryProbe() {
        auto it = std::find(stack.begin(), stack.end(), obj);
        if (it != stack.end()) {
            stack.erase(it);
        }
    }
};
#endif

// ThreadSafeList<T>
// -----------------
// A simple, self-contained, mutex-protected std::list<T>.
// All methods acquire an internal std::mutex before touching the list.
// Intended for cases where this container alone needs protection.
template <typename T>
class ThreadSafeList {
public:

    // Returns true if the list has no elements. Thread-safe.
    bool is_empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return list_.empty();
    }
    // Adds an element at the end. Thread-safe.
    void push_back(const T& value) {
#ifdef CW_DEBUG_LIST_REENTRY
        ThreadSafeListReentryProbe guard(this);
#endif
        std::lock_guard<std::mutex> lock(mutex_);
        list_.push_back(value);
    }

    // Adds an element at the end. Thread-safe.
    void push_back(T&& value) {
#ifdef CW_DEBUG_LIST_REENTRY
        ThreadSafeListReentryProbe guard(this);
#endif
        std::lock_guard<std::mutex> lock(mutex_);
        list_.push_back(std::move(value));
    }

    // Adds an element at the front. Thread-safe.
    void push_front(const T& value) {
#ifdef CW_DEBUG_LIST_REENTRY
        ThreadSafeListReentryProbe guard(this);
#endif
        std::lock_guard<std::mutex> lock(mutex_);
        list_.push_front(value);
    }

    // Adds an element at the front. Thread-safe.
    void push_front(T&& value) {
#ifdef CW_DEBUG_LIST_REENTRY
        ThreadSafeListReentryProbe guard(this);
#endif
        std::lock_guard<std::mutex> lock(mutex_);
        list_.push_front(std::move(value));
    }

    // Tries to remove the last element. Returns false if empty.
    bool try_pop_back(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (list_.empty()) {
            return false;
        }
        value = list_.back();
        list_.pop_back();
        return true;
    }

    // Tries to remove the first element. Returns false if empty.
    bool try_pop_front(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (list_.empty()) {
            return false;
        }
        value = list_.front();
        list_.pop_front();
        return true;
    }

    // Removes all elements equal to value. Thread-safe but O(n).
    // Take the parameter as const T& so callers can pass temporaries (e.g. a raw pointer literal).
    void remove(const T &value) {
#ifdef CW_DEBUG_LIST_REENTRY
        ThreadSafeListReentryProbe guard(this);
#endif
        std::lock_guard<std::mutex> lock(mutex_);
        list_.remove(value);
    }

    // Debug helper: prints the contents while holding the mutex.
    void print() const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& item : list_) {
            std::cout << item << " ";
        }
        std::cout << std::endl;
    }

    // Applies `action` to every element that satisfies `pred`, while holding the mutex.
    template <typename Action>
    void for_each(Action&& action) {
#ifdef CW_DEBUG_LIST_REENTRY
        ThreadSafeListReentryProbe guard(this);
#endif
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& elem : list_) {
            action(elem);
        }
    }
    // Applies `action` to every element that satisfies `pred`, while holding the mutex.
    // This lets callers perform filtered operations on the list without exposing
    // the underlying container or its lock.
    //
    // Example:
    //   list.for_each_if(
    //       [](Receiver* r){ return r->receives(msg, from); },
    //       [](Receiver* r){ r->enqueue(pkg); }
    //   );
    template <typename Predicate, typename Action>
    void for_each_if(Predicate&& pred, Action&& action) {
#ifdef CW_DEBUG_LIST_REENTRY
        ThreadSafeListReentryProbe guard(this);
#endif
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& elem : list_) {
            if (pred(elem)) {
                action(elem);
            }
        }
    }
    // NOTE:
    // The earlier version of this class exposed `lock()`/`unlock()` to let callers
    // hold the mutex across multiple operations. That pattern is error-prone because
    // it returns a reference to the list after locking with a local lock_guard
    // (which unlocks immediately), and it also exposes the mutex_ directly.
    // If external locking is needed, consider refactoring to provide a scoped accessor
    // or a separate synchronization primitive instead.

private:
    std::list<T> list_;
    mutable std::mutex mutex_;
};

// -----------------------------------------------------------------------------
// SharedThreadSafeList<T>
// -----------------------------------------------------------------------------
// This variant is meant to work together with *external* synchronization.
// Instead of owning its own condition variable / shared mutex, it takes them
// by reference in the constructor. That allows several lists (or other data
// structures) to notify the same condition variable so that waiting threads
// can wake up regardless of which list received data.
// Use this when multiple producers/consumers must coordinate via the same
// condition variable and shared mutex.

template <typename T>
class SharedThreadSafeList {
public:
    // The list does not own these synchronization primitives; they must outlive this list.
    // This makes it possible to have many lists signal the same condition variable.
    SharedThreadSafeList(boost::condition_variable_any& cv_any, boost::shared_mutex& cv_mutex)
        : cond_var_any_(cv_any), cond_var_mutex_(cv_mutex) {}

    // Pushes a value and then notifies *all* waiters on the shared condition variable.
    // We notify outside of the list's own mutex to reduce contention.
    void push_back(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            list_.push_back(std::move(value));
        }
        cond_var_any_.notify_all();
    }

    // Tries to pop the first element.
    // Unlike ThreadSafeList, this type is expected to cooperate with external
    // waiting code that holds `cond_var_mutex_` and waits on `cond_var_any_`.
    // Here we only protect the internal list with our own mutex_.
    bool try_pop_front(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (list_.empty()) {
            return false;
        }
        value = std::move(list_.front());
        list_.pop_front();
        return true;
    }

    // Lightweight empty check, protected by the internal mutex.
    bool is_empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return list_.empty();
    }

    // Expose the shared synchronization primitives so external code can
    //  * lock/read with the shared mutex
    //  * wait on / notify the shared condition variable
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
