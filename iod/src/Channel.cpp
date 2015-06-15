#include <iostream>
#include <map>
#include <set>
#include <stdlib.h>
#include <string.h>
#include <boost/thread.hpp>
#include "Channel.h"
#include "MessageLog.h"
#include "MessageEncoding.h"
#include "value.h"
#include "MessagingInterface.h"
#include "SocketMonitor.h"
#include "ConnectionManager.h"
#include "Logger.h"
#include "ClientInterface.h"
#include "IODCommand.h"
#include "IODCommands.h"
#include "SyncRemoteStatesAction.h"


std::map<std::string, Channel*> *Channel::all = 0;
std::map< std::string, ChannelDefinition* > *ChannelDefinition::all = 0;

State ChannelImplementation::DISCONNECTED("DISCONNECTED");
State ChannelImplementation::CONNECTING("CONNECTING");
State ChannelImplementation::CONNECTED("CONNECTED");

std::map< MachineInstance *, MachineRecord> Channel::pending_items;

MachineRef::MachineRef() : refs(1) {
}

ChannelImplementation::~ChannelImplementation() { }


Channel::Channel(const std::string ch_name, const std::string type)
        : ChannelImplementation(), MachineInstance(ch_name.c_str(), type.c_str()),
            name(ch_name), port(0), mif(0), communications_manager(0),
			monit_subs(0), monit_pubs(0),
			connect_responder(0), disconnect_responder(0),
			monitor_thread(0),
			throttle_time(0), connections(0), aborted(false), subscriber_thread(0),
			cmd_client(0), cmd_server(0)
{
    if (all == 0) {
        all = new std::map<std::string, Channel*>;
    }
    (*all)[name] = this;
	if (::machines.find(ch_name) == ::machines.end())
		::machines[ch_name] = this;
	markActive(); // this machine performs state changes in the ::idle() processing
}

Channel::~Channel() {
    stop();
    std::set<MachineInstance*>::iterator iter = this->channel_machines.begin();
    while (iter != channel_machines.end()) {
        MachineInstance *machine = *iter++;
        machine->unpublish();
    }
	::machines.erase(_name);
    this->channel_machines.clear();
	busy_machines.erase(this);
	all_machines.remove(this);
	pending_state_change.erase(this);
    remove(name);
}

void Channel::syncInterfaceProperties(MachineInstance *m) {
	if ( definition()->updates_names.count(m->getName())
		|| definition()-> shares_names.count(m->getName())) {

		std::map<std::string, Value>::const_iterator found = definition()->updates_names.find(m->getName());
		if (found == definition()->updates_names.end()) {
			found = definition()->shares_names.find(m->getName());
			if (found == definition()->shares_names.end()) return;
		}
		const std::pair<std::string, Value>elem = *found;
		std::string interface_name(elem.second.asString());

		MachineClass *mc = MachineClass::find(interface_name.c_str());
		if (!mc) {
			NB_MSG << "Warning: Interface " << interface_name << " is not defined\n";
			return;
		}
		MachineInterface *mi = dynamic_cast<MachineInterface*>(mc);
		if (mi) {
			//NB_MSG << "sending properties for machine " << m->getName() << "\n";
			std::set<std::string>::iterator props = mi->property_names.begin();
			while (props != mi->property_names.end()) {
				const std::string &s = *props++;
				Value v = m->getValue(s);
				if (v != SymbolTable::Null) {
					char *cmd = MessageEncoding::encodeCommand("PROPERTY", m->getName(), s, v);
					std::string response;
					// TBD this can take some time. need to remember where we are up to and come back later
					sendMessage(cmd, *cmd_client, response);
				}
				else {
					//NB_MSG << "Note: machine " << m->getName() << " does not have a property "
					//	<< s << " corresponding to interface " << interface_name << "\n";
				}
			}
		}
		else {
			NB_MSG << "could not find the interface definition for " << m->getName() << "\n";
		}
	}
}

void Channel::syncRemoteStates() {
	if (definition()->isPublisher())  return; // publishers do not initially send the state of all machines
	if (isClient()) {
		std::set<MachineInstance*>::iterator iter = this->channel_machines.begin();
		while (iter != channel_machines.end()) {
			MachineInstance *m = *iter++;
			if (!m->isShadow()) {
				std::string state(m->getCurrentStateString());
				//NB_MSG << "Machine " << m->getName() << " current state: " << state << "\n";
				char buf[200];
				const char *msg = MessageEncoding::encodeState(m->getName(), state);
				std::string response;
				// TBD this can take some time. need to remember where we are up to and come back later
				sendMessage(msg, *cmd_client, response);

				// look at the interface defined for this machine and for each property
				// on the interface, send the current value
				syncInterfaceProperties(m);
			}
		}
	}
	else {
		std::set<MachineInstance*>::iterator iter = this->channel_machines.begin();
		while (iter != channel_machines.end()) {
			MachineInstance *m = *iter++;
			if (!m->isShadow()) {
				const char *state = m->getCurrentStateString();
				//NB_MSG << "Machine " << m->getName() << " current state: " << state << "\n";
				char buf[200];
				const char *msg = MessageEncoding::encodeState(m->getName(), state);
				std::string response;
				if (cmd_client)
					sendMessage(msg, *cmd_client, response);
				else
					sendStateChange(m, state);

				// look at the interface defined for this machine and for each property
				// on the interface, send the current value
				syncInterfaceProperties(m);
			}
		}
	}
}

Action::Status Channel::setState(State &new_state, bool resume) {
	Action::Status res = MachineInstance::setState(new_state, resume);
	if (res != Action::Complete) return res;
	if (new_state.getName() == "CONNECTED") {
		setNeedsCheck();
		enableShadows();
		SyncRemoteStatesActionTemplate srsat;
		enqueueAction(srsat.factory(this));
	}
	else if (new_state.getName() == "DISCONNECTED") {
		disableShadows();
		setNeedsCheck();
	}
	return res;
}

Action::Status Channel::setState(const char *new_state, bool resume) {
	Action::Status res = MachineInstance::setState(new_state, resume);
	if (res != Action::Complete) return res;
	if (strcmp(new_state, "CONNECTED") == 0) {
		setNeedsCheck();
		enableShadows();
		SyncRemoteStatesActionTemplate srsat;
		enqueueAction(srsat.factory(this));
	}
	else if (strcmp(new_state, "DISCONNECTED") == 0) {
		disableShadows();
		setNeedsCheck();
	}
	return res;
}

void Channel::start() {
	char tnam[100];
	int pgn_rc = pthread_getname_np(pthread_self(),tnam, 100);
	assert(pgn_rc == 0);
	//NB_MSG << "Channel " << name << " starting from thread "<< tnam << "\n";


	if (isClient()) {
		//if (definition()->monitors() || monitors()) startSubscriber();
		//else startClient();
		startSubscriber();
	}
	else {
		if (definition()->isPublisher()) startPublisher();
		else startServer();
	}
	started_ = true;
}

void Channel::stop() {
	//TBD fix machines that publish to this channel
    disableShadows();
	if (isClient()) {
		if (definition()->monitors() || monitors()) stopSubscriber();
	}
	else {
		if (definition()->updates() || updates()
		|| definition()->shares_machines() || shares_machines())
			stopSubscriber();
	}
	started_ = false;
}

void Channel::startChannels() {
	if (!all) return;
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        Channel *chn = (*iter).second; iter++;
		if (!chn->started()) chn->start();
	}
}
void Channel::stopChannels() {
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        Channel *chn = (*iter).second; iter++;
		if (!chn->started()) chn->stop();
	}
}


bool Channel::started() { return started_; }

void Channel::addConnection() {
	boost::mutex::scoped_lock(update_mutex);
	++connections;
	DBG_MSG << getName() << " client connected\n";
	if (connections == 1) {
		SetStateActionTemplate ssat(CStringHolder("SELF"), "CONNECTED" );
		enqueueAction(ssat.factory(this)); // execute this state change once all other actions are complete
/*
		if (!isClient()) {
			// dont' do this for pub/sub channels
			NB_MSG << "Channel " << name << " should inform client of machine states\n";
		}
		else {
			NB_MSG << "Channel " << name << " should send current state to shadows\n";
		}
 */
	}
 }

void Channel::dropConnection() {
	boost::mutex::scoped_lock(update_mutex);
	assert(connections);
	--connections;
	DBG_MSG << getName() << " client disconnected\n";
	if(!connections) {
		SetStateActionTemplate ssat(CStringHolder("SELF"), "DISCONNECTED" );
		enqueueAction(ssat.factory(this)); // execute this state change once all other actions are complete
	}
	assert(connections>=0);
	if (connections == 0) {
		//NB_MSG << "last connection dropped, stopping channel " << _name << "\n";
		stopServer();
		//delete this;
	}
}


Channel &Channel::operator=(const Channel &other) {
    assert(false);
    return *this;
}

std::ostream &Channel::operator<<(std::ostream &out) const  {
    out << "CHANNEL";
    return out;
}

std::ostream &operator<<(std::ostream &out, const Channel &m) {
    return m.operator<<(out);
}

bool Channel::operator==(const Channel &other) {
    return false;
}


void Channel::setPort(unsigned int new_port) {
    assert(port == 0);
    port = new_port;
}
unsigned int Channel::getPort() const { return port; }


int Channel::uniquePort(unsigned int start, unsigned int end) {
    int res = 0;
    char address_buf[40];
    while (true) {
        try{
            zmq::socket_t test_bind(*MessagingInterface::getContext(), ZMQ_PULL);
            res = random() % (end-start+1) + start;
            snprintf(address_buf, 40, "tcp://*:%d", res);
            test_bind.bind(address_buf);
            int linger = 0; // do not wait at socket close time
            test_bind.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
            //std::cout << "found available port " << res << "\n";
            break;
        }
        catch (zmq::error_t err) {
            if (zmq_errno() != EADDRINUSE) {
                break;
            }
        }
    }
    return res;
}

ChannelDefinition::ChannelDefinition(const char *n, ChannelDefinition *prnt)
		: MachineClass(n),parent(prnt), ignore_response(false) {
    if (!all) all = new std::map< std::string, ChannelDefinition* >;
    (*all)[n] = this;
	states.push_back("DISCONNECTED");
	states.push_back("CONNECTED");
	states.push_back("CONNECTING");
	default_state = State("DISCONNECTED");
	initial_state = State("DISCONNECTED");
	disableAutomaticStateChanges();
}

ChannelDefinition *ChannelDefinition::find(const char *name) {
    if (!all) return 0;
    std::map< std::string, ChannelDefinition* >::iterator found = all->find(name);
    if (found == all->end()) return 0;
    return (*found).second;
}
Channel *ChannelDefinition::instantiate(unsigned int port) {
    
    Channel *chn = Channel::findByType(name);
    if (!chn) chn = Channel::create(port, this);
    return chn;
}

bool ChannelDefinition::isPublisher() const { return ignore_response; }
void ChannelDefinition::setPublisher(bool which) { ignore_response = which; }

bool ChannelImplementation::monitors() const {
	return monitors_exports
		|| !monitors_names.empty()
		|| !monitors_patterns.empty()
		|| !monitors_properties.empty();
}

bool ChannelImplementation::updates() const {
	return !updates_names.empty();
}

bool ChannelImplementation::shares_machines() const {
	return !shares_names.empty();
}


class ChannelConnectMonitor : public EventResponder {
public:
	ChannelConnectMonitor(Channel *channel) : chn(channel) {}
	void operator()(const zmq_event_t &event_, const char* addr_) {
		chn->addConnection();
	}
	Channel *chn;
};
class ChannelDisconnectMonitor : public EventResponder {
public:
	ChannelDisconnectMonitor(Channel *channel) : chn(channel) {}
	void operator()(const zmq_event_t &event_, const char* addr_) {
		chn->dropConnection();
	}
	Channel *chn;
};

void Channel::startPublisher() {
	//NB_MSG << name << " startPublisher\n";
	if (mif) {
		NB_MSG << "Channel::startPublisher() called when mif is already allocated\n";
		return;
	}
	std::string socknam("inproc://");
	mif = MessagingInterface::create("*", port);
	monit_subs = new SocketMonitor(*mif->getSocket(), mif->getURL().c_str());

	connect_responder = new ChannelConnectMonitor(this);
	disconnect_responder = new ChannelDisconnectMonitor(this);
	//cmd_socket = &subscription_manager.setup;
	monit_subs->addResponder(ZMQ_EVENT_ACCEPTED, connect_responder);
	monit_subs->addResponder(ZMQ_EVENT_DISCONNECTED, disconnect_responder);
	monitor_thread = new boost::thread(boost::ref(*monit_subs));
	mif->start();
}

void Channel::startServer() {
	//NB_MSG << name << " startServer\n";
	if (mif) {
		NB_MSG << "Channel::startServer() called when mif is already allocated\n";
		return;
	}
	std::string socknam("inproc://");
	mif = MessagingInterface::create("*", port, eCHANNEL);
	monit_subs = new SocketMonitor(*mif->getSocket(), mif->getURL().c_str());

	connect_responder = new ChannelConnectMonitor(this);
	disconnect_responder = new ChannelDisconnectMonitor(this);
	monit_subs->addResponder(ZMQ_EVENT_ACCEPTED, connect_responder);
	monit_subs->addResponder(ZMQ_EVENT_DISCONNECTED, disconnect_responder);
	monitor_thread = new boost::thread(boost::ref(*monit_subs));
	mif->start();
}

void Channel::startClient() {
	//NB_MSG << name << " startClient\n";
	if (mif) {
		NB_MSG << "Channel::startClient() called when mif is already allocated\n";
		return;
	}
	std::string socknam("inproc://");
	Value host = getValue("host");
	if (host == SymbolTable::Null)
		host = "localhost";
	Value port_val = getValue("port");
	if (port_val == SymbolTable::Null) port = 5555; else {
		long remote_port;
		if (port_val.asInteger(remote_port)) port = (int)remote_port; else port = 5555;
	}

	mif = MessagingInterface::create(host.asString().c_str(), port, eCHANNEL);
	monit_subs = new SocketMonitor(*mif->getSocket(), mif->getURL().c_str());

	connect_responder = new ChannelConnectMonitor(this);
	disconnect_responder = new ChannelDisconnectMonitor(this);
	//cmd_socket = &subscription_manager.setup;
	monit_subs->addResponder(ZMQ_EVENT_ACCEPTED, connect_responder);
	monit_subs->addResponder(ZMQ_EVENT_DISCONNECTED, disconnect_responder);
	monitor_thread = new boost::thread(boost::ref(*monit_subs));
	mif->start();
}

void Channel::stopServer() {
	if (monitor_thread) {
		monit_subs->abort();
		delete monitor_thread;
		monitor_thread = 0;
	}
	if (monit_subs) {
		if (connect_responder) {
			monit_subs->removeResponder(ZMQ_EVENT_ACCEPTED, connect_responder);
			delete connect_responder;
			connect_responder = 0;
		}
		if (disconnect_responder) {
			monit_subs->removeResponder(ZMQ_EVENT_DISCONNECTED, disconnect_responder);
			delete disconnect_responder;
			disconnect_responder = 0;
		}
		delete monit_subs;
		monit_subs = 0;
	}
}

bool Channel::isClient() {
	return (definition_file != "dynamic");
}

void Channel::abort() { aborted = true; }

void Channel::operator()() {
	std::string thread_name(name);
	thread_name += " subscriber";
#ifdef __APPLE__
	pthread_setname_np(thread_name.c_str());
#else
	pthread_setname_np(pthread_self(), thread_name.c_str());
#endif

	/* Clients need to use a Subscription Manager to connect to the remote end
	 	and need to setup a host and port for that purpose.
	 	Servers need to monitor a subscriber socket
	 	Both cases use a cmd socket to communicate between the main thread and
	 	the Subscription Manager.
	 */
	if (isClient()){
		Value host = getValue("host");
		Value port_val = getValue("port");
		if (host == SymbolTable::Null)
			host = "localhost";
		long port = 0;
		if (!port_val.asInteger(port)) return;
		//char buf[150];
		////snprintf(buf, 150, "tcp://%s:%d", host.asString().c_str(), (int)port);
		std::cout << " channel " << _name << " starting subscription to " << host << ":" << port << "\n";
		communications_manager = new SubscriptionManager(definition()->name.c_str(),
														 eCHANNEL, host.asString().c_str(),(int)port);
		//NB_MSG << "Channel " << name << "::operator() subscriber thread initialising\n";
	}
	else
		communications_manager = new SubscriptionManager(definition()->name.c_str(), eCHANNEL);

	cmd_server = createCommandSocket(false);
	usleep(500);
	char start_cmd[20];
	//NB_MSG << "channel " << name << " thread waiting for start message\n";
	size_t start_len = cmd_server->recv(start_cmd, 20);
	if (!start_len) { NB_MSG << name << " error getting start message\n"; }
	cmd_server->send("ok", 2);
	zmq::pollitem_t *items;

	try {
		while (!aborted) {
			// clients have a socket to setup the channel, servers do not need it
			int num_poll_items = 0;
			{
				if (communications_manager->isClient())
					num_poll_items = 3;
				else
					num_poll_items = 2;
				items = new zmq::pollitem_t[num_poll_items];
				int idx = communications_manager->configurePoll(items);
				assert(idx < num_poll_items);
				items[idx].socket = *cmd_server;
				items[idx].events = ZMQ_POLLERR | ZMQ_POLLIN;
				items[idx].fd = 0;
				items[idx].revents = 0;
			}
			try {
				if (!communications_manager->checkConnections(items, num_poll_items, *cmd_server)) {
					usleep(100000);
					continue;
				}
			}
			catch(zmq::error_t err) {
				NB_MSG << "Channel " << name << " ZMQ error: " << zmq_strerror(errno) << "\n";
				continue;
			}

			if ( !(items[1].revents & ZMQ_POLLIN) || (items[1].revents & ZMQ_POLLERR) )
				continue;
			zmq::message_t update;
			communications_manager->subscriber().recv(&update, ZMQ_NOBLOCK);
			struct timeval now;
			gettimeofday(&now, 0);
			long len = update.size();
			char *data = (char *)malloc(len+1);
			memcpy(data, update.data(), len);
			data[len] = 0;
			//NB_MSG << "Channel " << name << " subscriber received: " << data << "\n";
			{
				IODCommand *command = parseCommandString(data);
				if (command) {
					boost::mutex::scoped_lock(update_mutex);
					pending_commands.push_back(command);
				}
			}
			free(data);
		}
	}
	catch (std::exception ex) {
		NB_MSG << "Channel " << name << " saw exception " << ex.what() << "\n";
	}
}

bool Channel::sendMessage(const char *msg, zmq::socket_t &sock, std::string &response) {
	char tnam[100];
	int pgn_rc = pthread_getname_np(pthread_self(),tnam, 100);
	assert(pgn_rc == 0);

	if (!subscriber_thread) {
		//NB_MSG << tnam << " Channel " << name << " sendMessage() sending " << msg << " directly\n";
		return ::sendMessage(msg, sock, response);
		/*safeSend(sock, msg, strlen(msg));
		usleep(10);
		zmq::message_t resp;
		char buf[200];
		size_t len;
		safeRecv(sock, buf, 200, true, len);*/
	}
	else {
		//NB_MSG << "Channel " << name << " sendMessage() sending " << msg << " through subscription manager thread\n";
		safeSend(*cmd_client, msg, strlen(msg));
		char response_buf[100];
		size_t rlen;
		safeRecv(*cmd_client, response_buf, 100, true, rlen);
		response = response_buf;
	}
	return true;
}

zmq::socket_t *Channel::createCommandSocket(bool client_endpoint) {
	char cmd_socket_name[100];
	snprintf(cmd_socket_name, 100, "inproc://%s_cmd", name.c_str());
	//snprintf(cmd_socket_name, 100, "tcp://localhost:5577");
	char *pos = pos = strchr(cmd_socket_name, ':')+1;
	while ( (pos = strchr(pos, ':'))  ) *pos = '-';
	//NB_MSG << "Channel " << name << " bound to command interface " << cmd_socket_name << "\n";

	if (client_endpoint) {
		//NB_MSG << "Channel " << name << " bound to interface " << cmd_socket_name << "\n";

		zmq::socket_t *sock = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REQ);
		sock->connect(cmd_socket_name);
		return sock;
	}
	else {
		zmq::socket_t *sock = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REP);
		try {
			sock->bind(cmd_socket_name);
			int linger = 0;
			sock->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
			return sock;
		}
		catch(std::exception ex) {
			assert(false);
			return 0;
		}
	}
}

void Channel::startSubscriber() {
	assert(!communications_manager);
	assert(isClient());

	if (definition_->isPublisher()) {

		Value host = getValue("host");
		Value port_val = getValue("port");
		if (host == SymbolTable::Null)
			host = "localhost";
		long port = 0;
		if (!port_val.asInteger(port)) return;
		//char buf[150];
		////snprintf(buf, 150, "tcp://%s:%d", host.asString().c_str(), (int)port);
		std::cout << " channel " << _name << " starting subscription to " << host << ":" << port << "\n";
		communications_manager = new SubscriptionManager(definition()->name.c_str(),
                              eCLOCKWORK, host.asString().c_str(), (int)port);
	}
	else {
		char tnam[100];
		int pgn_rc = pthread_getname_np(pthread_self(),tnam, 100);
		assert(pgn_rc == 0);
		//NB_MSG << "Channel " << name << " setting up subscriber thread and client side connection from thread " << tnam << "\n";

		subscriber_thread = new boost::thread(boost::ref(*this));

		// create a socket to communicate with the newly started subscriber thread
		cmd_client = createCommandSocket(true);

		// start the subcriber thread
		cmd_client->send("start",5);
		char buf[100];
		size_t buflen = cmd_client->recv(buf, 100);
		buf[buflen] = 0;
		//NB_MSG << "Channel " << name << " got response to start: " << buf << "\n";
	}
}

void Channel::stopSubscriber() {
	delete communications_manager;
	communications_manager = 0;
}

void ChannelDefinition::instantiateInterfaces() {
    // find all machines listed in the updates clauses of channel definitions
    if (!all) return; // no channel definitions to look through
    std::map< std::string, ChannelDefinition* >::iterator iter = all->begin();
    while (iter != all->end()) {
        const std::pair<std::string, ChannelDefinition*> item = *iter++;
        std::map<std::string, Value>::iterator updated_machines = item.second->updates_names.begin();
        while (updated_machines != item.second->updates_names.end()) {
            const std::pair<std::string, Value> &instance_name = *updated_machines++;
            MachineInstance *found = MachineInstance::find(instance_name.first.c_str());
            if (!found) { // instantiate a shadow to represent this machine
                if (item.second) {
                    MachineInstance *m = MachineInstanceFactory::create(instance_name.first.c_str(),
                                                                        instance_name.second.asString().c_str(),
                                                                        MachineInstance::MACHINE_SHADOW);
                    m->setDefinitionLocation("dynamic", 0);
					MachineClass *mc = MachineClass::find(instance_name.second.asString().c_str());
                    m->setProperties(mc->properties);
                    m->setStateMachine(mc);
                    m->setValue("startup_enabled", false);
                    machines[instance_name.first] = m;
                }
                else {
                    char buf[150];
                    snprintf(buf, 150, "Error: no interface named %s", item.first.c_str());
                    MessageLog::instance()->add(buf);
                }
            }
        }
		std::map<std::string, Value>::iterator i_shared = item.second->shares_names.begin();
		while (i_shared != item.second->shares_names.end()) {
			const std::pair<std::string, Value> &instance_name = *i_shared++;
			MachineInstance *found = MachineInstance::find(instance_name.first.c_str());
			if (!found) { // instantiate a shadow to represent this machine
				if (item.second) {
					MachineInstance *m = MachineInstanceFactory::create(instance_name.first.c_str(),
																		instance_name.second.asString().c_str(),
																		MachineInstance::MACHINE_SHADOW);
					m->setDefinitionLocation("dynamic", 0);
					MachineClass *mc = MachineClass::find(instance_name.second.asString().c_str());
					m->setProperties(mc->properties);
					m->setStateMachine(mc);
					m->setValue("startup_enabled", false);
					machines[instance_name.first] = m;
				}
				else {
					char buf[150];
					snprintf(buf, 150, "Error: no interface named %s", item.first.c_str());
					MessageLog::instance()->add(buf);
				}
			}
		}
    }
    // channels automatically setup monitoring on machines they update
    // when the channel is instantiated. At startup, however, some machines
    // may not have been defined when the channel is created so we need
    // to do an extra pass here.
    Channel::setupAllShadows();
}

Channel *Channel::create(unsigned int port, ChannelDefinition *defn) {
    assert(defn);
    char channel_name[100];
    snprintf(channel_name, 100, "%s:%d", defn->name.c_str(), port);
    Channel *chn = new Channel(channel_name, defn->name);

    chn->setPort(port);
    chn->setDefinition(defn);
	chn->setDefinitionLocation("dynamic", 0);
    chn->setStateMachine(defn);
		if (defn->getThrottleTime()) std::cout << " channel " << channel_name << " is throttled (" << defn->getThrottleTime() << ")\n";
		chn->setThrottleTime(defn->getThrottleTime());
    if (defn->monitors_exports) chn->monitors_exports = true;
    chn->modified();
    chn->setupShadows();
    chn->setupFilters();
    return chn;
}

void ChannelImplementation::modified() {
    struct timeval now;
    gettimeofday(&now, 0);
    last_modified = now.tv_sec * 1000000L + now.tv_usec;
}

void ChannelImplementation::checked() {
    struct timeval now;
    gettimeofday(&now, 0);
    last_checked = now.tv_sec * 1000000L + now.tv_usec;
}

static void copyJSONArrayToSet(cJSON *obj, const char *key, std::set<std::string> &res) {
    cJSON *items = cJSON_GetObjectItem(obj, key);
    if (items && items->type == cJSON_Array) {
        cJSON *item = items->child;
        while (item) {
            if (item->type == cJSON_String) res.insert(item->valuestring);
            item = item->next;
        }
    }
}

static void copyJSONArrayToMap(cJSON *obj, const char *key, std::map<std::string, Value> &res, const char *key_name = "property", const char *value_name = "type") {
    cJSON *items = cJSON_GetObjectItem(obj, key);
    if (items && items->type == cJSON_Array) {
        cJSON *item = items->child;
        while (item) {
            // we only collect items from the array that match our expected format of
            // property,value pairs
            if (item->type == cJSON_Object) {
                cJSON *js_prop = cJSON_GetObjectItem(item, key_name);
                cJSON *js_type = cJSON_GetObjectItem(item, value_name);
                cJSON *js_val = cJSON_GetObjectItem(item, "value");
                if (js_prop->type == cJSON_String) {
                    res[js_prop->valuestring] = MessageEncoding::valueFromJSONObject(js_val, js_type);
                }
            }
            item = item->next;
        }
    }
}

ChannelDefinition *ChannelDefinition::fromJSON(const char *json) {
    cJSON *obj = cJSON_Parse(json);
    if (!obj) return 0;
/*    std::map<std::string, Value> options;
*/
    cJSON *item = cJSON_GetObjectItem(obj, "identifier");
    std::string name;
    if (item && item->type == cJSON_String) {
        name = item->valuestring;
    }
    ChannelDefinition *defn = new ChannelDefinition(name.c_str());
    defn->identifier = name;
    item = cJSON_GetObjectItem(obj, "key");
    if (item && item->type == cJSON_String)
        defn->setKey(item->valuestring);
    item = cJSON_GetObjectItem(obj, "version");
    if (item && item->type == cJSON_String)
        defn->setVersion(item->valuestring);
    copyJSONArrayToSet(obj, "monitors", defn->monitors_names);
    copyJSONArrayToSet(obj, "monitors_patterns", defn->monitors_patterns);
    copyJSONArrayToMap(obj, "monitors_properties", defn->monitors_properties);
    item = cJSON_GetObjectItem(obj, "monitors_exports");
    if (item && item->type == cJSON_True)
        defn->monitors_exports = true;
    else if (item && item->type == cJSON_False)
        defn->monitors_exports = false;

    copyJSONArrayToMap(obj, "shares", defn->shares_names);
    copyJSONArrayToMap(obj, "updates", defn->updates_names);
    copyJSONArrayToSet(obj, "sends", defn->send_messages);
    copyJSONArrayToSet(obj, "receives", defn->recv_messages);
    return defn;
}

static cJSON *StringSetToJSONArray(std::set<std::string> &items) {
    cJSON *res = cJSON_CreateArray();
    std::set<std::string>::iterator iter = items.begin();
    while (iter != items.end()) {
        const std::string &str = *iter++;
        cJSON_AddItemToArray(res, cJSON_CreateString(str.c_str()));
    }
    return res;
}

static cJSON *MapToJSONArray(std::map<std::string, Value> &items, const char *key_name = "property", const char *value_name = "type") {
    cJSON *res = cJSON_CreateArray();
    std::map<std::string, Value>::iterator iter = items.begin();
    while (iter != items.end()) {
        const std::pair<std::string, Value> item = *iter++;
        cJSON *js_item = cJSON_CreateObject();
        cJSON_AddItemToObject(js_item, key_name, cJSON_CreateString(item.first.c_str()));
        cJSON_AddStringToObject(js_item, value_name, MessageEncoding::valueType(item.second).c_str());
        MessageEncoding::addValueToJSONObject(js_item, "value", item.second);
        cJSON_AddItemToArray(res, js_item);
    }
    return res;
}

char *ChannelDefinition::toJSON() {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "identifier", this->name.c_str());
    cJSON_AddStringToObject(obj, "key", this->psk.c_str());
    cJSON_AddStringToObject(obj, "version", this->version.c_str());
    cJSON_AddItemToObject(obj, "monitors", StringSetToJSONArray(this->monitors_names));
    cJSON_AddItemToObject(obj, "monitors_patterns", StringSetToJSONArray(this->monitors_patterns));
    cJSON_AddItemToObject(obj, "monitors_properties", MapToJSONArray(this->monitors_properties));
    if (this->monitors_exports)
        cJSON_AddTrueToObject(obj, "monitors_exports");
    else
        cJSON_AddFalseToObject(obj, "monitors_exports");
    cJSON_AddItemToObject(obj, "shares", MapToJSONArray(this->shares_names));
    cJSON_AddItemToObject(obj, "updates", MapToJSONArray(this->updates_names, "name","type"));
    cJSON_AddItemToObject(obj, "sends", StringSetToJSONArray(this->send_messages));
    cJSON_AddItemToObject(obj, "receives", StringSetToJSONArray(this->recv_messages));
    char *str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return str;
}



MessageHandler::MessageHandler() : data(0), data_size(0) {
}

MessageHandler::~MessageHandler() {
    if (data) free(data);
}

void MessageHandler::handleMessage(const char *buf, size_t len) {
    if (data) { free(data); data = 0; data_size = 0; }
    data = (char*)malloc(len);
    memcpy(data, buf, len);
}

bool MessageHandler::receiveMessage(zmq::socket_t &sock) {
    if (data) { free(data); data = 0; data_size = 0; }
    zmq::message_t update;
    if (sock.recv(&update, ZMQ_NOBLOCK)) {
        long len = update.size();
        data = (char *)malloc(len+1);
        memcpy(data, update.data(), len);
        data[len] = 0;
		data_size = len+1;
        return true;
    }
    return false;
}


Channel *Channel::find(const std::string name) {
    if (!all) return 0;
    std::map<std::string, Channel *>::iterator found = all->find(name);
    if (found == all->end()) return 0;
    return (*found).second;
}

void Channel::remove(const std::string name) {
    if (!all) return;
    std::map<std::string, Channel *>::iterator found = all->find(name);
    if (found == all->end()) return;
    all->erase(found);
}

void Channel::setDefinition(const ChannelDefinition *def) {
    definition_ = def;
}

void Channel::sendPropertyChange(MachineInstance *machine, const Value &key, const Value &val) {
    if (!all) return;
    std::string name = machine->fullName();
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        Channel *chn = (*iter).second; iter++;
        
        if (!chn->channel_machines.count(machine))
            continue;
        if (chn->filtersAllow(machine)) {
			if (chn->throttle_time) {
				//std::cout << chn->getName() << " throttling " << machine->getName() << " " << key << "\n";
				pending_items[machine].properties[key.asString()] = val;
			}
			else {
				//if (!chn->channel_machines(machine)) chn->channel_machines.insert(machine);
				if (chn->communications_manager) {
					std::string response;
					char *cmd = MessageEncoding::encodeCommand("PROPERTY", name, key, val); // send command
					if (chn->isClient())
						chn->sendMessage(cmd, chn->communications_manager->setup(),response);
					else
						chn->sendMessage(cmd, chn->communications_manager->subscriber(),response);
					//std::cout << "channel " << name << " got response: " << response << "\n";
					free(cmd);
				}
				else if (chn->mif) {
					char *cmd = MessageEncoding::encodeCommand("PROPERTY", name, key, val); // send command
					chn->mif->send(cmd);
					free(cmd);
				}
			}
        }
    }
}

// send the property changes that have been recorded for this machine
void Channel::sendPropertyChanges(MachineInstance *machine) {
    if (!all) return;
    std::string name = machine->fullName();
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        Channel *chn = (*iter).second; iter++;
        
        if (!chn->channel_machines.count(machine))
            continue;
				
		if (!chn->throttle_time) continue;

		std::map<MachineInstance *, MachineRecord>::iterator found = pending_items.find(machine);
		if (chn->filtersAllow(machine) && found != pending_items.end() ) {
			MachineRecord &machine = (*found).second;
			std::map<std::string, Value>::iterator iter = machine.properties.begin();
			while (iter != machine.properties.end()) {
				std::pair<std::string, Value> item = *iter;
				if (chn->communications_manager) {
					std::string response;
					char *cmd = MessageEncoding::encodeCommand("PROPERTY", name, item.first, item.second); // send command
					if (chn->isClient())
						chn->sendMessage(cmd, chn->communications_manager->setup(),response);
					else
						chn->sendMessage(cmd, chn->communications_manager->subscriber(),response);
					free(cmd);
				}
				else if (chn->mif) {
					char *cmd = MessageEncoding::encodeCommand("PROPERTY", name, item.first, item.second); // send command
					chn->mif->send(cmd);
					free(cmd);
				}
				iter = machine.properties.erase(iter);
			}
		}
    }
}

bool Channel::matches(MachineInstance *machine, const std::string &name) {
    if (definition()->monitors_names.count(name))
        return true;
    else if (channel_machines.count(machine))
        return true;
    return false; // only test the channel instance if the channel definition matches
}

bool Channel::patternMatches(const std::string &machine_name) {
    // no match on name but we still may match on pattern
    std::set<std::string>::iterator iter = monitors_patterns.begin();
    while (iter != monitors_patterns.end()) {
        const std::string &pattern = *iter++;
        rexp_info *rexp = create_pattern(pattern.c_str());
        if (!rexp->compilation_error) {
            if (execute_pattern(rexp, machine_name.c_str()) == 0) {
                return true;
            }
        }
        else {
            MessageLog::instance()->add(rexp->compilation_error);
            DBG_MSG << "Channel error: " << name << " " << rexp->compilation_error << "\n";
            return false;
        }
    }
    return false;
}

bool Channel::doesUpdate() {
    return definition()->updates_names.empty();
}

bool Channel::doesShare() {
	return definition()->shares_names.empty();
}

bool Channel::doesMonitor() {
    return  monitors_exports || definition()->monitors_exports
        || ! (definition()->monitors_names.empty() && definition()->monitors_patterns.empty() && definition()->monitors_properties.empty()
              && monitors_names.empty() && monitors_patterns.empty() && monitors_properties.empty());
}

bool Channel::filtersAllow(MachineInstance *machine) {
    if ((definition()->monitors_exports || monitors_exports) && !machine->modbus_exports.empty())
        return true;
    
    if (!matches(machine, name)) return false;
    
    if (monitors_names.empty() && monitors_patterns.empty() && monitors_properties.empty())
        return true;
    
    // apply channel specific filters
    size_t n = monitors_names.count(name);
    if (!monitors_names.empty() && n)
        return true;
    
    // if patterns are given and this machine doesn't match anything else, we try patterns
    if (!monitors_patterns.empty()) {
        if (patternMatches(machine->getName()))
            return true;
    }
    
    // if properties are given and this machine doesn't match anything else, we try properties
    if (!monitors_properties.empty()) {
        // TBD check properties
    }
    return false;
}

Channel *Channel::findByType(const std::string kind) {
    if (!all) return 0;
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        Channel *chn = (*iter).second; iter++;
        if (chn->_type == kind) return chn;
    }
    return 0;
}


void Channel::sendStateChange(MachineInstance *machine, std::string new_state) {
	char tnam[100];
	int pgn_rc = pthread_getname_np(pthread_self(),tnam, 100);
	assert(pgn_rc == 0);

	MachineShadowInstance *ms = dynamic_cast<MachineShadowInstance*>(machine);


	//NB_MSG << "Channel::sendStateChange " << machine->getName() << "->" << new_state << "\n";
	if (!all) return;
    std::string machine_name = machine->fullName();
	char *cmdstr = MessageEncoding::encodeState(machine_name, new_state); // send command

	std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        Channel *chn = (*iter).second; iter++;

		if (ms) {
			// shadowed machines don't send state changes on channels that update them
			if (chn->definition()->updates_names.count(machine->getName())) {
				//NB_MSG << " send state change ignored on shadow machine " << machine->getName() << "\n";
				continue;
			}
		}

		if (!chn->channel_machines.count(machine))
            continue;
        if (chn->filtersAllow(machine)) {
			if (!chn->isClient() && chn->communications_manager) {
				std::string response;
				safeSend(chn->communications_manager->subscriber(), cmdstr, strlen(cmdstr) );
			}
            else if (chn->communications_manager
                && chn->communications_manager->setupStatus() == SubscriptionManager::e_done ) {
				if (!chn->definition()->isPublisher()) {
					std::string response;
					chn->sendMessage(cmdstr, chn->communications_manager->setup(),response);
					//NB_MSG << tnam << ": channel " << chn->name << " got response: " << response << "\n";
				}
				else {
					safeSend(chn->communications_manager->subscriber(), cmdstr, strlen(cmdstr) );
				}
            }
            else if (chn->mif) {
                chn->mif->send(cmdstr);
            }
            else {
                char buf[150];
                snprintf(buf, 150,
					 "Warning: machine %s changed state but the channel is not connected",
					 machine->getName().c_str());
                MessageLog::instance()->add(buf);
            }
        }
    }
	free(cmdstr);
}

void Channel::sendCommand(MachineInstance *machine, std::string command, std::list<Value>*params) {
	char tnam[100];
	int pgn_rc = pthread_getname_np(pthread_self(),tnam, 100);
	assert(pgn_rc == 0);

	if (!all) return;
    std::string name = machine->fullName();
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        Channel *chn = (*iter).second; iter++;

        if (!chn->channel_machines.count(machine))
            continue;
        if (chn->filtersAllow(machine)) {
            //if (!chn->channel_machines.count(machine)) chn->channel_machines.insert(machine);
            if (chn->communications_manager
                && chn->communications_manager->setupStatus() == SubscriptionManager::e_done ) {
                std::string response;
                char *cmd = MessageEncoding::encodeCommand(command, params); // send command
				if (chn->isClient())
					chn->sendMessage(cmd, chn->communications_manager->setup(),response);
				else
					chn->sendMessage(cmd, chn->communications_manager->subscriber(),response);
                //NB_MSG << tnam << ": channel " << name << " got response: " << response << "\n";
				free(cmd);
            }
            else if (chn->mif) {
                char *cmd = MessageEncoding::encodeCommand(command, params); // send command
				//NB_MSG << "Channel " << name << " sending " << cmd << "\n";
                chn->mif->send(cmd);
				free(cmd);
            }
            else {
                char buf[150];
                snprintf(buf, 150, "Warning: machine %s changed state but the channel is not connected",
                machine->getName().c_str());
                MessageLog::instance()->add(buf);
            }
        }
    }
}


void ChannelDefinition::setKey(const char *s) {
    psk = s;
}

void ChannelDefinition::setIdent(const char *s) {
    identifier = s;
}
void ChannelDefinition::setVersion(const char *s) {
    version = s;
}
void ChannelDefinition::addShare(const char *nm, const char *if_nm) {
	shares_names[nm] = if_nm;
	modified();
}
void ChannelImplementation::addMonitor(const char *s) {
    DBG_MSG << "add monitor for " << s << "\n";
    monitors_names.insert(s);
    modified();
}
void ChannelImplementation::addIgnorePattern(const char *s) {
    DBG_MSG << "add " << s << " to ignore list\n";
    ignores_patterns.insert(s);
    modified();
}
void ChannelImplementation::removeIgnorePattern(const char *s) {
    DBG_MSG << "remove " << s << " from ignore list\n";
    ignores_patterns.erase(s);
    modified();
}
void ChannelImplementation::removeMonitor(const char *s) {
    DBG_MSG << "remove monitor for " << s;
	if (monitors_names.count(s)) {
	    monitors_names.erase(s);
    	modified();
		DBG_MSG << "\n";
	}
	else {
		DBG_MSG << "...not found\n";
		std::string pattern = "^";
		pattern += s;
		pattern += "$";
		addIgnorePattern(pattern.c_str());
	}
}
void ChannelImplementation::addMonitorPattern(const char *s) {
    monitors_patterns.insert(s);
    modified();
}
void ChannelImplementation::addMonitorProperty(const char *key,Value &val) {
    monitors_properties[key] = val;
    modified();
}
void ChannelImplementation::removeMonitorProperty(const char *key,Value &val) {
    monitors_properties.erase(key);
    modified();
    //TBD Bug here, we should be using a set< pair<string, Value> >, not a map
}
void ChannelImplementation::addMonitorExports() {
    monitors_exports = true;
    modified();
}
void ChannelImplementation::removeMonitorExports() {
    monitors_exports = false;
    modified();
}

void ChannelImplementation::removeMonitorPattern(const char *s) {
    monitors_patterns.erase(s);
    modified();
}
void ChannelDefinition::addUpdates(const char *nm, const char *if_nm) {
    updates_names[nm] = if_nm;
    modified();
}
void ChannelDefinition::addShares(const char *nm, const char *if_nm) {
	shares_names[nm] = if_nm;
	modified();
}
void ChannelDefinition::addSendName(const char *s) {
    send_messages.insert(s);
    modified();
}
void ChannelDefinition::addReceiveName(const char *s) {
    recv_messages.insert(s);
    modified();
}
void ChannelDefinition::addOptionName(const char *n, Value &v) {
    options[n] = v;
    modified();
}

/* When a channel is created and connected, instances of updated machines on that channel are enabled.
 */
#include "EnableAction.h"

void Channel::enableShadows() {
    // enable all machines that are updated by this channel
	// don't perform the enable operation here though,
	// queue them for later
    std::map<std::string, Value>::const_iterator iter = definition()->updates_names.begin();
    while (iter != definition()->updates_names.end()) {
        const std::pair< std::string, Value> item = *iter++;
        MachineInstance *m = MachineInstance::find(item.first.c_str());
        MachineShadowInstance *ms = dynamic_cast<MachineShadowInstance*>(m);
		if (ms) {
			//NB_MSG << "Channel " << name << " enabling shadow machine " << ms->getName() << "\n";
			channel_machines.insert(ms); // ensure the channel is linked to the shadow machine
			EnableActionTemplate ea(ms->getName().c_str());
			enqueueAction(ea.factory(ms));
		}
    }
	iter = definition()->shares_names.begin();
	while (iter != definition()->shares_names.end()) {
		const std::pair< std::string, Value> item = *iter++;
		MachineInstance *m = MachineInstance::find(item.first.c_str());
		MachineShadowInstance *ms = dynamic_cast<MachineShadowInstance*>(m);
		if (ms) {
			//NB_MSG << "Channel " << name << " enabling shadow machine " << ms->getName() << "\n";
			channel_machines.insert(ms); // ensure the channel is linked to the shadow machine
			EnableActionTemplate ea(ms->getName().c_str());
			enqueueAction(ea.factory(ms));
		}
	}
}

void Channel::disableShadows() {
    // disable all machines that are updated by this channel
    std::map<std::string, Value>::const_iterator iter = definition()->updates_names.begin();
    while (iter != definition()->updates_names.end()) {
        const std::pair< std::string, Value> item = *iter++;
        MachineInstance *m = MachineInstance::find(item.first.c_str());
        MachineShadowInstance *ms = dynamic_cast<MachineShadowInstance*>(m);
		if (ms) {
			//NB_MSG << "Channel " << name << " disabling shadow machine " << ms->getName() << "\n";
			ms->disable();
		}
    }
	iter = definition()->shares_names.begin();
	while (iter != definition()->shares_names.end()) {
		const std::pair< std::string, Value> item = *iter++;
		MachineInstance *m = MachineInstance::find(item.first.c_str());
		MachineShadowInstance *ms = dynamic_cast<MachineShadowInstance*>(m);
		if (ms) {
			//NB_MSG << "Channel " << name << " disabling shadow machine " << ms->getName() << "\n";
			ms->disable();
		}
	}
}

void Channel::setupShadows() {
    // decide if the machines updated on this channel are instantiated as
    // normal machines and if so, publish their changes to this channel.
    if (!definition_) definition_ = ChannelDefinition::find(this->_type.c_str());
    if (!definition_) {
        char buf[150];
        snprintf(buf, 150, "Error: channel definition %s for %s could not be found", name.c_str(), _type.c_str());
        MessageLog::instance()->add(buf);
        return;
    }
    
    std::map<std::string, Value>::const_iterator iter = definition()->updates_names.begin();
    while (iter != definition()->updates_names.end()) {
        const std::pair< std::string, Value> item = *iter++;
        MachineInstance *m = MachineInstance::find(item.first.c_str());
        MachineShadowInstance *ms = dynamic_cast<MachineShadowInstance*>(m);
        if (m && !ms) { // this machine is not a shadow.
            if (!channel_machines.count(m)) {
                m->publish();
                channel_machines.insert(m);
            }
        }
        else if (m) {
            // this machine is a shadow
        }
    }

	iter = definition()->shares_names.begin();
	while (iter != definition()->shares_names.end()) {
		const std::pair< std::string, Value> item = *iter++;
		MachineInstance *m = MachineInstance::find(item.first.c_str());
		MachineShadowInstance *ms = dynamic_cast<MachineShadowInstance*>(m);
		if (m && !ms) { // this machine is not a shadow.
			if (!channel_machines.count(m)) {
				m->publish();
				channel_machines.insert(m);
			}
		}
		else if (m) {
			// this machine is a shadow
		}
	}


    //- TBD should this only subscribe if channel monitors a machine?
}

// poll channels and return number of descriptors with activity
// if n is 0 the block of data will be reallocated,
// otherwise up to n items will be initialised
int Channel::pollChannels(zmq::pollitem_t * &poll_items, long timeout, int n) {
    if (!all) return 0;
    if (!n) {
        std::map<std::string, Channel*>::iterator iter = all->begin();
        while (iter != all->end()) {
            const std::pair<std::string, Channel *> &item = *iter++;
            Channel *chn = item.second;
            if (chn->communications_manager) n += chn->communications_manager->numSocks();
        }
        if (poll_items) delete poll_items;
		poll_items = 0;
        if (n) poll_items = new zmq::pollitem_t[n];
    }
	if (!poll_items) return 0;

    int count = 0;
    zmq::pollitem_t *curr = poll_items;
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        const std::pair<std::string, Channel *> &item = *iter++;
        Channel *chn = item.second;
        if (chn->communications_manager) {
            chn->communications_manager->checkConnections();
            if (chn->communications_manager && n >= count + chn->communications_manager->numSocks()) {
                count += chn->communications_manager->configurePoll(curr);
                chn->setPollItemBase(curr);
                curr += count;
            }
        }
    }
    int rc = 0;
    while (true) {
        try {
            
            int rc = zmq::poll(poll_items, count, timeout);
            if (rc == -1 && errno == EAGAIN) continue;
            break;
        }
        catch(zmq::error_t e) {
            if (errno == EINTR) continue;
            char buf[150];
            snprintf(buf, 150, "Channel error: %s", zmq_strerror(errno));
            MessageLog::instance()->add(buf);
            DBG_MSG << buf << "\n";
            break;
        }
    }
    //if (rc>0) std::cout << rc << " channels with activity\n";
    return rc;
}

void Channel::handleChannels() {
	if (!all) return;
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        const std::pair<std::string, Channel *> &item = *iter++;
        Channel *chn = item.second;
        chn->checkCommunications();
    }
}


void Channel::setPollItemBase(zmq::pollitem_t *base) {
    poll_items = base;
}

void Channel::enable() {
	if (enabled()) {
		NB_MSG << "Channel " << name << " is already enabled\n";
	}
	else {
		MachineInstance::enable();
	}
}

void Channel::disable() {
}

void Channel::checkCommunications() {
	if (!communications_manager) return;
	if (!communications_manager->ready()) return;
    bool ok = communications_manager->checkConnections();
    if (!ok) {
        if (communications_manager->monit_setup->disconnected()
			&& current_state.getId() != ChannelImplementation::DISCONNECTED.getId())
            setState(ChannelImplementation::DISCONNECTED);
        else if (!communications_manager->monit_setup->disconnected()) {
            if (communications_manager->setupStatus() == SubscriptionManager::e_waiting_connect
				&& current_state.getId() != ChannelImplementation::CONNECTING.getId()) {
                setState(ChannelImplementation::CONNECTING);
            }
        }
        return;
    }
    if ( current_state.getId() != ChannelImplementation::CONNECTED.getId())
        setState(ChannelImplementation::CONNECTED);
    
    if ( poll_items && !(poll_items[1].revents & ZMQ_POLLIN) && message_handler) {
        if (message_handler->receiveMessage(communications_manager->subscriber())) {
//            NB_MSG << "Channel got message: " << message_handler->data << "\n";
		}
    }
	{
		boost::mutex::scoped_lock(update_mutex);
		std::list<IODCommand*>::iterator iter = pending_commands.begin();
		while (iter != pending_commands.end()) {
			IODCommand *command = *iter;
			assert(command);
			(*command)();
//			NB_MSG << "Channel " << name << " processed command " << command->param(0) << "\n";
			iter = pending_commands.erase(iter);
			completed_commands.push_back(command);
		}
	}
	if (!completed_commands.empty()) {
		std::string response;
//		NB_MSG << "Channel " << name << " finishing off processed commands\n";
		std::list<IODCommand*>::iterator iter = completed_commands.begin();
		while (iter != completed_commands.end()) {
			IODCommand *cmd = *iter;
			iter = completed_commands.erase(iter);
//			NB_MSG << "deleting completed command: " << cmd->param(0) << "\n";
			delete cmd;
		}
		//sendMessage("process_commands", *cmd_client, response);
//		NB_MSG << "Channel " << name << " got cmd response " << response << "\n";

		// TBD should this go back to the originator?
	}

}

void Channel::setupAllShadows() {
    if (!all) return;
    std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        Channel *chn = (*iter).second; iter++;
        if (chn) chn->setupShadows();
    }
}

void Channel::setupFilters() {
    checked();
    // check if this channel monitors exports and if so, add machines that have exports
    if (definition()->monitors_exports || monitors_exports) {
        std::list<MachineInstance*>::iterator m_iter = MachineInstance::begin();
        while (m_iter != MachineInstance::end()) {
            MachineInstance *machine = *m_iter++;
            if (! machine->modbus_exports.empty() ) {
                this->channel_machines.insert(machine);
                machine->publish();
            }
        }
    }
        
    std::set<std::string>::iterator iter = definition()->monitors_patterns.begin();
    while (iter != definition()->monitors_patterns.end()) {
        const std::string &pattern = *iter++;
        rexp_info *rexp = create_pattern(pattern.c_str());
        if (!rexp->compilation_error) {
            std::list<MachineInstance*>::iterator m_iter = MachineInstance::begin();
            while (m_iter != MachineInstance::end()) {
                MachineInstance *machine = *m_iter++;
                if (machine && execute_pattern(rexp, machine->getName().c_str()) == 0) {
                    if (!this->channel_machines.count(machine)) {
                        machine->publish();
                        this->channel_machines.insert(machine);
                    }
                }
            }
        }
        else {
            MessageLog::instance()->add(rexp->compilation_error);
            DBG_MSG << "Channel error: " << definition()->name << " " << rexp->compilation_error << "\n";
        }
    }
    iter = definition()->monitors_names.begin();
    while (iter != definition()->monitors_names.end()) {
        const std::string &name = *iter++;
        MachineInstance *machine = MachineInstance::find(name.c_str());
        if (machine && !this->channel_machines.count(machine)) {
            machine->publish();
            this->channel_machines.insert(machine);
        }
    }
    std::map<std::string, Value>::const_iterator prop_iter = definition()->monitors_properties.begin();
    while (prop_iter != definition()->monitors_properties.end()) {
        const std::pair<std::string, Value> &item = *prop_iter++;
        std::list<MachineInstance*>::iterator m_iter = MachineInstance::begin();
        //std::cout << "setting up channel: searching for machines where " <<item.first << " == " << item.second << "\n";
        while (m_iter != MachineInstance::end()) {
            MachineInstance *machine = *m_iter++;
            if (machine && !this->channel_machines.count(machine)) {
                Value &val = machine->getValue(item.first);
                // match if the machine has the property and Null was given as the match value
                //  or if the machine has the property and it matches the provided value
                if ( val != SymbolTable::Null &&
                        (item.second == SymbolTable::Null || val == item.second) ) {
                    //std::cout << "found match " << machine->getName() <<"\n";
                    this->channel_machines.insert(machine);
                    machine->publish();
                }
            }
        }
    }
    
    iter = definition()->ignores_patterns.begin();
    while (iter != definition()->ignores_patterns.end()) {
	    const std::string &pattern = *iter++;
	    rexp_info *rexp = create_pattern(pattern.c_str());
	    if (!rexp->compilation_error) {
		    std::list<MachineInstance*>::iterator machines = MachineInstance::begin();
		    while (machines != MachineInstance::end()) {
			    MachineInstance *machine = *machines++;
			    if (machine && execute_pattern(rexp, machine->getName().c_str()) == 0) {
				    if (this->channel_machines.count(machine)) {
					    //std::cout << "unpublished " << machine->getName() << "\n";
					    machine->unpublish();
					    this->channel_machines.erase(machine);
				    }
			    }
		    }
	    }
	    else {
		    MessageLog::instance()->add(rexp->compilation_error);
		    DBG_MSG << "Channel error: " << definition()->name << " " << rexp->compilation_error << "\n";
	    }
    }
}

