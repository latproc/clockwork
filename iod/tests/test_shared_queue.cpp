#include <gtest/gtest.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <string>
#include "SharedQueue.h"
#include "Message.h"

template <typename T>
class MyThread {
public:
    MyThread(SharedQueue<T>& queue) : queue(queue) {}

    virtual void run() = 0;
    SharedQueue<T>& queue;
};

class MyIntThread : public MyThread<int> {
public:
    MyIntThread(SharedQueue<int>& queue) : MyThread<int>(queue) {}

    void run() override {
        for (int i = 0; i < 1000; ++i) {
            queue.enqueue(i);
            usleep(1);
        }
    }
};

class MyVectorThread {
public:
    MyVectorThread(SharedQueue<std::vector<int>>& queue) : queue(queue) {}

    void run() {
        for (int i = 0; i < 1000; ++i) {
            std::vector<int> v = {i, i + 1, i + 2};
            queue.enqueue(v);
            if (random() % 2) { usleep(1); }
        }
    }

    SharedQueue<std::vector<int>>& queue;
};

class MyMessageThread : public MyThread<Message> {
public:
    MyMessageThread(SharedQueue<Message>& queue) : MyThread<Message>(queue) {}

    void run() override {
        for (int i = 0; i < 1000; ++i) {
            Message m(std::to_string(i).c_str());
            queue.enqueue(m);
            if (random() % 2) { usleep(1); }
        }
    }
};

class MyBundleThread : public MyThread<Bundle> {
public:
    MyBundleThread(SharedQueue<Bundle>& queue) : MyThread<Bundle>(queue) {}

    void run() override {
        for (int i = 0; i < 1000; ++i) {
            Message m(std::to_string(i).c_str());
            Bundle b(nullptr, nullptr, std::move(m));
            queue.enqueue(b);
            if (random() % 2) { usleep(1); }
        }
    }
};

TEST(SharedQueueTest, enqueueDequeue)  {
    SharedQueue<int> queue;
    queue.enqueue(1);
    queue.enqueue(2);

    int value;
    if (queue.dequeue(value)) {
		EXPECT_EQ(1, value);
    }

    if (queue.dequeue(value)) {
        EXPECT_EQ(2, value);
    }
}

TEST(SharedQueueTest, multiThreaded) {
    SharedQueue<int> queue;
    MyIntThread thread(queue);
    std::thread t1(&MyIntThread::run, &thread);
    std::thread t2(&MyIntThread::run, &thread);

    for (int i = 0; i < 1000; ++i) {
        queue.enqueue(i);
        usleep(1);
    }

    t1.join();
    t2.join();

    int value;
    int count = 0;
    while (queue.dequeue(value)) { ++count; }
    EXPECT_EQ(count, 3000);
}

TEST(SharedQueueTest, WorksWithNodeTypeVectorOfInt) {
    SharedQueue<std::vector<int>> queue;
    std::vector<int> v1 = {1, 2, 3};
    std::vector<int> v2 = {4, 5, 6};
    queue.enqueue(v1);
    queue.enqueue(v2);

    std::vector<int> result;
    if (queue.dequeue(result)) {
        EXPECT_EQ(v1, result);
    }

    if (queue.dequeue(result)) {
        EXPECT_EQ(v2, result);
    }
}

TEST(SharedQueueTest, WorksWithNodeTypeString) {
    SharedQueue<std::string> queue;
    std::string s1 = "hello";
    std::string s2 = "world";
    queue.enqueue(s1);
    queue.enqueue(s2);

    std::string result;
    if (queue.dequeue(result)) {
        EXPECT_EQ(s1, result);
    }

    if (queue.dequeue(result)) {
        EXPECT_EQ(s2, result);
    }
}

TEST(SharedQueueTest, WorksWithNodeTypeVectorMultithreaded) {
    SharedQueue<std::vector<int>> queue;
    MyVectorThread thread(queue);
    std::thread t1(&MyVectorThread::run, &thread);
    std::thread t2(&MyVectorThread::run, &thread);

    for (int i = 0; i < 1000; ++i) {
        std::vector<int> v = {i, i + 1, i + 2};
        queue.enqueue(v);
        if (random() % 2) { usleep(1); }
    }

    t1.join();
    t2.join();

    std::vector<int> result;
    int count = 0;
    while (queue.dequeue(result)) { ++count; }
    EXPECT_EQ(count, 3000);
}

TEST(SharedQueueTest, WorksWithNodeTypeMessage) {
    SharedQueue<Message> queue;
    Message m1("hello");
    Message m2("world");
    queue.enqueue(m1);
    queue.enqueue(m2);

    Message result;
    if (queue.dequeue(result)) {
        EXPECT_EQ(m1, result);
    }

    if (queue.dequeue(result)) {
        EXPECT_EQ(m2, result);
    }
}

TEST(SharedQueueTest, WorksWithNodeTypeMessageMultithreaded) {
    SharedQueue<Message> queue;
    MyMessageThread thread(queue);
    std::thread t1(&MyThread<Message>::run, &thread);
    std::thread t2(&MyThread<Message>::run, &thread);

    for (int i = 0; i < 1000; ++i) {
        Message m(std::to_string(i).c_str());
        queue.enqueue(m);
        if (random() % 2) { usleep(1); }
    }

    t1.join();
    t2.join();

    Message result;
    int count = 0;
    while (queue.dequeue(result)) { ++count; }
    EXPECT_EQ(count, 3000);
}

TEST(SharedQueueTest, WorksWithNodeTypeBundle) {
    SharedQueue<Bundle> queue;
    Message m1("hello");
    Message m2("world");
    Bundle b1(nullptr, nullptr, std::move(m1));
    Bundle b2(nullptr, nullptr, std::move(m2));
    queue.enqueue(b1);
    queue.enqueue(b2);

    Bundle result;
    if (queue.dequeue(result)) {
        EXPECT_EQ(b1, result);
    }

    if (queue.dequeue(result)) {
        EXPECT_EQ(b2, result);
    }
}
