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

/*
    This program will ping the iod every second, as long as it has a connection
    to its device and that device is responding.
    ... and the rest..
 */
#include <iostream>
#include "anet.h"
#include <boost/thread.hpp>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <cassert>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <sys/socket.h>
#include <zmq.hpp>
#include "regular_expressions.h"
#include <functional>
#include <exception>
#include <boost/bind.hpp>

#include <stdio.h>
#include <sys/select.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include "value.h"
#include "cJSON.h"
#include "options.h"
#include "Dispatcher.h"
#include "MessageEncoding.h"
#include "MessagingInterface.h"

class DeviceStatus {
public:
    enum State {e_unknown, e_disconnected, e_connected, e_up, e_failed, e_timeout };
    State current() { return status; }
    State previous() { return status; }
    static DeviceStatus *instance() { if (!instance_) instance_ = new DeviceStatus; return instance_; }
    void setStatus(State s){ prev_status = status; status = s; }
private:
    static DeviceStatus *instance_;
    State status;
    State prev_status;
    DeviceStatus() : status(e_unknown), prev_status(e_unknown) { }
    DeviceStatus(const DeviceStatus &);
    DeviceStatus& operator=(const DeviceStatus&);
};

DeviceStatus *DeviceStatus::instance_;

static const char *stringFromDeviceStatus(DeviceStatus::State status) {
    switch(status) {
        case DeviceStatus::e_disconnected:
            return "disconnected";
            break;
        case DeviceStatus::e_connected:
            return "connected";
            break;
        case DeviceStatus::e_timeout:
            return "timeout";
            break;
        case DeviceStatus::e_unknown:
            return "unknown";
            break;
        case DeviceStatus::e_up:
            return "running";
            break;
        case DeviceStatus::e_failed:
            return "failed";
            break;
    }
    return "unknown";
}


void usage(int argc, const char * argv[]) {
    std::cout << "Usage: " << argv[0]
        << "\n (--host hostname [--port port]) \n"
        << " | (--serial_port portname --serial_settings baud:bits:parity:stop_bits:flow_control )\n"
        << " --property property_name [--client] [--name device_name]\n"
        << " --watch_property property_name --collect_repeats [ --no_timeout_disconnect | --disconnect_on_timeout ]\n"
        << " --no_json --queue queue_name --channel channel_name [--cw_port port] "
        << "\n";
}


std::string escapeNonprintables(const char *buf) {
    const char *hex = "0123456789ABCDEF";
	std::string res;
	while (*buf) {
		if (isprint(*buf)) res += *buf;
		else if (*buf == '\015') res += "\\r";
		else if (*buf == '\012') res += "\\n";
		else if (*buf == '\010') res += "\\t";
		else {
			const char tmp[3] = { hex[ (*buf & 0xf0) >> 4], hex[ (*buf & 0x0f)], 0 };
			res = res + "#{" + tmp + "}";
		}
		++buf;
	}
	return res;
}

/** The Options structure provides methods to access parsed values of the commandline parameters

 */

class Options {

    Options() : is_server(true), port_(10240), host_(0), name_(0), machine_(0), property_(0), pattern_(0), iod_host_(0),
        serial_port_name_(0), serial_settings_(0), watch_(0), queue_(0), collect_duplicates(false), disconnect_on_timeout(true), cw_publisher(5556),
                got_host(false), got_port(true), got_property(false), got_pattern(false), structured_messaging(true),
                got_queue(false) {
        setIODHost("localhost");
    }
    static Options *instance_;
public:
    static Options *instance() { if (!instance_) instance_ = new Options; return instance_; }
    ~Options() {
        if (host_) free(host_);
        if (name_) free(name_);
        if (machine_) free(machine_);
        if (property_) free(property_);
        instance_ = 0;
    }
    bool valid() const {
        // serial implies client
        bool result =
                ( (got_port && got_host) || (got_serial && got_serial_settings) )
            && (got_property || (got_queue && structured_messaging) ) && got_pattern && name_ != 0 && iod_host_ != 0;
        if (!result) {
            std::stringstream msg;
            msg << "\nError:\n";
            if ( (got_serial && !got_serial_settings)  || (got_serial_settings && !got_serial)) {
                if (got_serial_settings) msg << "  specifying serial_settings requires --serial_port is also given\n";
                if (got_serial) msg << "  specifying serial_port requires --serial_port_settings is also given\n";
            }
            else if ( !got_host && !got_serial ) {
                msg << "  --host is required unless serial port is being used\n";
            }
            if (!got_property && !got_queue)
                msg << "  no property name or queue name detected (--property machine.property or --queue queue_name)\n";
            if (!got_pattern)
                msg << "  no pattern detected (--pattern text)\n";
            if (!name_)
                msg << "  no name given (--name)\n";
            if (!sendJSON() && got_queue)
                msg << " structure message (JSON) mode is required for sending to a queue\n";
            std::cerr <<msg.str() << "\n";
        }
        return result;
    }
    
    void clientMode(bool which = true) { is_server = !which; }
    void serverMode(bool which = true) { is_server = which; }
    
    bool server() const { return is_server; }
    bool client() const { return !is_server; }

    int port() const { return port_; }
    void setPort(int p) { port_ = p; got_port = true; }
    
    char *host() const { return host_; }
    void setHost(const char *h) {
        if (host_) free(host_);
        host_ = strdup(h); got_host = true;
    }
    
    const char *name() const { return name_; }
    void setName(const char *n) { if(name_) free(name_); name_ = strdup(n); }
    
    const char *property() const { return property_; }
    const char *machine() const { return machine_; }
    
    const char *iodHost() const { return iod_host_; }
    void setIODHost(const char *h) { if (iod_host_) free(iod_host_); iod_host_ = strdup(h); }
    
    const char *serialPort() const { return serial_port_name_; }
    const char *serialSettings() const { return serial_settings_; }
    
    bool skippingRepeats() const { return !collect_duplicates; }
    void doNotSkipRepeats() { collect_duplicates = true; }
    
    bool disconnectOnTimeout() { return disconnect_on_timeout; }
    void setDisconnectOnTimeout(bool which) { disconnect_on_timeout = which; }
    
    void setSerialPort(const char *port_name) {
        if (serial_port_name_) free(serial_port_name_);
        serial_port_name_ = strdup(port_name);
        got_serial = true;
        clientMode(true);
    }
    
    void setSerialSettings(const char *settings) {
        if (serial_settings_) free(serial_settings_);
        serial_settings_ = strdup(settings);
        got_serial_settings = true;
        clientMode(true);
    }
    
    const char *watchProperty() const { return watch_; }
    void setWatch(const char*new_watch) {
        if (watch_) free(watch_);
        watch_ = strdup(new_watch);
    }
    
    // properties are provided in dot form, we split it into machine and property for the iod
    void setProperty(const char* machine_property) {
        if (machine_) free(machine_);
        if (property_) free(property_);
        if (!machine_property) {
            machine_ = 0;
            property_ = 0;
        }
        else {
            char *buf = strdup(machine_property);
            char *dot = (strchr(buf, '.'));
            if (!dot) {
                std::cerr << "Warning: property should be in the form 'machine.property'\n";
                free(buf);
                return;
            }
            *dot++ = 0; // terminate the machine name at the dot
            machine_ = strdup(buf);
            property_ = strdup(dot);
            free(buf);
            got_property = true;
        }
    }
	
	void setChannelName(const char *chan) {
		if (channel_) free(channel_);
		channel_ = strdup(chan);
	}
	const char *getChannelName() const { if (channel_) return channel_; else return "DeviceConnector"; }
	
    const char *pattern() { return pattern_; }
    void setPattern(const char *p) {
        if (pattern_) free(pattern_);
        pattern_ = strdup(p);
        compiled_pattern = create_pattern(pattern_);
        got_pattern = (compiled_pattern->compilation_result == 0) ? true : false;
        if (!got_pattern)
            std::cerr << compiled_pattern->compilation_error << "\n";
    
    }
    rexp_info *regexpInfo() { return compiled_pattern;}
    rexp_info *regexpInfo() const { return compiled_pattern;}
    
    bool sendJSON() const { return structured_messaging; }
    void setSendJSON(bool which) { structured_messaging = which; }
    void setQueue(const char *q) {
        if (queue_) free(queue_);
        queue_ = strdup(q);
        got_queue = true;
    }
    const char *queue() const { return queue_; }
    
    int publisher_port() const { return cw_publisher; }
    void set_publisher_port(int port) { cw_publisher = port; }

    
protected:
    bool is_server;    // if true, listen for connections, otherwise, connect to a device
    int port_;          // network port number
    char *host_;        // host name
    char *name_;        // device name
    char *machine_;     //  fsm in iod
    char *property_;    // property in the iod fsm
    char *pattern_;     // pattern used to detect a complete message
    rexp_info *compiled_pattern;
    char *iod_host_;
    char *serial_port_name_;
    char *serial_settings_;
    char *watch_;
    char *queue_;
	char *channel_;
    bool collect_duplicates;
    bool disconnect_on_timeout;
    int cw_publisher;
    
    // validation
    bool got_host;
    bool got_port;
    bool got_property;
    bool got_pattern;
    bool got_serial;
    bool got_serial_settings;
    bool structured_messaging;
    bool got_queue;
};


enum config_state {cs_baud, cs_bits, cs_parity, cs_stop, cs_flow, cs_end };

config_state operator++(config_state &cs) {
    switch (cs) {
        case cs_baud: { cs = cs_bits; break;}
        case cs_bits: { cs = cs_parity; break; }
        case cs_parity: { cs = cs_stop; break; }
        case cs_stop:   { cs = cs_flow; break; };
        case cs_flow:   { cs = cs_end; break; };
        case cs_end:
        default:  { cs = cs_end; break; }
    }
    return cs;
}

int getSettings(const char *str, struct termios *settings) {
	int err;
	char *buf = strdup(str);
	char *fld, *p = buf;
	enum config_state state = cs_baud;
	
	while (state != cs_end) {
		fld = strsep(&p, ":");
		if (!fld) goto done_getSettings; // no more fields
        
		char *tmp = 0;
		// most fields are numbers so we usually attempt to convert the field to a number
		long val = 8;
		if (state != cs_parity && state != cs_flow) val = strtol(fld, &tmp, 10);
		if ( (tmp && *tmp == 0) || ( (state == cs_parity || state == cs_flow) && *fld != ':') ) { // config included the field
			switch(state) {
				case cs_baud:
					if ( (err = cfsetspeed(settings, val)) ) {
						perror("setting port speed");
						return err;
					}
					settings->c_cflag |= (CLOCAL | CREAD);
					break;
				case cs_bits:
					settings->c_cflag &= ~CSIZE;
					if (val == 8)
						settings->c_cflag |= CS8;
					else if (val == 7)
						settings->c_cflag |= CS7;
					break;
				case cs_parity:
					if (toupper(*fld) == 'N') {
						settings->c_cflag &= ~PARENB;
					}
					else if (toupper(*fld) == 'E') {
						settings->c_cflag |= PARENB;
						settings->c_cflag &= ~PARODD;
					}
					else if (toupper(*fld) == 'O') {
						settings->c_cflag |= PARENB;
						settings->c_cflag |= PARODD;
					}
					break;
				case cs_stop:
					if (val != 2)
						settings->c_cflag &= ~CSTOPB;
					else
						settings->c_cflag |= CSTOPB;
					break;
				case cs_flow:
#if 0
					if (toupper(*fld) == 'Y')
						settings->c_cflag |= CNEW_RTSCTS;
					else if (toupper(*fld) == 'N')
						settings->c_cflag &= !CNEWRTSCTS;
#endif
					break;
				case cs_end:
				default: ;
			}
		}
		else {
			if (*tmp && *tmp != ':') {
				fprintf(stderr, "skipping unrecognised setting: %s\n", fld);
			}
		}
		++state;
	}
done_getSettings:
	free(buf);
	return 0;
}

int setupSerialPort(const char *portname, const char *setting_str) {
	int serial = ::open(portname, O_RDWR | O_NDELAY); //O_RDWR | O_NOCTTY | O_NDELAY);
	if (serial == -1) {
		perror("Error opening port ");
        return -1;
	}
	{
		int flags = fcntl(serial, F_GETFL);
		if (flags == -1) {
			perror("fcntl getting flags ");
            goto closePort;
		}
		flags |= O_NONBLOCK;
        
		int ec = 0;
		ec = fcntl(serial, F_SETFL, flags);
		if (ec == -1) {
			perror("fcntl setting flags ");
            goto closePort;
		}
	}
	{
		struct termios settings;
		int result;
		result = tcgetattr(serial, &settings);
		if (result == -1) {
			perror("getting terminal attributes");
            goto closePort;
		}
        settings.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        settings.c_iflag &= ~(IXON | IXOFF | IXANY);
		if ( ( result = getSettings(setting_str, &settings)) ) {
			fprintf(stderr, "Setup error %d\n", result);
            goto closePort;
		}
		result = tcsetattr(serial, 0, &settings);
		if (result == -1) {
			perror("setting terminal attributes");
            goto closePort;
		}
	}
	return serial;
closePort:
    close(serial);
    return -1;
}


/** The MatchFunction provides a regular expression interface so that data incoming on the
    device connection can be filtered. Only matched data is passed back to iod/clockwork
 */

static struct timeval last_send;
static std::string last_message;

static const char *iod_connection = "inproc://iod_interface";

struct MatchFunction {
    static MatchFunction *instance() { if (!instance_) instance_ = new MatchFunction; return instance_; }
    static int match_func(const char *match, int index, void *data)
    {
        //std::cout << "match: " << index << " " << match << "\n";
        if (index == 0) {
            size_t *n = (size_t*)data;
            *n += strlen(match);
        }
		int num_sub = (int)numSubexpressions(Options::instance()->regexpInfo());
        if (num_sub == 0 || index>0) {
            struct timeval now;
            gettimeofday(&now, 0);
            if (Options::instance()->sendJSON() && Options::instance()->queue()) {
                // structured messaging, pushing each match to a queue
                if (index == 0 || index == 1) {
                    instance()->params.clear();
                    instance()->params.push_back(Options::instance()->queue());
                }
                instance()->params.push_back(Value(match, Value::t_string));
                if (index == num_sub) {
                    char *msg = MessageEncoding::encodeCommand("DATA", &instance()->params);
                    if ( (Options::instance()->skippingRepeats() == false
                                            || last_message != msg || last_send.tv_sec +5 <  now.tv_sec)) {
                        if (msg) {
                            if (!instance_) { MatchFunction::instance(); usleep(50); }
                            std::string response;
                            if (sendMessage(msg, MatchFunction::instance()->iod_interface, response)) {
                                last_message = msg;
                                last_send.tv_sec = now.tv_sec;
                                last_send.tv_usec = now.tv_usec;
                            }
                            free(msg);
                        }
                    }
                    else if (msg) free(msg);
                }
            }
            if (Options::instance()->property()) {
                if (index == 0 || index == 1)
                    MatchFunction::instance()->result = match;
                else
                    MatchFunction::instance()->result = MatchFunction::instance()->result + " " +match;
                
                std::string res = MatchFunction::instance()->result;
                if (index == num_sub && (Options::instance()->skippingRepeats() == false
                                            || last_message != res || last_send.tv_sec +5 <  now.tv_sec)) {
                    char *cmd = MessageEncoding::encodeCommand("PROPERTY", Options::instance()->machine(), Options::instance()->property(), res.c_str());
                    if (cmd) {
                        std::string response;
                        if (sendMessage(cmd, MatchFunction::instance()->iod_interface, response)) {
                            last_message = res;
                            last_send.tv_sec = now.tv_sec;
                            last_send.tv_usec = now.tv_usec;
                        }
                        free(cmd);
                    }
                }
            }
        }
        return 0;
    }
    
protected:
    static MatchFunction *instance_;
    std::list<Value> params;
    std::string result;
private:
    MatchFunction() : iod_interface(*MessagingInterface::getContext(), ZMQ_REQ){
        iod_interface.connect(iod_connection);
    }
    zmq::socket_t iod_interface;
    MatchFunction(const MatchFunction&);
    MatchFunction &operator=(const MatchFunction&);
};
MatchFunction *MatchFunction::instance_;

/** The ConnectionThread maintains a connection to the device, either on a network
    (tcp) stream or a serial port.
 
   Device Status is used to report the connection state to clockwork via the status property
*/

struct ConnectionThread {
    
    void operator()() {
        try {
            gettimeofday(&last_active, 0);
            
            if (Options::instance()->server()) {
                listener = anetTcpServer(msg_buffer, Options::instance()->port(), Options::instance()->host());
                if (listener == -1) {
                    std::cerr << msg_buffer << " attempting to listen on port " << Options::instance()->port() << "...aborting\n";
                    DeviceStatus::instance()->setStatus(DeviceStatus::e_failed);
                    updateProperty();
                    done = true;
                    return;
                }
            }
            
            const int buffer_size = 100;
            char buf[buffer_size];
            size_t offset = 0;
            useconds_t retry_delay = 50000; // usec delay before trying to setup
                                               // the connection initialy 50ms with a back-off algorithim
            
            DeviceStatus::instance()->setStatus(DeviceStatus::e_disconnected);
            updateProperty();
            while (!done) {
                
                // connect or accept connection
                if (DeviceStatus::instance()->current() == DeviceStatus::e_disconnected && Options::instance()->client()) {
                    if (Options::instance()->serialPort()) {
                        connection = setupSerialPort(Options::instance()->serialPort(), Options::instance()->serialSettings());
                        if (connection == -1) {
                            usleep(retry_delay); // pause before trying again
                            if (retry_delay < 2000000) retry_delay *= 1.2;
                            continue;
                        }
                    }
                    else {
                        connection = anetTcpConnect(msg_buffer, Options::instance()->host(), Options::instance()->port());
                        if (connection == -1) {
                            std::cerr << msg_buffer << " retrying in " << (retry_delay/1000) << "ms\n";
                            usleep(retry_delay); // pause before trying again
                            if (retry_delay < 2000000) retry_delay *= 1.2;
                            continue;
                        }
                    }
                    retry_delay = 50000;
                    DeviceStatus::instance()->setStatus(DeviceStatus::e_connected);
                    updateProperty();
                }
                
                fd_set read_ready;
                FD_ZERO(&read_ready);
                int nfds = 0;
                DeviceStatus::State dev_state = DeviceStatus::instance()->current();
                if (dev_state == DeviceStatus::e_connected  || dev_state == DeviceStatus::e_up || dev_state == DeviceStatus::e_timeout ) {
                    FD_SET(connection, &read_ready);
                    nfds = connection+1;
                }
                else if (Options::instance()->server()) {
                    FD_SET(listener, &read_ready);
                    nfds = listener+1;
                }
                
                struct timeval select_timeout;
                select_timeout.tv_sec = 2;
                select_timeout.tv_usec = 0;
                int err = select(nfds, &read_ready, NULL, NULL, &select_timeout);
                if (err == -1) {
                    if (errno != EINTR)
                        std::cerr << "socket: " << strerror(errno) << "\n";
                }
                else if (err == 0) {
                    DeviceStatus::State dev_state = DeviceStatus::instance()->current();
                    if (dev_state == DeviceStatus::e_connected || dev_state == DeviceStatus::e_up) {
                        DeviceStatus::instance()->setStatus( DeviceStatus::e_timeout );
                        updateProperty();
                        std::cerr << "select timeout " << select_timeout.tv_sec << "."
                        << std::setfill('0') << std::setw(3) << (select_timeout.tv_usec / 1000) << "\n";
                        
                        // we disconnect from a TCP connection on a timeout but do nothing in the case of a serial port
                        if (!Options::instance()->serialPort() && Options::instance()->disconnectOnTimeout()) {
                            std::cerr << "Closing device connection due to timeout\n";
                            close(connection);
                            DeviceStatus::instance()->setStatus( DeviceStatus::e_disconnected );
                            updateProperty();
                        }
                        continue;
                    }
                }
                
                if (Options::instance()->server() && DeviceStatus::instance()->current() == DeviceStatus::e_disconnected && FD_ISSET(listener, &read_ready)) {
                    // Accept and setup a connection
                    int port;
                    char hostip[16]; // dot notation
                    connection = anetAccept(msg_buffer, listener, hostip, &port);
                    
                    if (anetTcpKeepAlive(msg_buffer, connection) == -1) {
                        std::cerr << msg_buffer << "\n";
                        close(connection);
                        continue;
                    }
                    
                    if (anetTcpNoDelay(msg_buffer, connection) == -1) {
                        std::cerr << msg_buffer << "\n";
                        close(connection);
                        continue;
                    }
                    
                    if (anetNonBlock(msg_buffer, connection) == -1) {
                        std::cerr << msg_buffer << "\n";
                        close(connection);
                        continue;
                    }
                    DeviceStatus::instance()->setStatus( DeviceStatus::e_connected );
                    updateProperty();
                }
                else if (connection != -1 && FD_ISSET(connection, &read_ready)) {
                    if (offset < buffer_size-1) {
                        ssize_t n = read(connection, buf+offset, buffer_size - offset - 1);
                        if (n == -1) {
                            if (!done && errno != EINTR && errno != EAGAIN) { // stop() may cause a read error that we ignore
                                std::cerr << "error: " << strerror(errno) << " reading from connection\n";
                                close(connection);
                                DeviceStatus::instance()->setStatus( DeviceStatus::e_disconnected );
                                updateProperty();
                            }
                        }
                        else if (n) {
                            DeviceStatus::instance()->setStatus( DeviceStatus::e_up );
                            updateProperty();
                            
                            buf[offset+n] = 0;
                            {
                                boost::mutex::scoped_lock lock(connection_mutex);
                                gettimeofday(&last_active, 0);
                                last_msg = buf;
                            }
                        }
                        else {
                            close(connection);
                            DeviceStatus::instance()->setStatus( DeviceStatus::e_disconnected );
                            updateProperty();
                            std::cerr << "connection lost\n";
                            connection = -1;
                        }
                        
                        // recalculate offset as we step through matches
                        offset = 0;
                        each_match(Options::instance()->regexpInfo(), buf, &MatchFunction::match_func, &offset);
                        size_t len = strlen(buf);
#if 0
                        std::cout << "buf: ";
                        for (int i=0; i<=len; ++i) {
                            std::cout << std::setw(2) << std::setfill('0') << std::hex << (int)buf[i] << " ";
                        }
                        std::cout << "\n";
                        std::cout << "     ";
                        
                        for (int i=0; i<offset; ++i) {
                            std::cout << std::hex << "   ";
                        }
                        std::cout << "^\n";
#endif
                        if (offset)  {
                            memmove(buf, buf+offset, len+1-offset);
                        }
                        offset = strlen(buf);
                    }
                    else {
                        std::cerr << "buffer full: " << buf << "\n";
                        close(connection);
                        DeviceStatus::instance()->setStatus(DeviceStatus::e_disconnected);
                        updateProperty();
                    }
                }
            }
        }catch (std::exception e) {
            if (zmq_errno())
                std::cerr << zmq_strerror(zmq_errno()) << "\n";
            else
                std::cerr << e.what() << "\n";
        }
    }
    
    void send(const char *msg) {
        boost::mutex::scoped_lock lock(connection_mutex);
        to_send += msg;
        if (connection != -1 && !to_send.empty()) {
            //boost::mutex::scoped_lock lock(connection_mutex);
            
            size_t n = write(connection, to_send.c_str(), to_send.length());
            if ((long)n == -1) {
                std::cerr << "write error sending data to current connection\n";
            }
            else {
                to_send = to_send.substr(n); // shift off the data that has been written
            }
        }
    }
    
    ConnectionThread() : done(false), connection(-1), cmd_interface(*MessagingInterface::getContext(), ZMQ_REQ), msg_buffer(0)
    {
        assert(Options::instance()->valid());
        msg_buffer =new char[ANET_ERR_LEN];
        gettimeofday(&last_active, 0);
        last_property_update.tv_sec = 0;
        last_property_update.tv_usec = 0;
        last_status = DeviceStatus::e_unknown;
        cmd_interface.connect(iod_connection);
    }
    
    void stop() {
        done = true;
        DeviceStatus::State dev_state = DeviceStatus::instance()->current();
        if (dev_state == DeviceStatus::e_connected || dev_state == DeviceStatus::e_up || dev_state == DeviceStatus::e_disconnected) {
            done = true;
            close(connection);
        }
    }
    
    // returns the last received message and the time it arrived
    void get_last_message(std::string &msg, struct timeval &msg_time) {
        boost::mutex::scoped_lock lock(connection_mutex);
        msg = last_msg;
        msg_time = last_active;
    }
    
    void updateProperty() {
        struct timeval now;
        gettimeofday(&now, 0);
        if (last_status != DeviceStatus::instance()->current() || now.tv_sec >= last_property_update.tv_sec + 5) {
            {
            boost::mutex::scoped_lock lock(connection_mutex);
            last_property_update.tv_sec = now.tv_sec;
            last_property_update.tv_usec = now.tv_usec;
            last_status = DeviceStatus::instance()->current();
            }
            bool sent = false;
            char *cmd_str = MessageEncoding::encodeCommand("PROPERTY", Options::instance()->name(),
                                                           "status", stringFromDeviceStatus(DeviceStatus::instance()->current()));
            std::string response;
            if (cmd_str) {
                sent = sendMessage(cmd_str, cmd_interface, response);
                free(cmd_str);
            }
//            sent = iod_interface.setProperty(Options::instance()->name(),
//                                            "status", stringFromDeviceStatus(DeviceStatus::instance()->current()));
            if (!sent) {
                std::cerr << "Failed to set status property " << Options::instance()->name() << ".status\n";
            }
            if (response == "Unknown device") {
                std::cout << "invalid clockwork device name " << Options::instance()->name() << "\n";
            }
        }
    }
    bool done;
    int listener;
    int connection;
    zmq::socket_t cmd_interface;
    std::string last_msg;
    char *msg_buffer;
    struct timeval last_active;
    DeviceStatus::State last_status;
    struct timeval last_property_update;
    boost::mutex connection_mutex;
    struct termios terminal_settings;
    std::string to_send;
};

bool done = false;
Options *Options::instance_;

static void finish(int sig)
{
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGINT, &sa, 0);
    done = true;
}

bool setup_signals()
{
    struct sigaction sa;
    sa.sa_handler = finish;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, 0) || sigaction(SIGINT, &sa, 0)) {
				done = true;
        return false;
    }
    return true;
}

class PropertyStatus {
public:
    struct timeval last_property_update;

};


int main(int argc, const char * argv[])
{
    zmq::context_t context;
    MessagingInterface::setContext(&context);
    
    last_send.tv_sec = 0;
    last_send.tv_usec = 0;
    try {
        Options &options = *Options::instance();
        for (int i=1; i<argc; i++) {
            if (strcmp(argv[i], "--port") == 0 && i < argc-1) {
                long port;
                char *next;
                port = strtol(argv[++i], &next, 10);
                if (!*next) options.setPort(port & 0xffff);
            }
            else if (strcmp(argv[i], "--host") == 0 && i < argc-1) {
                options.setHost(argv[++i]);
            }
            else if (strcmp(argv[i], "--cw_host") == 0 && i < argc-1) {
                options.setIODHost(argv[++i]);
            }
            else if (strcmp(argv[i], "--name") == 0 && i < argc-1) {
                options.setName(argv[++i]);
            }
            else if (strcmp(argv[i], "--property") == 0 && i < argc-1) {
                options.setProperty(argv[++i]);
            }
            else if (strcmp(argv[i], "--pattern") == 0 && i < argc-1) {
                options.setPattern(argv[++i]);
            }
            else if (strcmp(argv[i], "--queue") == 0 && i < argc-1) {
                options.setQueue(argv[++i]);
            }
            else if (strcmp(argv[i], "--client") == 0) {
                options.clientMode();
            }
            else if (strcmp(argv[i], "--serial_port") == 0) {
                options.setSerialPort(argv[++i]);
            }
            else if (strcmp(argv[i], "--serial_settings") == 0) {
                options.setSerialSettings(argv[++i]);
            }
            else if (strcmp(argv[i], "--watch_property") == 0) {
                options.setWatch(argv[++i]);
            }
            else if (strcmp(argv[i], "--collect_repeats") == 0) {
                options.doNotSkipRepeats();
            }
            else if (strcmp(argv[i], "--disconnect_on_timeout") == 0) {
                options.setDisconnectOnTimeout(true);
            }
            else if (strcmp(argv[i], "--no_timeout_disconnect") == 0) {
                options.setDisconnectOnTimeout(false);
            }
            else if (strcmp(argv[i], "--no_json") == 0) {
                options.setSendJSON(false);
            }
            else if (strcmp(argv[i], "--cw_port") == 0 && i < argc-1) {
                int pport = (int)strtol(argv[++i], 0, 10);
                options.set_publisher_port(pport);
                set_publisher_port(pport);
            }
			else if (strcmp(argv[i], "--channel") == 0) {
				options.setChannelName(argv[++i]);
			}
            else {
                std::cerr << "Warning: parameter " << argv[i] << " not understood\n";
            }
        }
        if (!options.valid()) {
            usage(argc, argv);
            exit(EXIT_FAILURE);
        }

        struct timeval last_time;
        gettimeofday(&last_time, 0);
        if (!setup_signals()) {
            std::cerr << "Error setting up signals " << strerror(errno) << "\n";
        }
        
        
        zmq::socket_t cmd(*MessagingInterface::getContext(), ZMQ_REP);
        cmd.bind(iod_connection);
        usleep(5000);
        
	      ConnectionManager *connection_manager = 0;
				try {
	        if (options.watchProperty()) {
				connection_manager = new SubscriptionManager(Options::instance()->getChannelName());
	        }
	        else {
	            connection_manager = new CommandManager(options.iodHost());
	        }
				}
				catch(zmq::error_t io) {
					std::cout << "zmq error: " << zmq_strerror(errno) << "\n";
				}
				catch(std::exception ex) {
					std::cout << " unknown exception: " << zmq_strerror(errno) << "\n";
				}
				assert(connection_manager);
        
        ConnectionThread connection_thread;
        boost::thread monitor(boost::ref(connection_thread));

        //PropertyMonitorThread watch_thread(options, connection_thread);
        //boost::thread *watcher = 0;
            
        //if (options.watchProperty()) {
        //    watcher = new boost::thread(boost::ref(watch_thread));
        //}

        zmq::pollitem_t *items = new zmq::pollitem_t[3];
        memset(items, 0, sizeof(zmq::pollitem_t)*3);
        int idx = 0;
        
        int cmd_index = -1;
        int subs_index = -1;
        int num_items = 0;
        if (options.watchProperty()) {
            SubscriptionManager *sm = dynamic_cast<SubscriptionManager*>(connection_manager);
						assert(sm);
            items[idx].socket = sm->setup; items[idx].events = ZMQ_POLLERR | ZMQ_POLLIN;  idx++;
            subs_index = idx;
            items[idx].socket = sm->subscriber; items[idx].events = ZMQ_POLLERR | ZMQ_POLLIN;  idx++;
        }
        else {
            CommandManager *cm = dynamic_cast<CommandManager*>(connection_manager);
						assert(cm);
            items[idx].socket = *cm->setup; items[idx].events = ZMQ_POLLERR | ZMQ_POLLIN;  idx++;
        }
        cmd_index = idx; items[idx].socket = cmd; items[idx].events = ZMQ_POLLERR | ZMQ_POLLIN;  idx++;
        num_items = idx;

        
        while (!done)
        {
            struct timeval now;
            gettimeofday(&now, 0);

            try {
                if (!connection_manager->checkConnections(items, num_items, cmd)) { usleep(50000); continue;}
            }
            catch (std::exception e) {
                if (zmq_errno())
                    std::cerr << "error: " << zmq_strerror(zmq_errno()) << "\n";
                else
                    std::cerr << e.what() << "\n";
            }

            
            // TBD once the connection is open, check that data has been received within the last second
            DeviceStatus::State dev_stat = DeviceStatus::instance()->current();
            if (dev_stat == DeviceStatus::e_up || dev_stat == DeviceStatus::e_connected || dev_stat == DeviceStatus::e_timeout) {
                std::string msg;
                struct timeval last;
                connection_thread.get_last_message(msg, last);
                if (last_time.tv_sec != last.tv_sec && now.tv_sec > last.tv_sec + 1) {
                    if (dev_stat == DeviceStatus::e_timeout)
                        std::cerr << "Warning: Device timeout\n";
                    else
                        std::cout << "Warning: connection idle\n";
                    last_time = last;
                }
            }

            //if ( !(items[1].revents & ZMQ_POLLIN) ) continue;
            
            if (subs_index>=0 && items[subs_index].revents & ZMQ_POLLIN) {
                char data[1000];
                size_t len = 0;
                try {
                    SubscriptionManager *sm = dynamic_cast<SubscriptionManager*>(connection_manager);
                    if (sm) {
                        len = sm->subscriber.recv(data, 1000);
                        if (!len) continue;
                        data[len] = 0;
                        std::string cmd;
                        std::vector<Value>*params = 0;
                        MessageEncoding::getCommand(data, cmd, &params);
                        if (cmd == "PROPERTY") {
                            std::string prop = params->at(0).asString();
                            prop = prop + "." + params->at(1).asString();
                            if (prop == options.watchProperty()) {
                                connection_thread.send(params->at(2).asString().c_str());
                                std::cout << "sending: " << params->at(2).asString().c_str() << "\n";

                            }
                        }
                        if (params) delete params;
                    }
                }
                catch (zmq::error_t e) {
                    if (errno == EINTR) continue;
                    
                }
                data[len] = 0;
            }
        }

        connection_thread.stop();
        //if (watcher) watch_thread.stop();
        monitor.join();
    }
    catch (std::exception e) {
        if (zmq_errno()) 
            std::cerr << "error: " << zmq_strerror(zmq_errno()) << "\n";
        else
            std::cerr << e.what() << "\n";
				exit(1);
    }
    return 0;
}

