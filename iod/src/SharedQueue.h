#pragma once

#include <atomic>
#include <memory>
#include <iostream>
#include <type_traits>

template <typename T>
class SharedQueue {
    static_assert(std::is_copy_constructible<T>::value, "T must be copy constructible");
    static_assert(std::is_copy_assignable<T>::value, "T must be copy assignable");
    static_assert(std::is_default_constructible<T>::value, "T must be default constructible");
    static_assert(!std::is_destructible<T>::value || std::is_nothrow_destructible<T>::value, "T must have a non-throwing destructor");
public:
    SharedQueue() {
        Node* dummy = new Node(T());
        head.store(dummy);
        tail.store(dummy);
    }

    ~SharedQueue() {
        while (Node* node = head.load()) {
            head.store(node->next);
            delete node;
        }
    }

    void enqueue(const T& value) {
        Node* newNode = new Node(value);
        Node* oldTail = nullptr;

        while (true) {
            oldTail = tail.load();
            Node* next = oldTail->next.load();
            if (oldTail == tail.load()) {
                if (next == nullptr) {
                    if (oldTail->next.compare_exchange_weak(next, newNode)) {
                        break;
                    }
                } else {
                    tail.compare_exchange_weak(oldTail, next);
                }
            }
        }
        tail.compare_exchange_weak(oldTail, newNode);
    }

    bool dequeue(T& result) {
        Node* oldHead = nullptr;

        while (true) {
            oldHead = head.load();
            Node* oldTail = tail.load();
            Node* next = oldHead->next.load();

            if (oldHead == head.load()) {
                if (oldHead == oldTail) {
                    if (next == nullptr) {
                        return false;  // Queue is empty
                    }
                    tail.compare_exchange_weak(oldTail, next);
                } else {
                    result = next->value;
                    if (head.compare_exchange_weak(oldHead, next)) {
                        break;
                    }
                }
            }
        }
        delete oldHead;
        return true;
    }

private:
    struct Node {
        T value;
        std::atomic<Node*> next;

        Node(const T& val) : value(val), next(nullptr) {}
    };

    std::atomic<Node*> head;
    std::atomic<Node*> tail;
};

