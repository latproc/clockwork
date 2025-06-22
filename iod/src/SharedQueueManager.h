#pragma once

#include "ThreadSafeQueue.h"
#include <unordered_map>

struct IQueue {
    virtual ~IQueue() = default;
};

template<typename T>
class QueueWrapper : public IQueue {
public:
    SharedThreadSafeQueue<T> queue;

    template<typename... Args>
    QueueWrapper(Args&&... args) : queue(std::forward<Args>(args)...) {}
};

class SharedQueueManager {
public:
    SharedQueueManager() = default;
    ~SharedQueueManager() = default;

    template<typename T, typename... Args>
    void create(const std::string& name, Args&&... args) {
        queues[name] = std::make_unique<QueueWrapper<T>>(std::forward<Args>(args)...);
    }


    template<typename T>
    void remove(const std::string &name) {
        std::lock_guard<std::mutex> lock(mutex);
        queues.erase(name);
    }

    template <typename T>
    SharedThreadSafeQueue<T> &get(const std::string &name) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = queues.find(name);
        if (it != queues.end()) {
            auto *wrapper = dynamic_cast<QueueWrapper<T> *>(it->second.get());
            if (wrapper) {
                    return wrapper->queue;
            } else {
                    throw std::runtime_error("Queue type mismatch for name: " + name);
            }
        } else {
            throw std::runtime_error("Queue not found: " + name);
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        queues.clear();
    }

private:
    std::unordered_map<std::string, std::unique_ptr<IQueue>> queues;
    std::mutex mutex; // Mutex for thread-safe access to the queues
};
