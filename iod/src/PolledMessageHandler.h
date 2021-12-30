#ifndef __cw_polledmessagehandler__
#define __cw_polledmessagehandler__

#include <list>
#include <string.h>
#include <zmq.hpp>

class PolledMessageHandler {

    PolledMessageHandler(zmq::socket_t &socket, bool enabled = true);
    ~PolledMessageHandler();

  public:
    zmq::socket_t &sock;
    bool enabled;

    static std::list<PolledMessageHandler *> handlers;
    static void enlist(zmq::socket_t &socket);
    static void delist(zmq::socket_t &socket);
    static void enable(zmq::socket_t &socket);
    static void disable(zmq::socket_t &socket);

    static size_t size(); // returns the number of registered items
    static zmq::pollitem_t *getZMQPollItems(zmq::pollitem_t *);
    static zmq::socket_t *getSocket(unsigned int which);

    static int poll(zmq::pollitem_t *items, int num, long timeout);

  private:
    static zmq::pollitem_t *poll_items; // TBD this is not the right way to share poll items...
    static bool changed;
};

#endif
