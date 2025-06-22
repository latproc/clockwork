#include <gtest/gtest.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <string>
#include "ThreadSafeQueue.h"
#include <boost/thread.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <zmq.hpp>
#include "Message.h"
#include "SharedQueueManager.h"

template <typename T>
class MyThread {
public:
    MyThread(ThreadSafeQueue<T>& queue) : queue(queue) {}

    virtual void run() = 0;
    ThreadSafeQueue<T>& queue;
};

class MyIntThread : public MyThread<int> {
public:
    MyIntThread(ThreadSafeQueue<int>& queue) : MyThread<int>(queue) {}

    void run() override {
        for (int i = 0; i < 1000; ++i) {
            queue.enqueue(i);
            usleep(1);
        }
    }
};

class MyVectorThread {
public:
    MyVectorThread(ThreadSafeQueue<std::vector<int>>& queue) : queue(queue) {}

    void run() {
        for (int i = 0; i < 1000; ++i) {
            std::vector<int> v = {i, i + 1, i + 2};
            queue.enqueue(v);
            if (random() % 2) { usleep(1); }
        }
    }

    ThreadSafeQueue<std::vector<int>>& queue;
};

class MyMessageThread : public MyThread<Message> {
public:
    MyMessageThread(ThreadSafeQueue<Message>& queue) : MyThread<Message>(queue) {}

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
    MyBundleThread(ThreadSafeQueue<Bundle>& queue) : MyThread<Bundle>(queue) {}

    void run() override {
        for (int i = 0; i < 1000; ++i) {
            Message m(std::to_string(i).c_str());
            Bundle b(nullptr, nullptr, std::move(m));
            queue.enqueue(b);
            if (random() % 2) { usleep(1); }
        }
    }
};

TEST(ThreadSafeQueueTest, enqueueDequeue)  {
    ThreadSafeQueue<int> queue;
    queue.enqueue(1);
    queue.enqueue(2);

    int value;
    if (queue.try_dequeue(value)) {
		EXPECT_EQ(1, value);
    }

    if (queue.try_dequeue(value)) {
        EXPECT_EQ(2, value);
    }
}

TEST(ThreadSafeQueueTest, multiThreaded) {
    ThreadSafeQueue<int> queue;
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
    while (queue.try_dequeue(value)) { ++count; }
    EXPECT_EQ(count, 3000);
}

TEST(ThreadSafeQueueTest, WorksWithNodeTypeVectorOfInt) {
    ThreadSafeQueue<std::vector<int>> queue;
    std::vector<int> v1 = {1, 2, 3};
    std::vector<int> v2 = {4, 5, 6};
    queue.enqueue(v1);
    queue.enqueue(v2);

    std::vector<int> result;
    if (queue.try_dequeue(result)) {
        EXPECT_EQ(v1, result);
    }

    if (queue.try_dequeue(result)) {
        EXPECT_EQ(v2, result);
    }
}

TEST(ThreadSafeQueueTest, WorksWithNodeTypeString) {
    ThreadSafeQueue<std::string> queue;
    std::string s1 = "hello";
    std::string s2 = "world";
    queue.enqueue(s1);
    queue.enqueue(s2);

    std::string result;
    EXPECT_TRUE(queue.try_dequeue(result));
    EXPECT_EQ(s1, result);
    EXPECT_TRUE(queue.try_dequeue(result));
    EXPECT_EQ(s2, result);
}

TEST(ThreadSafeQueueTest, WorksWithNodeTypeVectorMultithreaded) {
    ThreadSafeQueue<std::vector<int>> queue;
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
    while (queue.try_dequeue(result)) { ++count; }
    EXPECT_EQ(count, 3000);
}

TEST(ThreadSafeQueueTest, WorksWithNodeTypeMessage) {
    ThreadSafeQueue<Message> queue;
    Message m1("hello");
    Message m2("world");
    queue.enqueue(m1);
    queue.enqueue(m2);

    Message result;
    EXPECT_TRUE(queue.try_dequeue(result));
    EXPECT_EQ(m1, result);
    EXPECT_TRUE(queue.try_dequeue(result));
    EXPECT_EQ(m2, result);
}

TEST(ThreadSafeQueueTest, WorksWithNodeTypeMessageMultithreaded) {
    ThreadSafeQueue<Message> queue;
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
    while (queue.try_dequeue(result)) { ++count; }
    EXPECT_EQ(count, 3000);
}

TEST(ThreadSafeQueueTest, WorksWithNodeTypeBundle) {
    ThreadSafeQueue<Bundle> queue;
    Message m1("hello");
    Message m2("world");
    Bundle b1(nullptr, nullptr, std::move(m1));
    Bundle b2(nullptr, nullptr, std::move(m2));
    queue.enqueue(b1);
    queue.enqueue(b2);

    Bundle result;
    EXPECT_TRUE(queue.try_dequeue(result));
    EXPECT_EQ(b1, result);
    EXPECT_TRUE(queue.try_dequeue(result));
    EXPECT_EQ(b2, result);
}

template <typename T>
class QueueManager {
public:
    QueueManager(std::condition_variable_any& owner_cond,
                 std::vector<SharedThreadSafeQueue<T>*>& queues,
                 boost::condition_variable_any& cv_any,
                 boost::shared_mutex& cv_mutex)
        : owner_cond(owner_cond), queues_(queues), cond_var_(cv_any),
          cond_var_mutex_(cv_mutex), stop_(false) {}

    void stop() {
        {
            std::unique_lock<std::mutex> lock(stop_mutex_);
            stop_ = true;
        }
        cond_var_.notify_all();
    }

    void process() {
        owner_cond.notify_all(); // Let the owner know we are ready
        while (true) {
            boost::shared_lock<boost::shared_mutex> lock(cond_var_mutex_);
            cond_var_.wait(lock, [this] { return stop_ || any_non_empty(); });
            if (stop_) {
                break;
            }
            for (auto& queue : queues_) {
                T value;
                if (queue->try_dequeue(value)) {
                    std::cout << "dequeued: " << value << std::endl;
                    ++count_;
                }
            }
            owner_cond.notify_all();
        }
    }

    int count() {
        return count_;
    }

private:
    bool any_non_empty() {
        for (auto& queue : queues_) {
            if (!queue->is_empty()) {
                return true;
            }
        }
        return false;
    }

    std::condition_variable_any &owner_cond;
    std::vector<SharedThreadSafeQueue<T>*>& queues_;
    boost::condition_variable_any& cond_var_;
    boost::shared_mutex& cond_var_mutex_;
    bool stop_;
    std::mutex stop_mutex_;
    int count_ = 0;
};


TEST(SharedThreadSafeQueueTest, NotifiesSharedConditionVariable) {

    boost::condition_variable_any cond_var_any_;
    boost::shared_mutex cond_var_mutex_;
    SharedThreadSafeQueue<std::string> queue(cond_var_any_, cond_var_mutex_);
    SharedThreadSafeQueue<std::string> queue2(cond_var_any_, cond_var_mutex_);
    std::vector<SharedThreadSafeQueue<std::string>*> queues = {&queue, &queue2};

    std::condition_variable_any test_cond;
    std::mutex test_mutex;
    QueueManager<std::string> manager(test_cond, queues, cond_var_any_, cond_var_mutex_);
    std::thread manager_thread(&QueueManager<std::string>::process, &manager);
    {
        std::unique_lock<std::mutex> lock(test_mutex);
        test_cond.wait(lock);
    }
    std::string s1 = "hello";
    std::string s2 = "world";
    queue.enqueue(s1);
    queue2.enqueue(s2);
    {
        std::unique_lock<std::mutex> lock(test_mutex);
        test_cond.wait(lock, [&manager] { return manager.count() == 2; });
    }
    manager.stop();
    manager_thread.join();
    EXPECT_EQ(manager.count(), 2);
}

class ZmqNotifier {
public:
    ZmqNotifier(zmq::context_t& context, const std::string& endpoint)
        : socket_(context, ZMQ_PUSH) {
        socket_.connect(endpoint);
    }

    void operator()() const {
        zmq::message_t msg("non_empty", 9);
        socket_.send(msg, ZMQ_DONTWAIT);
    }

private:
    mutable zmq::socket_t socket_;
};

TEST(SharedThreadSafeQueueTest, NotifiesZmq) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_PULL);
    socket.bind("inproc://test_queue");

    ZmqNotifier notify_zmq(context, "inproc://test_queue");
    auto notifier = [&notify_zmq]() {
        notify_zmq();
    };

    boost::condition_variable_any cond_var_any_;
    boost::shared_mutex cond_var_mutex_;
    SharedThreadSafeQueue<int> queue(cond_var_any_, cond_var_mutex_,
         boost::optional<ThreadSafeQueue<int>::Notifier>(notifier));
    queue.enqueue(1);

    zmq::message_t msg;
    EXPECT_TRUE(socket.recv(&msg));
    EXPECT_EQ(std::string(static_cast<char*>(msg.data()), msg.size()), "non_empty");

    int value;
    EXPECT_TRUE(queue.try_dequeue(value));
    EXPECT_EQ(value, 1);
}

TEST(SharedThreadSafeQueueTest, NotifiesZmqWithLazyNotifier) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_PULL);
    socket.bind("inproc://test_queue");

    boost::condition_variable_any cond_var_any_;
    boost::shared_mutex cond_var_mutex_;
    SharedThreadSafeQueue<int> queue(cond_var_any_, cond_var_mutex_);

    ZmqNotifier notify_zmq(context, "inproc://test_queue");
    queue.set_notifier([&notify_zmq]() { notify_zmq(); });
    queue.enqueue(1);

    zmq::message_t msg;
    EXPECT_TRUE(socket.recv(&msg));
    EXPECT_EQ(std::string(static_cast<char*>(msg.data()), msg.size()), "non_empty");

    int value;
    EXPECT_TRUE(queue.try_dequeue(value));
    EXPECT_EQ(value, 1);
}

TEST(SharedThreadSafeQueueTest, NotifiesToZmqFromMultipleQueues) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_PULL);
    socket.bind("inproc://test_queue");

    ZmqNotifier notify_zmq(context, "inproc://test_queue");
    auto notifier = [&notify_zmq]() {
        notify_zmq();
    };

    boost::condition_variable_any cond_var_any_;
    boost::shared_mutex cond_var_mutex_;
    SharedThreadSafeQueue<int> queue(cond_var_any_, cond_var_mutex_,
         boost::optional<ThreadSafeQueue<int>::Notifier>(notifier));
    SharedThreadSafeQueue<int> queue2(cond_var_any_, cond_var_mutex_,
         boost::optional<ThreadSafeQueue<int>::Notifier>(notifier));

    queue.enqueue(1);

    zmq::message_t msg;
    EXPECT_TRUE(socket.recv(&msg));
    EXPECT_EQ(std::string(static_cast<char*>(msg.data()), msg.size()), "non_empty");

    queue2.enqueue(2);
    EXPECT_TRUE(socket.recv(&msg));
    EXPECT_EQ(std::string(static_cast<char*>(msg.data()), msg.size()), "non_empty");

    int value;
    EXPECT_TRUE(queue.try_dequeue(value));
    EXPECT_EQ(value, 1);

    EXPECT_TRUE(queue2.try_dequeue(value));
    EXPECT_EQ(value, 2);
}

// Shared Queue Manager
//

TEST(SharedQueueManagerTest, CanQueueAndDequeueWhenGettingQueueFromManager) {
    SharedQueueManager manager;
    boost::condition_variable_any cond_var_any_;
    boost::shared_mutex cond_var_mutex_;
    manager.create<int>("test_queue", cond_var_any_, cond_var_mutex_);
    auto & queue = manager.get<int>("test_queue");

    queue.enqueue(1);
    queue.enqueue(2);

    int value;
    EXPECT_TRUE(queue.try_dequeue(value));
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(queue.try_dequeue(value));
    EXPECT_EQ(value, 2);
}

TEST(SharedQueueManagerTest, CanCreateAndGetMultipleQueues) {
    SharedQueueManager manager;
    boost::condition_variable_any cond_var_any_1;
    boost::shared_mutex cond_var_mutex_1;
    boost::condition_variable_any cond_var_any_2;
    boost::shared_mutex cond_var_mutex_2;

    manager.create<int>("int_queue", cond_var_any_1, cond_var_mutex_1);
    manager.create<std::string>("string_queue", cond_var_any_2, cond_var_mutex_2);

    auto & int_queue = manager.get<int>("int_queue");
    auto & string_queue = manager.get<std::string>("string_queue");

    int_queue.enqueue(42);
    string_queue.enqueue("hello");

    int int_value;
    std::string str_value;

    EXPECT_TRUE(int_queue.try_dequeue(int_value));
    EXPECT_EQ(int_value, 42);
    EXPECT_TRUE(string_queue.try_dequeue(str_value));
    EXPECT_EQ(str_value, "hello");
}

TEST(SharedQueueManagerTest, ThrowsOnQueueNotFound) {
    SharedQueueManager manager;
    boost::condition_variable_any cond_var_any_;
    boost::shared_mutex cond_var_mutex_;
    manager.create<int>("test_queue", cond_var_any_, cond_var_mutex_);

    EXPECT_THROW(manager.get<std::string>("test_queue"), std::runtime_error);
}

TEST(SharedQueueManagerTest, ThrowsOnQueueTypeMismatch) {
    SharedQueueManager manager;
    boost::condition_variable_any cond_var_any_;
    boost::shared_mutex cond_var_mutex_;
    manager.create<int>("test_queue", cond_var_any_, cond_var_mutex_);

    EXPECT_THROW(manager.get<std::string>("test_queue"), std::runtime_error);
}

TEST(SharedQueueManagerTest, CanRemoveQueue) {
    SharedQueueManager manager;
    boost::condition_variable_any cond_var_any_;
    boost::shared_mutex cond_var_mutex_;
    manager.create<int>("test_queue", cond_var_any_, cond_var_mutex_);

    auto & queue = manager.get<int>("test_queue");
    queue.enqueue(1);

    int value;
    EXPECT_TRUE(queue.try_dequeue(value));
    EXPECT_EQ(value, 1);

    manager.remove<int>("test_queue");

    EXPECT_THROW(manager.get<int>("test_queue"), std::runtime_error);
}
