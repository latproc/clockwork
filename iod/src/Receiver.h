//
// Created by Martin Leadbeater on 28/8/2022.
//

#ifndef LATPROC_RECEIVER_H
#define LATPROC_RECEIVER_H

#include "Message.h"

class Receiver : public Transmitter {
public:
    Receiver(CStringHolder name_str) : Transmitter(name_str), has_work(false) {}
    virtual bool receives(const Message &, Transmitter *t) = 0;
    virtual void handle(const Message &, Transmitter *from, bool needs_receipt = false) = 0;
    virtual void enqueue(const Package &package);
    bool hasMail() { return !mail_queue.empty(); }
    bool hasPending(const Message &msg);
    long getId() const { return id; }

protected:
    std::list<Package> mail_queue;
    static boost::mutex q_mutex;
    bool has_work;

    static std::set<Receiver *> working_machines; // machines that have non-empty work queues
};

#endif //LATPROC_RECEIVER_H
