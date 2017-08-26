#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/mutex.hpp>
#include "SharedWorkSet.h"

SharedWorkSet *SharedWorkSet::instance_ = 0;

SharedWorkSet *SharedWorkSet::instance() {
	if (!instance_) instance_ = new SharedWorkSet();
	return instance_;
}

void SharedWorkSet::add(MachineInstance *m) {
	boost::recursive_mutex::scoped_lock lock(mutex);
	busy_machines.insert(m);
}

void SharedWorkSet::remove(MachineInstance *m) {
	boost::recursive_mutex::scoped_lock scoped_lock(mutex);
	busy_machines.erase(m);
}

std::set<MachineInstance*>::iterator SharedWorkSet::erase(std::set<MachineInstance*>::iterator &iter) {
	boost::recursive_mutex::scoped_lock scoped_lock(mutex);
	return busy_machines.erase(iter);
}

bool SharedWorkSet::empty() {
	boost::recursive_mutex::scoped_lock scoped_lock(mutex);
	return busy_machines.empty();
}

size_t SharedWorkSet::size() {
	boost::recursive_mutex::scoped_lock scoped_lock(mutex);
	return busy_machines.size();
}

std::set<MachineInstance*>::iterator SharedWorkSet::begin() { return busy_machines.begin(); }
std::set<MachineInstance*>::iterator SharedWorkSet::end() { return busy_machines.end(); }
