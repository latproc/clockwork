#include <ThreadSafeQueue.h>
#include <iostream>
#include "MultiQueueMonitor.h"

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
    std::vector<ThreadSafeQueue<int>> queues;
    ThreadSafeQueue<int> queue1;
    ThreadSafeQueue<int> queue2;
    ThreadSafeQueue<int> queue3;
    queues.emplace_back(queue1);
    queues.emplace_back(queue2);
    queues.emplace_back(queue3);
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

