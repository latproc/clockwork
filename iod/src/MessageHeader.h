//
//  MessageHeader.hpp
//  Latproc
//
//  Created by Martin on 20/03/2017.
//
//

#ifndef MessageHeader_hpp
#define MessageHeader_hpp

#include <stdint.h>
#include <ostream>

struct MessageHeader {
    uint32_t msgid;
    uint32_t dest;
    uint32_t source;
    uint64_t start_time;
    uint64_t arrival_time;
    uint32_t options;

    static const int NEED_REPLY;

    static const int SOCK_REMOTE;
    static const int SOCK_CW;
    static const int SOCK_CHAN;
    static const int SOCK_CTRL;

    MessageHeader();
    MessageHeader(uint32_t dst, uint32_t src, bool need_reply);
    std::ostream &operator<<(std::ostream &out) const;

    void needReply(bool needs);
    bool needsReply();
    void reply(); // switch message send/receive
    uint32_t getId() { return msgid; }
    static uint32_t last_id;
};

std::ostream &operator<<(std::ostream &out, const MessageHeader &);

#endif /* MessageHeader_hpp */
