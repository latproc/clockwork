#include <stdint.h>
#include <lib_clockwork_client/includes.hpp>
#include "MachineInstance.h"
// // #include "value.h"
#include "AutoStats.h"

AutoStatStorage::AutoStatStorage(const char *time_name, const char *rate_name, unsigned int reset_every)
	: start_time(0), last_update(0), total_polls(0), total_time(0),
		total_delays(0), rate_property(0), time_property(0),reset_point(reset_every) {
	if (time_name) time_property = setupPropertyRef("SYSTEM", time_name);
	if (rate_name) rate_property = setupPropertyRef("SYSTEM", rate_name);
}
void AutoStatStorage::reset() { last_update = 0; total_polls = 0; total_time = 0; }
void AutoStatStorage::update(uint64_t now, uint64_t duration) {
	++total_polls;
	if (reset_point && total_polls>reset_point) {
		total_time = 0;
		total_polls = 1;
	}
	if (time_property) {
		total_time += duration;
		long *tp = (long*)time_property;
		*tp = total_time / total_polls;
	}
	if (rate_property) {
		uint64_t delay = now - last_update;
		total_delays += delay;
		long *rp = (long*)rate_property;
		*rp = total_delays / total_polls;
	}
}
bool AutoStatStorage::running() { return start_time != 0; };
void AutoStatStorage::start() { start_time = microsecs(); }
void AutoStatStorage::stop() {
	if (start_time) {
		uint64_t now = microsecs(); update(now, now-start_time);
		start_time = 0;
	}
}
void AutoStatStorage::update() {
	uint64_t now = microsecs();
	if (start_time) {update(now, now-start_time); }
	start_time = now;
}

const long *AutoStatStorage::setupPropertyRef(const char *machine_name, const char *property_name) {
	MachineInstance *system = MachineInstance::find(machine_name);
	if (!system) return 0;
	const Value &prop(system->getValue(property_name));
	if (prop == SymbolTable::Null) {
		system->setValue(property_name, 0);
		const Value &prop(system->getValue(property_name));
		if (prop.kind != Value::t_integer) return 0;
		return &prop.iValue;
	}
	if (prop.kind != Value::t_integer) return 0;
	return &prop.iValue;
}

AutoStat::AutoStat(AutoStatStorage &storage) : storage_(storage) { start_time = microsecs(); }
AutoStat::~AutoStat() {
	uint64_t now = microsecs();
	storage_.update(now, now-start_time);
}
