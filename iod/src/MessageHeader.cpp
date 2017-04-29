//
//  MessageHeader.cpp
//  Latproc
//
//  Created by Martin on 20/03/2017.
//
//

#include "MessageHeader.h"
#include "value.h"

const int MessageHeader::NEED_REPLY = 1;

const int MessageHeader::SOCK_REMOTE = 9;
const int MessageHeader::SOCK_CW = 1;
const int MessageHeader::SOCK_CHAN = 2;
const int MessageHeader::SOCK_CTRL = 3;

uint32_t MessageHeader::last_id = 0;
static uint64_t first_message_time = 0;

std::ostream &MessageHeader::operator<<(std::ostream &out) const  {
	out << (start_time-first_message_time) << " " << dest<<":" << source<<":" << options;
	return out;
}

std::ostream &operator<<(std::ostream &out, const MessageHeader&mh) {
	return mh.operator<<(out);
}


MessageHeader::MessageHeader(uint32_t dst, uint32_t src, bool need_reply)
: msgid(++last_id), dest(dst), source(src), start_time(microsecs()), arrival_time(0), options(0)
{
	if (first_message_time == 0) first_message_time = start_time;
	if (need_reply) options |= NEED_REPLY;
}

MessageHeader::MessageHeader() : msgid(++last_id), dest(0), source(0), start_time(microsecs()), arrival_time(0), options(0){
	if (first_message_time == 0) first_message_time = start_time;
}

void MessageHeader::needReply(bool needs) {
	if (needs) options|=NEED_REPLY;
	else options &= (-1 ^ NEED_REPLY);
}

bool MessageHeader::needsReply() {
	return ((options & NEED_REPLY) == NEED_REPLY);
}

void MessageHeader::reply() {
	uint32_t tmp = dest;
	dest = source;
	source = tmp;
}

