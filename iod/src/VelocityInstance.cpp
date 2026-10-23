#include "VelocityInstance.h"

#include "clockwork.h"
#include <algorithm>
#include <cmath>

extern uint64_t rate_calc_process_time;
std::list<VelocityInstance *> VelocityInstance::instances;

VelocityInstance::VelocityInstance(InstanceType instance_type)
    : MachineInstance(instance_type), estimator_(), last_source_time_(0), sample_period_us_(20000) {
    if (instance_type == MACHINE_INSTANCE) {
        instances.push_back(this);
    }
}

VelocityInstance::VelocityInstance(CStringHolder name, const char *type,
                                   InstanceType instance_type)
    : MachineInstance(name, type, instance_type), estimator_(), last_source_time_(0),
      sample_period_us_(20000) {
    if (instance_type == MACHINE_INSTANCE) {
        instances.push_back(this);
    }
}

VelocityInstance::~VelocityInstance() { instances.remove(this); }

namespace {

const char *state_name(VelocityEstimator::State state) {
    switch (state) {
    case VelocityEstimator::VALID:
        return "valid";
    case VelocityEstimator::STALE:
        return "stale";
    case VelocityEstimator::REJECTED:
        return "rejected";
    case VelocityEstimator::SETTLING:
    default:
        return "settling";
    }
}

} // namespace

void VelocityInstance::sampleAll(uint64_t sample_clock) {
    std::list<VelocityInstance *>::iterator iter = instances.begin();
    while (iter != instances.end()) {
        VelocityInstance *velocity = *iter++;
        if (velocity) {
            velocity->sampleSource(sample_clock);
        }
    }
}

void VelocityInstance::configureEstimator() {
    int64_t window_ms = 100;
    int64_t min_samples = 4;
    int64_t sample_ms = 20;
    double scale = 1.0;
    int64_t stale_ms = 250;
    double max_abs_value = 0.0;

    getValue("WindowMs").asInteger(window_ms);
    getValue("MinSamples").asInteger(min_samples);
    getValue("SampleMs").asInteger(sample_ms);
    getValue("Scale").asFloat(scale);
    getValue("StaleMs").asInteger(stale_ms);
    getValue("MaxAbsValue").asFloat(max_abs_value);

    estimator_.configure(
        static_cast<uint64_t>(std::max<int64_t>(1, window_ms)) * 1000,
        static_cast<size_t>(std::max<int64_t>(2, min_samples)),
        scale,
        static_cast<uint64_t>(std::max<int64_t>(0, stale_ms)) * 1000,
        std::max(0.0, max_abs_value));
    sample_period_us_ = static_cast<uint64_t>(std::max<int64_t>(1, sample_ms)) * 1000;
}

void VelocityInstance::publishInternal(const VelocityEstimator::Result &result) {
    /*
     * prod-experimental-mqtt-fix is deliberately internal-only here. Consumers
     * read these properties on the existing control clock; publishing them with
     * setValue would add dependent/MQTT traffic at the estimator rate.
     */
    properties.add("VALUE", Value(result.value), SymbolTable::ST_REPLACE);
    properties.add("Velocity", Value(result.value), SymbolTable::ST_REPLACE);
    properties.add("IOTIME", Value(static_cast<int64_t>(result.iotime)),
                   SymbolTable::ST_REPLACE);
    properties.add("RawValue", Value(result.raw_value), SymbolTable::ST_REPLACE);
    properties.add("RawIOTime", Value(static_cast<int64_t>(result.raw_iotime)),
                   SymbolTable::ST_REPLACE);
    properties.add("WindowStart", Value(static_cast<int64_t>(result.window_start)),
                   SymbolTable::ST_REPLACE);
    properties.add("WindowUs", Value(static_cast<int64_t>(result.window_us)),
                   SymbolTable::ST_REPLACE);
    properties.add("SampleCount", Value(static_cast<int64_t>(result.sample_count)),
                   SymbolTable::ST_REPLACE);
    properties.add("DeltaValue", Value(result.delta_value), SymbolTable::ST_REPLACE);
    properties.add("DeltaTimeUs", Value(static_cast<int64_t>(result.delta_time_us)),
                   SymbolTable::ST_REPLACE);
    properties.add("AgeMs", Value(static_cast<int64_t>(result.age_ms)),
                   SymbolTable::ST_REPLACE);
    properties.add("Valid", Value(result.valid), SymbolTable::ST_REPLACE);
    properties.add("Rejected", Value(result.rejected), SymbolTable::ST_REPLACE);
    properties.add("State", Value(state_name(result.state), Value::t_string),
                   SymbolTable::ST_REPLACE);
}

void VelocityInstance::sampleSource(uint64_t sample_clock) {
    if (parameters.empty()) {
        return;
    }
    MachineInstance *source = lookup(parameters[0]);
    if (!source) {
        return;
    }

    uint64_t source_time = rate_calc_process_time;
    int64_t source_iotime = 0;
    if (source->getValue("IOTIME").asInteger(source_iotime) && source_iotime > 0) {
        source_time = static_cast<uint64_t>(source_iotime);
    }
    double raw_value = 0.0;
    if (!source->getValue("VALUE").asFloat(raw_value)) {
        return;
    }

    configureEstimator();
    if (source_time > last_source_time_ &&
        (last_source_time_ == 0 || source_time - last_source_time_ >= sample_period_us_)) {
        estimator_.addSample(source_time, raw_value, sample_clock);
        last_source_time_ = source_time;
    }
    publishInternal(estimator_.evaluate(sample_clock));
}
