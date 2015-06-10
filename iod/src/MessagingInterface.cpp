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

#include <assert.h>
#include "MessagingInterface.h"
#include <iostream>
#include <exception>
#include <math.h>
#include <zmq.hpp>
#include <map>
#include "Logger.h"
#include "DebugExtra.h"
#include "cJSON.h"
#ifdef DYNAMIC_VALUES
#include "dynamic_value.h"
#endif
#include "symboltable.h"
#include "options.h"
#include "anet.h"
#include "MessageLog.h"
#include "Dispatcher.h"
#include "MessageEncoding.h"
#include "Channel.h"

MessagingInterface *MessagingInterface::current = 0;
zmq::context_t *MessagingInterface::zmq_context = 0;
std::map<std::string, MessagingInterface *>MessagingInterface::interfaces;
bool MessagingInterface::abort_all = false;


zmq::context_t *MessagingInterface::getContext() { return zmq_context; }

bool safeRecv(zmq::socket_t &sock, char *buf, int buflen, bool block, size_t &response_len, uint64_t timeout) {
//    struct timeval now;
//    gettimeofday(&now, 0);
//    uint64_t when = now.tv_sec * 1000000L + now.tv_usec + timeout;
	char tnam[100];
	int pgn_rc = pthread_getname_np(pthread_self(),tnam, 100);
	assert(pgn_rc == 0);

	response_len = 0;
	while (!MessagingInterface::aborted()) {
		try {
			zmq::pollitem_t items[] = { { sock, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0 } };
			int n = zmq::poll( &items[0], 1, timeout);
			if (!n && block) continue;
			if (items[0].revents & ZMQ_POLLIN) {
				//NB_MSG << tnam << " safeRecv() collecting data\n";
				response_len = sock.recv(buf, buflen, ZMQ_NOBLOCK);
				if (response_len > 0 && response_len < buflen) {
					buf[response_len] = 0;
					//NB_MSG << tnam << " saveRecv() collected data '" << buf << "' with length " << response_len << "\n";
				}
				else {
					//NB_MSG << tnam << " saveRecv() collected data with length " << response_len << "\n";
				}
				if (!response_len && block) continue;
			}
			return (response_len == 0) ? false : true;
		}
		catch (zmq::error_t e) {
			std::cerr << tnam << " safeRecv error " << errno << " " << zmq_strerror(errno) << "\n";
			if (errno == EINTR) { std::cerr << "interrupted system call, retrying\n"; 
				if (block) continue;
			}
			usleep(10);
			return false;
		}
	}
	return false;
}

void safeSend(zmq::socket_t &sock, const char *buf, int buflen) {
	char tnam[100];
	int pgn_rc = pthread_getname_np(pthread_self(),tnam, 100);
	assert(pgn_rc == 0);
	while (!MessagingInterface::aborted()) {
		try {
			//NB_MSG << tnam << " safeSend() sending " << buf << "\n";
			sock.send(buf, buflen);
			break;
		}
		catch (zmq::error_t) {
			if (zmq_errno() == EINTR) continue;
			throw;
		}
	}
}

bool sendMessage(const char *msg, zmq::socket_t &sock, std::string &response) {
	char tnam[100];
	int pgn_rc = pthread_getname_np(pthread_self(),tnam, 100);
	assert(pgn_rc == 0);
	int retries = 3;
	assert(msg);
sendMessage_transmit:
	--retries;
	assert(retries);
	while (1) {
        try {
			if ( !(*msg) ) { NB_MSG << " Warning: sending empty message"; }
            size_t len = sock.send(msg, strlen(msg));
			if (!len) {
				if (!len) { NB_MSG << "Warning: zero bytes sent of " << strlen(msg) << "\n"; }
				continue;
			}
			NB_MSG << "sending: " << msg << " on thread " << tnam << "\n";
            break;
        }
        catch (zmq::error_t e) {
			if (errno == EINTR) {
				NB_MSG << "Warning: send was interrupted (EAGAIN)\n";
				continue;
			}
            std::cerr << "sendMessage: " << zmq_strerror(errno) << " when transmitting\n";
            return false;
        }
    }
    char *buf = 0;
    size_t len = 0;
    while (len == 0) {
        try {
            //zmq::message_t rcvd;
			char rcvbuf[200];
			len = sock.recv(rcvbuf, 200, ZMQ_NOBLOCK);
                //len = rcvd.size();
			if (!len) {
				zmq::pollitem_t items[] = { { sock, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0 } };
				zmq::poll( items, 1, 1);
				if (items[0].revents & ZMQ_POLLIN) //len = rcvd.size();
					len = sock.recv(rcvbuf, 200, ZMQ_NOBLOCK);
			}
			if (!len)
				continue;
			buf = new char[len+1];
			memcpy(buf, rcvbuf, len);
			buf[len] = 0;
			response = buf;
			delete[] buf;

            break;
        }
        catch(zmq::error_t e)  {
            if (errno == EINTR) continue;
			if (zmq_errno() == EFSM) {
				// send must have failed
				usleep(50);
				goto sendMessage_transmit;
			}
            char err[300];
			const char *fnam = strrchr(__FILE__, '/');
			if (!fnam) fnam = __FILE__; else fnam++;
            snprintf(err, 300, "sendMessage error: %s in %s:%d\n", zmq_strerror(errno), fnam, __LINE__);
            std::cerr << err;
            return false;
        }
    }
    return true;
}

void MessagingInterface::setContext(zmq::context_t *ctx) {
    zmq_context = ctx;
    assert(zmq_context);
}
/*
MessagingInterface *MessagingInterface::getCurrent() {
    if (MessagingInterface::current == 0) {
        MessagingInterface::current = new MessagingInterface(1, publisher_port());
        usleep(200000); // give current subscribers time to notice us
    }
    return MessagingInterface::current;
}
 */



MessagingInterface *MessagingInterface::create(std::string host, int port, Protocol proto) {
    std::stringstream ss;
    ss << host << ":" << port;
    std::string id = ss.str();
    if (interfaces.count(id) == 0) {
        MessagingInterface *res = new MessagingInterface(host, port, MessagingInterface::DEFERRED_START, proto);
        interfaces[id] = res;
        return res;
    }
    else
        return interfaces[id];
}

MessagingInterface::MessagingInterface(int num_threads, int port_, bool deferred_start, Protocol proto)
		: Receiver("messaging_interface"), protocol(proto), socket(0),is_publisher(false),
			connection(-1), port(port_), owner_thread(0) {
		    owner_thread = pthread_self();
		is_publisher = true;
		hostname = "*";
		std::stringstream ss;
		ss << "tcp://" << hostname << ":" << port;
		url = ss.str();
		if (protocol == eCHANNEL)
			socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PAIR);
		else
			socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PUB);

		if (!deferred_start) start();
}

void MessagingInterface::start() {
	if (protocol == eCLOCKWORK || protocol == eZMQ|| protocol == eCHANNEL) {
		owner_thread = pthread_self();
		if (hostname == "*" || hostname == "*") {
			socket->bind(url.c_str());
		}
		else {
			connect();
		}
	}
	else {
		connect();
	}
}

MessagingInterface::MessagingInterface(std::string host, int remote_port, bool deferred, Protocol proto)
		:Receiver("messaging_interface"), protocol(proto), socket(0),is_publisher(false),
            connection(-1), hostname(host), port(remote_port), owner_thread(0) {
		std::stringstream ss;
		ss << "tcp://" << host << ":" << port;
		url = ss.str();
		if (protocol == eCLOCKWORK || protocol == eZMQ || protocol == eCHANNEL) {
			if (hostname == "*" || hostname == "0.0.0.0") {
				if (protocol == eCHANNEL) {
					is_publisher = true; // TBD
					socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PAIR);
				}
				else {
					is_publisher = true;
					socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_PUB);
				}
			}
			else {
				is_publisher = false; // TBD need to support acks
				socket = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REQ);
			}
		}
		else {
			NB_MSG << "Warning: unexpected protocl constructing a messaging interface\n";
		}

		if (!deferred) start();
}

int MessagingInterface::uniquePort(unsigned int start, unsigned int end) {
    int res = 0;
    char address_buf[40];
    std::set<unsigned int> checked_ports; // used to avoid duplicate checks and infinite loops
    while (true) {
        try{
            zmq::socket_t test_bind(*MessagingInterface::getContext(), ZMQ_PULL);
            res = random() % (end-start+1) + start;
            if (checked_ports.count(res) && checked_ports.size() < end-start+1) continue;
            checked_ports.insert(res);
            snprintf(address_buf, 40, "tcp://0.0.0.0:%d", res);
            test_bind.bind(address_buf);
            int linger = 0; // do not wait at socket close time
            test_bind.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
            test_bind.disconnect(address_buf);
            std::cerr << "found available port " << res << "\n";
            break;
        }
        catch (zmq::error_t err) {
            if (zmq_errno() != EADDRINUSE) {
                res = 0;
                break;
            }
            if (checked_ports.size() == end-start+1) {
                res = 0;
                break;
            }
        }
    }
    return res;
}


void MessagingInterface::connect() {
	if (protocol == eCLOCKWORK || protocol == eZMQ || protocol == eCHANNEL) {
        assert( pthread_equal(owner_thread, pthread_self()) );
	    socket->connect(url.c_str());
	    int linger = 0;
	    socket->setsockopt (ZMQ_LINGER, &linger, sizeof (linger));
	}
	else {
		char error[ANET_ERR_LEN];
    	connection = anetTcpConnect(error, hostname.c_str(), port);
    	if (connection == -1) {
        	MessageLog::instance()->add(error);
    	    std::cerr << error << "\n";
    	}
	}
}

MessagingInterface::~MessagingInterface() {
	int retries = 3;
	while  (retries-- && connection != -1) { 
		int err = close(connection);
		if (err == -1 && errno == EINTR) continue;
		connection = -1;
	}
    if (MessagingInterface::current == this) MessagingInterface::current = 0;
    socket->disconnect(url.c_str());
    delete socket;
}

bool MessagingInterface::receives(const Message&, Transmitter *t) {
    return true;
}
void MessagingInterface::handle(const Message&msg, Transmitter *from, bool needs_receipt ) {
    char *response = send(msg);
    if (response)
        free(response);
}


char *MessagingInterface::send(const char *txt) {
	if (owner_thread != pthread_self()) {
		char tnam1[100], tnam2[100];
		int pgn_rc = pthread_getname_np(pthread_self(),tnam1, 100);
		assert(pgn_rc == 0);
		pgn_rc = pthread_getname_np(owner_thread,tnam2, 100);
		assert(pgn_rc == 0);

		NB_MSG << "error: message send ("<< txt <<") from a different thread:"
		<< " owner: " << std::hex << " '" << owner_thread
		<< " '" << tnam2
		<< "' current: " << std::hex << " " << pthread_self()
		<< " '" << tnam1 << "'"
		<< "\n";
	}

    if (!is_publisher){
        DBG_MESSAGING << "sending message " << txt << " on " << url << "\n";
    }
    else {
        DBG_MESSAGING << "sending message " << txt << "\n";
    }
    size_t len = strlen(txt);
    
    // We try to send a few times and if an exception occurs we try a reconnect
    // but is this useful without a sleep in between?
    // It seems unlikely the conditions will have changed between attempts.
	zmq::message_t msg(len);
	strncpy ((char *) msg.data(), txt, len);
    while (true) {
        try {
            socket->send(msg);
            break;
        }
        catch (std::exception e) {
			if (errno == EINTR || errno == EAGAIN) {
				std::cerr << "MessagingInterface::send " << strerror(errno);
				continue;
			}
            if (zmq_errno())
                std::cerr << "Exception when sending " << url << ": " << zmq_strerror(zmq_errno()) << "\n";
            else
                std::cerr << "Exception when sending " << url << ": " << e.what() << "\n";
            socket->disconnect(url.c_str());
            usleep(50000);
            connect();
        }
    }
    if (!is_publisher) {
        while (true) {
            try {
                zmq::pollitem_t items[] = { { *socket, 0, ZMQ_POLLERR | ZMQ_POLLIN, 0 } };
                zmq::poll( &items[0], 1, 500);
                if (items[0].revents & ZMQ_POLLIN) {
                    zmq::message_t reply;
                    if (socket->recv(&reply)) {
                        len = reply.size();
                        char *data = (char *)malloc(len+1);
                        memcpy(data, reply.data(), len);
                        data[len] = 0;
                        std::cerr << url << ": " << data << "\n";
                        return data;
                    }
                }
                else if (items[0].revents & ZMQ_POLLERR) {
                    std::cerr << "MessagingInterface::send: error during recv\n";
                    continue;
                }
                std::cerr << "timeout: abandoning message " << txt << "\n";
                socket->disconnect(url.c_str());
                delete socket;
                socket = new zmq::socket_t(*getContext(), ZMQ_REQ);
                usleep(50000);
                connect();
                break;
            }
            catch (std::exception e) {
                if (zmq_errno())
                    std::cerr << "Exception when receiving response " << url << ": " << zmq_strerror(zmq_errno()) << "\n";
                else
                    std::cerr << "Exception when receiving response " << url << ": " << e.what() << "\n";
            }
        }
    }
    return 0;
}

char *MessagingInterface::send(const Message&msg) {
    char *text = MessageEncoding::encodeCommand(msg.getText(), msg.getParams());
    char *res = send(text);
    free(text);
    return res;
}

bool MessagingInterface::send_raw(const char *msg) {
    char error[ANET_ERR_LEN];
	int retry=2;
    size_t len = strlen(msg);
	while (retry--) {
	    size_t written = anetWrite(connection, (char *)msg, len);
	    if (written != len) {
	        snprintf(error, ANET_ERR_LEN, "error sending message: %s", msg );
	        MessageLog::instance()->add(error);
			close(connection);
			connect();
	    }
		else return true;
	}
	if (!retry) return false;
    return true;
}
char *MessagingInterface::sendCommand(std::string cmd, std::list<Value> *params) {
    char *request = MessageEncoding::encodeCommand(cmd, params);
    char *response = send(request);
    free(request);
    return response;
}

/* TBD, this may need to be optimised, one approach would be to use a 
 templating system; a property message looks like this:
 
 { "command":    "PROPERTY", "params":   
    [
         { "type":  "NAME", "value":    $1 },
         { "type":   "NAME", "value":   $2 },
         { "type":  TYPEOF($3), "value":  $3 }
     ] }
 
 */

char *MessagingInterface::sendCommand(std::string cmd, Value p1, Value p2, Value p3)
{
    std::list<Value>params;
    params.push_back(p1);
    params.push_back(p2);
    params.push_back(p3);
    return sendCommand(cmd, &params);
}

char *MessagingInterface::sendState(std::string cmd, std::string name, std::string state_name)
{
	size_t msglen = name.length() + state_name.length() + 50;
	char *buf = (char *)malloc(msglen);
	snprintf(buf, msglen, "{\"command\":\"STATE\", \"params\":[\"%s\", \"%s\"]}", name.c_str(), state_name.c_str());
	return buf;
/*
	cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "command", cmd.c_str());
    cJSON *cjParams = cJSON_CreateArray();
    cJSON_AddStringToObject(cjParams, "name", name.c_str());
    cJSON_AddStringToObject(cjParams, "state", state_name.c_str());
    cJSON_AddItemToObject(msg, "params", cjParams);
    char *request = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    char *response = send(request);
    free (request);
	return response;
 */
}

