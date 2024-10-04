#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <thread>
#include <iostream>
#include <boost/optional/optional.hpp>
#include "ThreadSafeQueue.h"

template <typename T>
class MultiQueueMonitor {
public:
    MultiQueueMonitor(std::vector<ThreadSafeQueue<T>>& queues)
        : queues_(queues), stop_(false) {}

    void start() {
        monitor_thread_ = std::thread([this] { this->monitor(); });
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(stop_mutex_);
            stop_ = true;
        }
        stop_cond_.notify_all();
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }

    bool dequeue_from_any(T& value) {
        std::unique_lock<std::mutex> lock(data_mutex_);
        cond_var_.wait(lock, [this] { return !data_queue_.empty() || stop_; });
        if (stop_) {
            return false;
        }
        value = std::move(data_queue_.front());
        data_queue_.pop();
        return true;
    }

private:
    void monitor() {
        while (true) {
            {
                std::lock_guard<std::mutex> lock(stop_mutex_);
                if (stop_) {
                    return;
                }
            }

            for (auto& queue : queues_) {
                boost::optional<T> value = queue.try_dequeue();
                if (value.has_value()) {
                    {
                        std::lock_guard<std::mutex> lock(data_mutex_);
                        data_queue_.push(std::move(value.value()));
                    }
                    cond_var_.notify_one();
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Polling interval
        }
    }

    std::vector<ThreadSafeQueue<T>>& queues_;
    std::queue<T> data_queue_;
    std::mutex data_mutex_;
    std::condition_variable cond_var_;
    std::thread monitor_thread_;
    bool stop_;
    std::mutex stop_mutex_;
    std::condition_variable stop_cond_;
};

void producer(ThreadSafeQueue<int>& queue, int start, int count) {
    for (int i = 0; i < count; ++i) {
        queue.enqueue(start + i);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void consumer(MultiQueueMonitor<int>& monitor) {
    while (true) {
        int value;
        if (!monitor.dequeue_from_any(value)) {
            break;
        }
        std::cout << "Consumer got: " << value << std::endl;
    }
}

int main() {
    ThreadSafeQueue<int> queue1;
    ThreadSafeQueue<int> queue2;
    ThreadSafeQueue<int> queue3;
    std::vector<ThreadSafeQueue<int>> queues = {queue1, queue2, queue3};
    MultiQueueMonitor<int> monitor(queues);

    std::thread producer1(producer, std::ref(queue1), 0, 10);
    std::thread producer2(producer, std::ref(queue2), 100, 10);
    std::thread producer3(producer, std::ref(queue3), 200, 10);
    std::thread consumer_thread(consumer, std::ref(monitor));

    monitor.start();

    producer1.join();
    producer2.join();
    producer3.join();

    monitor.stop();
    consumer_thread.join();

    return 0;
}

