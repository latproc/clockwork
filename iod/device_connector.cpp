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
#include <boost/bind.hpp>

#include <stdio.h>
#include <sys/select.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>


struct DeviceStatus {
    enum State {e_unknown, e_disconnected, e_connected, e_up, e_failed, e_timeout };
    State status;
    
    DeviceStatus() : status(e_unknown) { }
};


void usage(int argc, const char * argv[]) {
    std::cout << "Usage: " << argv[0]
        << " --host hostname --port port --property property_name [--client] [--name device_name]\n";
}

struct Options {
    Options() : is_server(true), port_(10240), host_(0), name_(0), machine_(0), property_(0), pattern_(0), iod_host_(0),
                serial_port_name_(0), serial_settings_(0),
                got_host(false), got_port(true), got_property(false), got_pattern(false)  {
        setIODHost("localhost");
    }
    ~Options() {
        if (host_) free(host_);
        if (name_) free(name_);
        if (machine_) free(machine_);
        if (property_) free(property_);
    }
    bool valid() const {
        // serial implies client
        return ( (got_port && got_host) || (got_serial && got_serial_settings && !is_server) )
            && got_property && got_pattern && name_ != 0 && iod_host_ != 0;
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
    
    void setSerialPort(const char *port_name) {
        serial_port_name_ = strdup(port_name);
        got_serial = true;
    }
    
    void setSerialSettings(const char *settings) {
        serial_settings_ = strdup(settings);
        got_serial_settings = true;
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
    
    // validation
    bool got_host;
    bool got_port;
    bool got_property;
    bool got_pattern;
    bool got_serial;
    bool got_serial_settings;
};



int getSettings(const char *str, struct termios *settings) {
	int err;
	char *buf = strdup(str);
	char *fld, *p = buf;
	enum config_state {cs_baud, cs_bits, cs_parity, cs_stop, cs_flow, cs_end };
	enum config_state state = cs_baud;
	
	while (state != cs_end) {
		fld = strsep(&p, ":");
		if (!fld) goto done_getSettings; // no more fields
        
		char *tmp = 0;
		// most fields are numbers so we usually attempt to convert the field to a number
		long val;
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
		state++;
	}
done_getSettings:
	free(buf);
	return 0;
}

int setupSerialPort(const char *portname, const char *setting_str) {
	int serial = open(portname, O_RDWR | O_NOCTTY | O_NDELAY);
	if (serial == -1) {
		perror("Error opening port ");
        return -1;
	}
	{
		int flags = fcntl(serial, F_GETFL);
		if (flags == -1) {
			perror("fcntl getting flags ");
            return -1;
		}
		flags |= O_NONBLOCK;
        
		int ec = 0;
		ec = fcntl(serial, F_SETFL, flags);
		if (ec == -1) {
			perror("fcntl setting flags ");
            return -1;
		}
	}
	{
		struct termios settings;
		int result;
		result = tcgetattr(serial, &settings);
		if (result == -1) {
			perror("getting terminal attributes");
            return -1;
		}
		if ( ( result = getSettings(setting_str, &settings)) ) {
			fprintf(stderr, "Setup error %d\n", result);
            return -1;
		}
		result = tcsetattr(serial, 0, &settings);
		if (result == -1) {
			perror("setting terminal attributes");
            return -1;
		}
	}
	return serial;
}


struct IODInterface{
    
    const int REQUEST_RETRIES;
    const int REQUEST_TIMEOUT;
    
    void sendMessage(const char *message) {
        boost::mutex::scoped_lock lock(interface_mutex);

        try {
            const char *msg = (message) ? message : "";

            int retries = REQUEST_RETRIES;
            while (!done && retries) {
                size_t len = strlen(msg);
                zmq::message_t request (len);
                memcpy ((void *) request.data (), msg, len);
                socket->send (request);
                bool expect_reply = true;
                
                while (!done && expect_reply) {
                    zmq::pollitem_t items[] = { { *socket, 0, ZMQ_POLLIN, 0 } };
                    zmq::poll( &items[0], 1, REQUEST_TIMEOUT*1000);
                    if (items[0].revents & ZMQ_POLLIN) {
                        zmq::message_t reply;
                        if (!socket->recv(&reply)) continue;
                    	len = reply.size();
                    	char *data = (char *)malloc(len+1);
                    	memcpy(data, reply.data(), len);
                    	data[len] = 0;
                    	std::cout << data << "\n";
                    	free(data);
						return;
                    }
                    else if (--retries == 0) {
                        // abandon
                        expect_reply = false;
                        std::cerr << "abandoning message " << msg << "\n";
                        delete socket;
                        connect();
                    }
                    else {
                        // retry
                        std::cerr << "retrying message " << msg << "\n";
                        delete socket;
                        connect();
                        socket->send (request);
                    }
                }
            }
        }
        catch(std::exception e) {
            std::cerr <<e.what() << "\n";
        }
    }
    
    void setProperty(const std::string &machine, const std::string &property, const std::string &val) {
        std::stringstream ss;
        std::cout << "val: '" << val << "'\n";
        ss << "PROPERTY " << machine << " " << property << " " << val << "\n";
        sendMessage(ss.str().c_str());
    }
    
    void connect() {
        try {
            std::stringstream ss;
            ss << "tcp://" << options.iodHost() << ":" << 5555;
            socket = new zmq::socket_t (*context, ZMQ_REQ);
            socket->connect(ss.str().c_str());
            int linger = 0; // do not wait at socket close time
            socket->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        }
        catch (std::exception e) {
            if (zmq_errno())
                std::cerr << zmq_strerror(zmq_errno()) << "\n";
            else
                std::cerr << e.what() << "\n";
        }
    }
    
    void stop() { done = true; }
    
    IODInterface(const Options &opts) : REQUEST_RETRIES(3), REQUEST_TIMEOUT(2000), context(0), socket(0), options(opts), done(false) {
        context = new zmq::context_t(1);
        connect();
    }
    
    zmq::context_t *context;
    zmq::socket_t *socket;
    boost::mutex interface_mutex;
    const Options &options;
    bool done;

};


static struct timeval last_send;
static std::string last_message;

struct MatchFunction {
    MatchFunction(const Options &opts, IODInterface &iod) :options(opts), iod_interface(iod) {
        instance_ = this;
    }
    static MatchFunction *instance() { return instance_; }
    static int match_func(const char *match, int index, void *data)
    {
        std::cout << "match: " << index << " " << match << "\n";
        if (index == 0) {
            size_t *n = (size_t*)data;
            *n += strlen(match);
        }
		int num_sub = (int)numSubexpressions(instance()->options.regexpInfo());
        if (num_sub == 0 || index>0) {
            struct timeval now;
            gettimeofday(&now, 0);
			if (index == 0 || index == 1) 
				MatchFunction::instance()->result = match;
			else 
				MatchFunction::instance()->result += match;
			std::string res = MatchFunction::instance()->result;
            if (index == num_sub && (last_message != res || last_send.tv_sec +5 <  now.tv_sec)) {
                instance()->iod_interface.setProperty(instance()->options.machine(), instance()->options.property(), res.c_str());
				last_message = res;
                last_send.tv_sec = now.tv_sec;
                last_send.tv_usec = now.tv_usec;
            }
        }
        return 0;
    }
protected:
    static MatchFunction *instance_;
    const Options &options;
    IODInterface &iod_interface;
	std::string result;
private:
    MatchFunction(const MatchFunction&);
    MatchFunction &operator=(const MatchFunction&);
};
MatchFunction *MatchFunction::instance_;


struct ConnectionThread {

    void operator()() {
        try {
        gettimeofday(&last_active, 0);
        MatchFunction *match = new MatchFunction(options, iod_interface);
        
        if (options.server()) {
            listener = anetTcpServer(msg_buffer, options.port(), options.host());
            if (listener == -1) {
                std::cerr << msg_buffer << "...aborting\n";
                device_status.status = DeviceStatus::e_failed;
                updateProperty();
                done = true;
                return;
            }
        }
        
        const int buffer_size = 100;
        char buf[buffer_size];
        size_t offset = 0;
        
        device_status.status = DeviceStatus::e_disconnected;
        updateProperty();
        while (!done) {
            
            // connect or accept connection
            if (device_status.status == DeviceStatus::e_disconnected && options.client()) {
                if (options.serialPort()) {
                    connection = setupSerialPort(options.serialPort(), options.serialSettings());
                    if (connection == -1) {
                        continue;
                    }
                }
                else {
                    connection = anetTcpConnect(msg_buffer, options.host(), options.port());
                    if (connection == -1) {
                        std::cerr << msg_buffer << "\n";
                        continue;
                    }
                }
                device_status.status = DeviceStatus::e_connected;
                updateProperty();
            }
           
            fd_set read_ready;
            FD_ZERO(&read_ready);
            int nfds = 0;
            if (device_status.status == DeviceStatus::e_connected
                || device_status.status == DeviceStatus::e_up
                || device_status.status == DeviceStatus::e_timeout
                ) {
                FD_SET(connection, &read_ready);
                nfds = connection+1;
            }
            else if (options.server()) {
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
                if (device_status.status == DeviceStatus::e_connected || device_status.status == DeviceStatus::e_up) {
                    device_status.status = DeviceStatus::e_timeout;
                    updateProperty();
                    std::cerr << "select timeout\n";
                    close(connection);
                    device_status.status = DeviceStatus::e_disconnected;
                    updateProperty();
					continue;
                }
            }
            
            
            if (device_status.status == DeviceStatus::e_disconnected && FD_ISSET(listener, &read_ready)) {
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
                device_status.status = DeviceStatus::e_connected;
                updateProperty();
            }
            else if (connection != -1 && FD_ISSET(connection, &read_ready)) {
                if (offset < buffer_size-1) {
                    ssize_t n = read(connection, buf+offset, buffer_size - offset - 1);
                    if (n == -1) { 
                        if (!done && errno != EINTR && errno != EAGAIN) { // stop() may cause a read error that we ignore
                            std::cerr << "error: " << strerror(errno) << " reading from connection\n";
                            close(connection);
                            device_status.status = DeviceStatus::e_disconnected;
 							updateProperty();                           
                        }
                    }
                    else if (n) {
                        device_status.status = DeviceStatus::e_up;
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
                        device_status.status = DeviceStatus::e_disconnected;
                        updateProperty();
                        std::cerr << "connection lost\n";
                        connection = -1;
                    }
                    
                    // recalculate offset as we step through matches
                    offset = 0;
                    each_match(options.regexpInfo(), buf, &MatchFunction::match_func, &offset);
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
                    std::cerr << "buffer full\n";
                    close(connection);
                    device_status.status = DeviceStatus::e_disconnected;
					updateProperty();                           
                }
            }
        }
		delete match;
        }catch (std::exception e) {
            if (zmq_errno())
                std::cerr << zmq_strerror(zmq_errno()) << "\n";
            else
                std::cerr << e.what() << "\n";
        }
    }
    
    ConnectionThread(Options &opts, DeviceStatus &status, IODInterface &iod)
            : done(false), connection(-1), options(opts), device_status(status), iod_interface(iod)
    {
        assert(options.valid());
        msg_buffer =new char[ANET_ERR_LEN];
        gettimeofday(&last_active, 0);
        last_property_update.tv_sec = 0;
        last_property_update.tv_usec = 0;
        last_status.status = DeviceStatus::e_unknown;
    }
    
    void stop() {
        done = true;
        if (device_status.status == DeviceStatus::e_connected || device_status.status == DeviceStatus::e_up) {
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
        if (last_status.status != device_status.status || now.tv_sec >= last_property_update.tv_sec + 5) {
            last_property_update.tv_sec = now.tv_sec;
            last_property_update.tv_usec = now.tv_usec;
            last_status.status = device_status.status;
            switch(device_status.status) {
                case DeviceStatus::e_disconnected:
                    iod_interface.setProperty(options.name(), "status", "disconnected");
                    break;
                case DeviceStatus::e_connected:
                    iod_interface.setProperty(options.name(), "status", "connected");
                    break;
                case DeviceStatus::e_timeout:
                    iod_interface.setProperty(options.name(), "status", "timeout");
                    break;
                case DeviceStatus::e_unknown:
                    iod_interface.setProperty(options.name(), "status", "unknown");
                    break;
                case DeviceStatus::e_up:
                    iod_interface.setProperty(options.name(), "status", "running");
                    break;
                case DeviceStatus::e_failed:
                    iod_interface.setProperty(options.name(), "status", "failed");
                    break;
            }
        }
    }
    bool done;
    int listener;
    int connection;
    std::string last_msg;
    Options &options;
    DeviceStatus &device_status;
    IODInterface &iod_interface;
    char *msg_buffer;
    struct timeval last_active;
    DeviceStatus last_status;
    struct timeval last_property_update;
    boost::mutex connection_mutex;
    struct termios terminal_settings;
};


bool done = false;

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
        return false;
    }
    return true;
}


int main(int argc, const char * argv[])
{
    last_send.tv_sec = 0;
    last_send.tv_usec = 0;
    
    Options options;
    try {
    
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
        else if (strcmp(argv[i], "--name") == 0 && i < argc-1) {
            options.setName(argv[++i]);
        }
        else if (strcmp(argv[i], "--property") == 0 && i < argc-1) {
            options.setProperty(argv[++i]);
        }
        else if (strcmp(argv[i], "--pattern") == 0 && i < argc-1) {
            options.setPattern(argv[++i]);
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
        else {
            std::cerr << "Warning: parameter " << argv[i] << " not understood\n";
        }
    }
    if (!options.valid()) {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    
    DeviceStatus device_status;
    IODInterface iod_interface(options);
    
	ConnectionThread connection_thread(options, device_status, iod_interface);
	boost::thread monitor(boost::ref(connection_thread));
    
    struct timeval last_time;
    gettimeofday(&last_time, 0);
    if (!setup_signals()) {
        std::cerr << "Error setting up signals " << strerror(errno) << "\n";
    }
    
    while (!done && !connection_thread.done)
    {
        struct timeval now;
        gettimeofday(&now, 0);
        // TBD once the connection is open, check that data has been received within the last second
        if (device_status.status == DeviceStatus::e_up || device_status.status == DeviceStatus::e_timeout) {
            std::string msg;
            struct timeval last;
            connection_thread.get_last_message(msg, last);
            if (last_time.tv_sec != last.tv_sec && now.tv_sec > last.tv_sec + 1) {
                if (device_status.status == DeviceStatus::e_timeout)
                    std::cerr << "Warning: Device timeout\n";
                else
                    std::cout << "Warning: connection idle\n";
                last_time = last;
            }
        }
        // send a message to iod
        usleep(100);
    }

    iod_interface.stop();
    connection_thread.stop();
    monitor.join();
    }
    catch (std::exception e) {
        if (zmq_errno())
            std::cerr << zmq_strerror(zmq_errno()) << "\n";
        else
            std::cerr << e.what() << "\n";
    }
    return 0;
}

