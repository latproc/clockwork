/*
    Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

    This file is part of Latproc

    Latproc is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    Latproc is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Latproc; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "SyncRemoteStatesAction.h"
#include "Channel.h"
#include "DebugExtra.h"
#include "IOComponent.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageEncoding.h"
#include "MessageLog.h"

Action *SyncRemoteStatesActionTemplate::factory(MachineInstance *mi) {
    return new SyncRemoteStatesAction(mi, *this);
}

class SyncRemoteStatesActionInternals {
  public:
    std::list<char *> messages; // encoded messages (free() required on completion)
    MessageHeader header;
    std::list<char *>::iterator *iter;
    Channel *chn;
    zmq::socket_t *sock;

    std::string sockurl;

    enum MessageState { e_sending, e_receiving, e_done } message_state;
    enum ProcessState { ps_init, ps_sending_messages, ps_waiting_ack } process_state;

    SyncRemoteStatesActionInternals()
        : header(MessageHeader::SOCK_CW, MessageHeader::SOCK_CHAN, false), iter(0), chn(0), sock(0),
          message_state(e_sending), process_state(ps_init) {
        //(ChannelInternals::SOCK_CHAN, ChannelInternals::SOCK_CHAN, true);
    }
};

SyncRemoteStatesActionTemplate::SyncRemoteStatesActionTemplate(Channel *channel, zmq::socket_t *s)
    : chn(channel), sock(s) {}

SyncRemoteStatesAction::SyncRemoteStatesAction(MachineInstance *mi,
                                               SyncRemoteStatesActionTemplate &t)
    : Action(mi) {
    internals = new SyncRemoteStatesActionInternals;
    internals->sock = t.sock;
    internals->chn = t.chn;
}

Action::Status SyncRemoteStatesAction::execute() {
    char buf[200];
    Channel *chn = dynamic_cast<Channel *>(owner);
    if (internals->process_state == SyncRemoteStatesActionInternals::ps_init) {
        //snprintf(buf, 200, "%s Sync remote states - init", chn->getName().c_str());
        //MessageLog::instance()->add(buf);
        owner->start(this);
        status = Running;
#if 0
        if (!internals->sock) {
            internals->sock = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PAIR);
            internals->sock->bind("inproc://syncstates");
            chn->addSocket(10, "inproc://syncstates");
        }
#endif
        //assert(chn);
        //assert(chn == internals->chn);
        internals->process_state = SyncRemoteStatesActionInternals::ps_sending_messages;
        if (chn->syncRemoteStates(internals->messages)) {
            internals->iter = new std::list<char *>::iterator(internals->messages.begin());
            if (*internals->iter != internals->messages.end()) {
                internals->message_state = SyncRemoteStatesActionInternals::e_sending;
            }
            else {
                internals->message_state = SyncRemoteStatesActionInternals::e_done;
            }
        }
        else {
            status = Failed;
            error_str = "Failed to sync";
            owner->stop(this);
            return status;
        }
    }

    if (internals->process_state == SyncRemoteStatesActionInternals::ps_sending_messages) {
        //snprintf(buf, 200, "%s Sync remote states - process state: sending messages", chn->getName().c_str());
        //MessageLog::instance()->add(buf);
        //DBG_CHANNELS << buf << "\n";
        if (*internals->iter == internals->messages.end()) {
            //snprintf(buf, 200, "%s Sync remote states - done", chn->getName().c_str());
            //MessageLog::instance()->add(buf);
            //DBG_CHANNELS << buf << "\n";
            internals->message_state = SyncRemoteStatesActionInternals::e_done;
        }
        if (internals->message_state == SyncRemoteStatesActionInternals::e_sending) {
            char *current_message = *(*internals->iter);
            //snprintf(buf, 200, "%s Sync remote states - sending: %s", chn->getName().c_str(), current_message);
            //MessageLog::instance()->add(buf);
            //DBG_CHANNELS << buf << "\n";

            internals->header.needReply(false);
            internals->header.start_time = microsecs();
            safeSend(*internals->sock, current_message, strlen(current_message), internals->header);
            free(current_message);
            *internals->iter = internals->messages.erase(*internals->iter);
            internals->message_state = SyncRemoteStatesActionInternals::e_receiving;
            // skip receiving temporarily
            internals->message_state = SyncRemoteStatesActionInternals::e_done;
        }
        else if (internals->message_state == SyncRemoteStatesActionInternals::e_receiving) {
            char *repl;
            size_t len;
            if (safeRecv(*internals->sock, &repl, &len, false, 0, internals->header)) {
                //snprintf(buf, 200, "%s Sync remote states - received reply %s", chn->getName().c_str(), repl);
                //MessageLog::instance()->add(buf);
                //DBG_CHANNELS << buf << "\n";
                internals->message_state = SyncRemoteStatesActionInternals::e_done;
                delete[] repl;
            }
            else {
                return Running;
            }
        }
        if (internals->message_state == SyncRemoteStatesActionInternals::e_done) {
            //snprintf(buf, 200, "%s Sync remote states - message done", chn->getName().c_str());
            //MessageLog::instance()->add(buf);
            //DBG_CHANNELS << buf << "\n";
            if (*internals->iter != internals->messages.end()) {
                internals->message_state = SyncRemoteStatesActionInternals::e_sending;
            }
            else {
                if (internals->iter) {
                    delete internals->iter;
                    internals->iter = 0;
                }
            }
            if (!internals->iter) { // finished sending messages
                if (internals->process_state ==
                    SyncRemoteStatesActionInternals::ps_sending_messages) {
                    //safeSend(*cmd_client, "done", 4);
                    //std::string ack;
                    //MessageHeader mh(ChannelInternals::SOCK_CTRL, ChannelInternals::SOCK_CTRL, false);
                    //sendMessage("done", *cmd_client, ack, mh);
                    internals->header.dest = MessageHeader::SOCK_CTRL;
                    internals->header.source = MessageHeader::SOCK_CTRL;
                    internals->header.needReply(true);
                    internals->header.start_time = microsecs();
                    safeSend(*internals->sock, "done", 4, internals->header);
                    internals->process_state = SyncRemoteStatesActionInternals::ps_waiting_ack;
                }
            }
        }
    }
    else if (internals->process_state == SyncRemoteStatesActionInternals::ps_waiting_ack) {
        //snprintf(buf, 200, "%s Sync remote states - awaiting ack", chn->getName().c_str());
        //MessageLog::instance()->add(buf);
        //DBG_CHANNELS << buf << "\n";
        char *ack;
        size_t len;
        if (safeRecv(*internals->sock, &ack, &len, false, 0, internals->header)) {
            DBG_CHANNELS << "channel " << chn->getName() << " got " << ack << " from server\n";
            status = Complete;
            result_str = (const char *)ack; // force a new allocation
            delete[] ack;
            owner->stop(this);

            // execute a state change once all other actions are

            if (chn->isClient()) {
                SetStateActionTemplate ssat(CStringHolder("SELF"), "ACTIVE");
                SetStateAction *ssa = (SetStateAction *)ssat.factory(chn);
                chn->enqueueAction(ssa);
            }
            else {

                SetStateActionTemplate ssat(CStringHolder("SELF"), "DOWNLOADING");
                chn->enqueueAction(ssat.factory(chn));
            }
            return status;
        }
    }
    return status;
#if 0
    std::stringstream ss;
    ss << owner->getName() << " failed to find machine " << target.get() << " for SetState action" << std::flush;
    std::string str = ss.str();
    error_str = strdup(str.c_str());
    status = Failed;
    owner->stop(this);
    return status;
#endif
}

Action::Status SyncRemoteStatesAction::run() { return execute(); }

Action::Status SyncRemoteStatesAction::checkComplete() {
    usleep(5);
    if (status == New || status == NeedsRetry) {
        status = execute();
    }
    if (status == Suspended) {
        resume();
    }
    if (status != Running) {
        return status;
    }
    else {
        status = execute();
    }
    return status;
}

std::ostream &SyncRemoteStatesAction::operator<<(std::ostream &out) const {
    return out << "SyncRemoteStatesAction";
}
