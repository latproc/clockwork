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

State ChannelImplementation::CONNECTING("CONNECTING");
State ChannelImplementation::DISCONNECTED("DISCONNECTED");
State ChannelImplementation::DOWNLOADING("DOWNLOADING");
State ChannelImplementation::WAITSTART("WAITSTART");
State ChannelImplementation::UPLOADING("UPLOADING");
State ChannelImplementation::CONNECTED("CONNECTED");
State ChannelImplementation::ACTIVE("ACTIVE");

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
			DBG_MSG << "sending properties for machine " << m->getName() <<":" << mi->name << "\n";
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
					NB_MSG << "Note: machine " << m->getName() << " does not have a property "
						<< s << " corresponding to interface " << interface_name << "\n";
				}
			}
		}
		else {
			NB_MSG << "could not find the interface definition for " << m->getName() << "\n";
		}
	}
}

bool Channel::syncRemoteStates() {
	//DBG_MSG << "Channel " << name << " syncRemoteStates " << current_state << "\n";
	// publishers do not initially send the state of all machines
	if (definition()->isPublisher()) return false;
	if (current_state == ChannelImplementation::DISCONNECTED) return false;
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
		//safeSend(*cmd_client, "done", 4);
		std::string ack;
		sendMessage("done", *cmd_client, ack);
		DBG_MSG << "channel " << name << " got " << ack << " from server\n";
		SetStateActionTemplate ssat(CStringHolder("SELF"), "ACTIVE" );
		enqueueAction(ssat.factory(this)); // execute this state change once all other actions are
	}
	else {
		std::set<MachineInstance*>::iterator iter = this->channel_machines.begin();
		while (iter != channel_machines.end()) {
			MachineInstance *m = *iter++;
			if (!m->isShadow()) {
				const char *state = m->getCurrentStateString();
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
		SetStateActionTemplate ssat(CStringHolder("SELF"), "DOWNLOADING" );
		enqueueAction(ssat.factory(this)); // execute this state change once all other actions are
/*
 		if (cmd_client) {
			std::string ack;
			sendMessage("done", *cmd_client, ack);
		}
		else {
			assert(false);
			mif->send("done");
		}
*/
	}
	//DBG_MSG << "Channel " << name << " syncRemoteStatesDone\n";
	return true;
}

Action::Status Channel::setState(State &new_state, bool resume) {
	Action::Status res = MachineInstance::setState(new_state, resume);
	if (res != Action::Complete) {
		NB_MSG << "Action " << *this << " not complete\n";
		return res;
	}
	if (new_state == ChannelImplementation::CONNECTED) {
		DBG_MSG << name << " CONNECTED\n";
		setNeedsCheck();
		if (isClient()) {
			SetStateActionTemplate ssat(CStringHolder("SELF"), "DOWNLOADING" );
			enqueueAction(ssat.factory(this)); // execute this state change once all other actions are
		}
	}
	else if (new_state == ChannelImplementation::WAITSTART) {
		DBG_MSG << name << " WAITSTART\n";
	}
	else if (new_state == ChannelImplementation::UPLOADING) {
		DBG_MSG << name << " UPLOADING\n";
		setNeedsCheck();
		SyncRemoteStatesActionTemplate srsat;
		enqueueAction(srsat.factory(this));
	}
	else if (new_state == ChannelImplementation::DOWNLOADING) {
		DBG_MSG << name << " DOWNLOADING\n";
		std::string ack;
		if (isClient()) {
			sendMessage("status", *cmd_client, ack);
			DBG_MSG << "channel " << name << " got ack: " << ack << " to start request\n";
		}
		else {
			sendMessage("done", *cmd_client, ack);
			DBG_MSG << "channel " << name << " got ack: " << ack << " when finished upload\n";
		}
	}
	else if (new_state == ChannelImplementation::DISCONNECTED) {
		DBG_MSG << name << " DISCONNECTED\n";
		disableShadows();
		setNeedsCheck();
#if 0
		if (isClient()) {
			if (monitor_thread && monit_subs) {
				monit_subs->abort();
				monitor_thread->join();
				delete monit_subs;
				monit_subs = 0;
				delete monitor_thread;
				monitor_thread = 0;
			}
			if (communications_manager) delete communications_manager;
			communications_manager = 0;
		}
#endif
	}
	return res;
}

Action::Status Channel::setState(const char *new_state_cstr, bool resume) {
	State new_state(new_state_cstr);
	return MachineInstance::setState(new_state, resume);
}

void Channel::start() {
	char tnam[100];
	int pgn_rc = pthread_getname_np(pthread_self(),tnam, 100);
	assert(pgn_rc == 0);
	//DBG_MSG << "Channel " << name << " starting from thread "<< tnam << "\n";


	if (isClient()) {
		//if (definition()->monitors() || monitors()) startSubscriber();
		//else startClient();
		startSubscriber();
	}
	else {
		if (definition()->isPublisher()) startServer(eZMQ); else startSubscriber();
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
	DBG_MSG << getName() << " client number " << connections << " connected in state " << current_state << "\n";
	if (connections == 1) {
		if (isClient()){
			if (definition()->isPublisher()) {
				SetStateActionTemplate ssat(CStringHolder("SELF"), "ACTIVE" );
				enqueueAction(ssat.factory(this)); // execute this state change once all other actions are complete
			}
			else {
				SetStateActionTemplate ssat(CStringHolder("SELF"), "DOWNLOADING" );
				enqueueAction(ssat.factory(this)); // execute this state change once all other actions are complete
			}
		}
		else {
			if (definition()->isPublisher()) {
				SetStateActionTemplate ssat(CStringHolder("SELF"), "ACTIVE" );
				enqueueAction(ssat.factory(this)); // execute this state change once all other actions are complete
			}
			else {
				SetStateActionTemplate ssat(CStringHolder("SELF"), "WAITSTART" );
				enqueueAction(ssat.factory(this)); // execute this state change once all other actions are complete
			}
		}
	}
	else if (isClient())
		assert(false);
	else if (!definition()->isPublisher()) {
		if (current_state != ChannelImplementation::WAITSTART) {
			DBG_MSG << "Channel " << getName() << " waiting for start\n";
			SetStateActionTemplate ssat(CStringHolder("SELF"), "WAITSTART" );
			enqueueAction(ssat.factory(this)); // execute this state change once all other actions are complete
		}
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
		//DBG_MSG << "last connection dropped, stopping channel " << _name << "\n";
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
	states.push_back("WAITSTART");
	states.push_back("UPLOADING");
	states.push_back("DOWNLOADING");
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

Value ChannelDefinition::getValue(const char *property) {
	std::map<std::string, Value>::iterator iter = options.find(property);
	if (iter == options.end()) return SymbolTable::Null;
	return (*iter).second;
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

void Channel::startServer(ProtocolType proto) {
	//NB_MSG << name << " startServer\n";
	if (mif) {
		NB_MSG << "Channel::startServer() called when mif is already allocated\n";
		return;
	}
	if (monitor_thread) {
		NB_MSG << "Channel " << name << " already prepared as a server\n";
		return;
	}

	std::string socknam("inproc://");
	mif = MessagingInterface::create("*", port, proto);
	monit_subs = new SocketMonitor(*mif->getSocket(), mif->getURL().c_str());

	connect_responder = new ChannelConnectMonitor(this);
	disconnect_responder = new ChannelDisconnectMonitor(this);
	monit_subs->addResponder(ZMQ_EVENT_ACCEPTED, connect_responder);
	monit_subs->addResponder(ZMQ_EVENT_DISCONNECTED, disconnect_responder);
	monitor_thread = new boost::thread(boost::ref(*monit_subs));
	mif->start();
}

void Channel::startClient() {
	//DBG_MSG << name << " startClient\n";
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
	return;

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

// the client and server enter the downloading and uploading states
// in opposite orders.
void Channel::checkStateChange(std::string event) {
	DBG_MSG << "Received " << event << " in " << current_state << " on " << name << "\n";
	if (current_state == ChannelImplementation::DISCONNECTED) return;
	if (isClient()) {
		if ( current_state == ChannelImplementation::DOWNLOADING )
			setState(ChannelImplementation::UPLOADING);
		else if (current_state == ChannelImplementation::UPLOADING) {
			DBG_MSG << name << " -> ACTIVE\n";
			setState(ChannelImplementation::ACTIVE);
		}
		else {
			NB_MSG << "Unexpected channel state " << current_state << " on " << name << "\n";
			assert(false);
		}
	}
	else {
		// note that the start event may arrive before our switch to waitstart.
		// we rely on the client resending if necessary
		if ( current_state == ChannelImplementation::WAITSTART && event == "status" )
			setState(ChannelImplementation::UPLOADING);
		else if (current_state == ChannelImplementation::ACTIVE && event == "status")
			setState(ChannelImplementation::UPLOADING);
		else if (current_state == ChannelImplementation::UPLOADING)
			setState(ChannelImplementation::ACTIVE);
		else if (current_state == ChannelImplementation::DOWNLOADING)
			setState(ChannelImplementation::ACTIVE);
		else {
			NB_MSG << "Unexpected channel state " << current_state << " on " << name << "\n";
			assert(false);
		}
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
		//DBG_MSG << "Channel " << name << "::operator() subscriber thread initialising\n";
	}
	else {
		communications_manager = new SubscriptionManager(definition()->name.c_str(), eCHANNEL, "*", port);
	}

	cmd_server = createCommandSocket(false);
	usleep(500);
	char start_cmd[20];
	DBG_MSG << "channel " << name << " thread waiting for start message\n";
	size_t start_len = cmd_server->recv(start_cmd, 20);
	if (!start_len) { NB_MSG << name << " error getting start message\n"; }
	cmd_server->send("ok", 2);
	zmq::pollitem_t *items;
	DBG_MSG << "channel " << name << " thread received start message\n";

	//SetStateActionTemplate ssat(CStringHolder("SELF"), "CONNECTED" );
	//enqueueAction(ssat.factory(this)); // execute this state change once all other actions are

	try {
		while (!aborted && communications_manager) {
			// clients have a socket to setup the channel, servers do not need it
			int num_poll_items = 0;
			int subscriber_idx = 0;
			{
				if (communications_manager->isClient()) {
					num_poll_items = 3;
					subscriber_idx = 1;
				}
				else {
					num_poll_items = 2;
					subscriber_idx = 0;
				}
				items = new zmq::pollitem_t[num_poll_items];
				int idx = communications_manager->configurePoll(items);
				assert(idx < num_poll_items);
				items[idx].socket = *cmd_server;
				items[idx].events = ZMQ_POLLERR | ZMQ_POLLIN;
				items[idx].fd = 0;
				items[idx].revents = 0;
			}
			try {
				if (completed_commands.empty() && !communications_manager->checkConnections(items, num_poll_items, *cmd_server)) {
					usleep(1000);
					continue;
				}
			}
			catch(zmq::error_t err) {
				DBG_MSG << "Channel " << name << " ZMQ error: " << zmq_strerror(errno) << "\n";
				continue;
			}
			if (!completed_commands.empty()) {
				boost::mutex::scoped_lock(update_mutex);
				std::string response;
				DBG_MSG << "Channel " << name << " finishing off processed commands\n";
				std::list<IODCommand*>::iterator iter = completed_commands.begin();
				while (iter != completed_commands.end()) {
					IODCommand *cmd = *iter;
					iter = completed_commands.erase(iter);
					DBG_MSG << "deleting completed command: " << cmd->param(0) << "\n";
					if (cmd->done == IODCommand::Success) {
						DBG_MSG << cmd->param(0) <<" succeeded: " << cmd->result() << "\n";
					}
					else {
						DBG_MSG << cmd->param(0) <<" failed: " << cmd->error() << "\n";
					}
					delete cmd;
				}
				// TBD should this go back to the originator?
			}

			if (isClient() && current_state == ChannelImplementation::DOWNLOADING) {
				long current_time = getTimerVal()->iValue;
				if (current_time > 1000) {
					DBG_MSG << "channel " << name << " resending status request to server\n";
					SetStateActionTemplate ssat(CStringHolder("SELF"), "CONNECTED" );
					enqueueAction(ssat.factory(this)); // execute this state change once all other actions are
				}
			}

			if ( !(items[subscriber_idx].revents & ZMQ_POLLIN)
				|| (items[subscriber_idx].revents & ZMQ_POLLERR) )
				continue;
			zmq::message_t update;
			communications_manager->subscriber().recv(&update, ZMQ_NOBLOCK);
			struct timeval now;
			gettimeofday(&now, 0);
			long len = update.size();
			if (len == 0) continue;

			char *data = (char *)malloc(len+1);
			memcpy(data, update.data(), len);
			data[len] = 0;
			DBG_MSG << "Channel " << name << " subscriber received: " << data << "\n";
			if (strncmp(data, "done", len) == 0 || strncmp(data, "status", len) == 0) {
				checkStateChange(data);
			}
			else
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
		//DBG_MSG << tnam << " Channel " << name << " sendMessage() sending " << msg << " directly\n";
		return ::sendMessage(msg, sock, response);
		/*safeSend(sock, msg, strlen(msg));
		usleep(10);
		zmq::message_t resp;
		char buf[200];
		size_t len;
		safeRecv(sock, buf, 200, true, len);*/
	}
	else {
		DBG_MSG << "Channel " << name << " sendMessage() sending " << msg << " through a channel thread\n";
		safeSend(*cmd_client, msg, strlen(msg));
		char response_buf[100];
		size_t rlen;
		safeRecv(*cmd_client, response_buf, 100, true, rlen);
		response = response_buf;
		DBG_MSG << "Channel " << name << " sendMessage() got response for " << msg << " from a channel thread\n";

	}
	return true;
}

zmq::socket_t *Channel::createCommandSocket(bool client_endpoint) {
	char cmd_socket_name[100];
	snprintf(cmd_socket_name, 100, "inproc://%s_cmd", name.c_str());
	char *pos = pos = strchr(cmd_socket_name, ':')+1;
	while ( (pos = strchr(pos, ':'))  ) *pos = '-';

	if (client_endpoint) {
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

	if (definition_->isPublisher()) {
		assert(false); // untested code
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
		//DBG_MSG << "Channel " << name << " setting up subscriber thread and client side connection from thread " << tnam << "\n";

		subscriber_thread = new boost::thread(boost::ref(*this));

		// create a socket to communicate with the newly started subscriber thread
		cmd_client = createCommandSocket(true);

		// start the subcriber thread
		cmd_client->send("start",5);
		char buf[100];
		size_t buflen = cmd_client->recv(buf, 100);
		buf[buflen] = 0;

		connect_responder = new ChannelConnectMonitor(this);
		disconnect_responder = new ChannelDisconnectMonitor(this);
		if (isClient())
			communications_manager->monit_subs.addResponder(ZMQ_EVENT_CONNECTED, connect_responder);
		else
			communications_manager->monit_subs.addResponder(ZMQ_EVENT_ACCEPTED, connect_responder);
		communications_manager->monit_subs.addResponder(ZMQ_EVENT_DISCONNECTED, disconnect_responder);
		//DBG_MSG << "Channel " << name << " got response to start: " << buf << "\n";
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
					DBG_MSG << "Instantiating SHADOW " << instance_name.first << " for Channel " << item.second->name << "\n";
                    MachineInstance *m = MachineInstanceFactory::create(instance_name.first.c_str(),
							instance_name.second.asString().c_str(),
							MachineInstance::MACHINE_SHADOW);
                    m->setDefinitionLocation("dynamic", 0);
					MachineClass *mc = MachineClass::find(instance_name.second.asString().c_str());
                    m->setProperties(mc->properties);
                    m->setStateMachine(mc);
                    m->setValue("startup_enabled", false);
                    machines[instance_name.first] = m;
                    ::machines[instance_name.first] = m;
                }
                else {
                    char buf[150];
                    snprintf(buf, 150, "Error: no interface named %s", item.first.c_str());
                    MessageLog::instance()->add(buf);
					NB_MSG << buf << "\n";
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
					::machines[instance_name.first] = m;
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
						chn->sendMessage(cmd, chn->communications_manager->subscriber(),response); //setup()
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
						chn->sendMessage(cmd, chn->communications_manager->subscriber(),response);//setup()
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

	if (!all) return;
    std::string machine_name = machine->fullName();
	char *cmdstr = MessageEncoding::encodeState(machine_name, new_state); // send command

	std::map<std::string, Channel*>::iterator iter = all->begin();
    while (iter != all->end()) {
        Channel *chn = (*iter).second; iter++;
		if (chn->current_state != ChannelImplementation::ACTIVE) continue;
#if 0
		if (ms) {
			// shadowed machines don't send state changes on channels that update them
			if (chn->definition()->updates_names.count(machine->getName())) {
				//DBG_MSG << " send state change ignored on shadow machine " << machine->getName() << "\n";
				continue;
			}
		}
#endif
		if (!chn->channel_machines.count(machine))
            continue;
        if (chn->filtersAllow(machine)) {
			//DBG_MSG << "Channel " << chn->name << " sendStateChange " << machine->getName() << "->" << new_state << "\n";
			if (!chn->isClient() && chn->communications_manager) {
				std::string response;
				safeSend(chn->communications_manager->subscriber(), cmdstr, strlen(cmdstr) );
			}
            else if (chn->communications_manager
                  && chn->communications_manager->setupStatus() == SubscriptionManager::e_done ) {

				//IODCommand *cmd = new IODCommandSendStateChange(machine_name, new_state);
				//chn->pending_commands.push_back(cmd);

				if (!chn->definition()->isPublisher()) {
					std::string response;
					chn->sendMessage(cmdstr, chn->communications_manager->subscriber(),response);//setup()
					//DBG_MSG << tnam << ": channel " << chn->name << " got response: " << response << "\n";
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
					chn->sendMessage(cmd, chn->communications_manager->subscriber(),response);//setup()
				else
					chn->sendMessage(cmd, chn->communications_manager->subscriber(),response);
                //DBG_MSG << tnam << ": channel " << name << " got response: " << response << "\n";
				free(cmd);
            }
            else if (chn->mif) {
                char *cmd = MessageEncoding::encodeCommand(command, params); // send command
				//DBG_MSG << "Channel " << name << " sending " << cmd << "\n";
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
			//DBG_MSG << "Channel " << name << " enabling shadow machine " << ms->getName() << "\n";
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
			//DBG_MSG << "Channel " << name << " enabling shadow machine " << ms->getName() << "\n";
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
			m->publish();
            // this machine is a shadow
			channel_machines.insert(m);
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

// This method is executed on the main thread
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

// This method is executed on the main thread
void Channel::checkCommunications() {
	if (!communications_manager) return;
	if (!communications_manager->ready()) return;
    bool ok = communications_manager->checkConnections();
    if (!ok) {
		if (isClient()) {
			if (communications_manager->monit_setup->disconnected()
				&& current_state != ChannelImplementation::DISCONNECTED)
				setState(ChannelImplementation::DISCONNECTED);
			else if (!communications_manager->monit_setup->disconnected()) {
				if (communications_manager->setupStatus() == SubscriptionManager::e_waiting_connect
					&& current_state != ChannelImplementation::CONNECTING) {
						setState(ChannelImplementation::CONNECTING);
				}
			}
		}
        return;
	}
	if ( current_state == ChannelImplementation::DISCONNECTED) {
		setState(ChannelImplementation::CONNECTED);
	}
	else if ( current_state == ChannelImplementation::CONNECTED
			|| current_state == ChannelImplementation::CONNECTING) {
		if (isClient())
			setState(ChannelImplementation::DOWNLOADING);
		else
			setState(ChannelImplementation::UPLOADING);
	}

    if ( poll_items && !(poll_items[1].revents & ZMQ_POLLIN) && message_handler) {
        if (message_handler->receiveMessage(communications_manager->subscriber())) {
            NB_MSG << "Channel got message: " << message_handler->data << "\n";
		}
    }
	{
		boost::mutex::scoped_lock(update_mutex);
		std::list<IODCommand*>::iterator iter = pending_commands.begin();
		while (iter != pending_commands.end()) {
			IODCommand *command = *iter;
			assert(command);
			(*command)();
			NB_MSG << "Channel " << name << " processed command " << command->param(0) << "\n";
			iter = pending_commands.erase(iter);
			completed_commands.push_back(command);
		}
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

