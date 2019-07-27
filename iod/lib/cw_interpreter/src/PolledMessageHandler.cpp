
#include "PolledMessageHandler.h"
#include <zmq.hpp>

std::list<PolledMessageHandler *>PolledMessageHandler::handlers;
zmq::pollitem_t *PolledMessageHandler::poll_items = 0;
bool PolledMessageHandler::changed = false; // indicates whether the cached poll_items is valid


PolledMessageHandler::PolledMessageHandler(zmq::socket_t &socket, bool is_enabled) : sock(socket), enabled(is_enabled) {
    
}

PolledMessageHandler::~PolledMessageHandler() {
    
}

void PolledMessageHandler::enlist(zmq::socket_t &socket) {
    handlers.push_back(new PolledMessageHandler(socket));
    changed = true;
}

void PolledMessageHandler::delist(zmq::socket_t &socket) {
    std::list<PolledMessageHandler *>::iterator iter = handlers.begin();
    while (iter != handlers.end()) {
        PolledMessageHandler* pmh = *iter;
        if (&pmh->sock == &socket) { handlers.erase(iter); delete pmh; changed = true; return; }
    }
}

void PolledMessageHandler::enable(zmq::socket_t &socket) {
    std::list<PolledMessageHandler *>::iterator iter = handlers.begin();
    while (iter != handlers.end()) {
        PolledMessageHandler* pmh = *iter++;
        if (&pmh->sock == &socket) { pmh->enabled = true; changed = true; return; }
    }
}

void PolledMessageHandler::disable(zmq::socket_t &socket){
    std::list<PolledMessageHandler *>::iterator iter = handlers.begin();
    while (iter != handlers.end()) {
        PolledMessageHandler* pmh = *iter++;
        if (&pmh->sock == &socket) { pmh->enabled = false; changed = true; return; }
    }
}

zmq::pollitem_t * PolledMessageHandler::getZMQPollItems(zmq::pollitem_t *items) {
    if (handlers.empty()) return items;
    
    // use the existing item list if provided
    zmq::pollitem_t * res = items;
    if (res == NULL) res = new zmq::pollitem_t[handlers.size()];
    
    memset(res, 0, sizeof(zmq::pollitem_t) * handlers.size());
    
    std::list<PolledMessageHandler *>::iterator iter = handlers.begin();
    int idx = 0;
    while (iter != handlers.end()) {
        PolledMessageHandler* pmh = *iter++;
        if (pmh->enabled) res[idx++].socket = (void*)pmh->sock;
    }
    if (poll_items) delete poll_items;
    poll_items = res;
    changed = false;
    return res;
}

int PolledMessageHandler::poll(zmq::pollitem_t *items, int num, long timeout) {
    return zmq::poll(items, num, timeout);
}




