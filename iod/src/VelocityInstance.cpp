#include "VelocityInstance.h"

#include "Scheduler.h"
#include "SharedWorkSet.h"
#include "clockwork.h"
#include <algorithm>
#include <cmath>

extern uint64_t rate_calc_process_time;

VelocityInstance::VelocityInstance(InstanceType instance_type)
    : MachineInstance(instance_type), estimator_(), last_source_time_(0) {
    idle_time = 20000;
}

VelocityInstance::VelocityInstance(CStringHolder name, const char *type,
                                   InstanceType instance_type)
    : MachineInstance(name, type, instance_type), estimator_(), last_source_time_(0) {
    idle_time = 20000;
}

VelocityInstance::~VelocityInstance() {}

void VelocityInstance::setNeedsCheck() {
    MachineInstance::setNeedsCheck();
    SharedWorkSet::instance()->add(this);
}

void VelocityInstance::configureEstimator() {
    int64_t window_ms = 200;
    int64_t min_samples = 4;
    double scale = 1.0;
    int64_t stale_ms = 250;
    double max_abs_value = 0.0;

    getValue("WindowMs").asInteger(window_ms);
    getValue("MinSamples").asInteger(min_samples);
    getValue("Scale").asFloat(scale);
    getValue("StaleMs").asInteger(stale_ms);
    getValue("MaxAbsValue").asFloat(max_abs_value);

    estimator_.configure(
        static_cast<uint64_t>(std::max<int64_t>(1, window_ms)) * 1000,
        static_cast<size_t>(std::max<int64_t>(2, min_samples)),
        scale,
        static_cast<uint64_t>(std::max<int64_t>(0, stale_ms)) * 1000,
        std::max(0.0, max_abs_value));
}

void VelocityInstance::publishInternal(const VelocityEstimator::Result &result) {
    /*
     * prod-experimental-mqtt-fix is deliberately internal-only here. Consumers
     * read these properties on the existing control clock; publishing them with
     * setValue would add dependent/MQTT traffic at the estimator rate.
     */
    properties.add("VALUE", Value(result.value), SymbolTable::ST_REPLACE);
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
}

void VelocityInstance::idle() {
    if (!is_enabled || parameters.empty()) {
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
    const uint64_t now = source_time;
    if (source_time > last_source_time_) {
        estimator_.addSample(source_time, raw_value, now);
        last_source_time_ = source_time;
    }
    publishInternal(estimator_.evaluate(now));

    Trigger *next = new Trigger(this, "VelocityTimer");
    Scheduler::instance()->add(new ScheduledItem(static_cast<long>(idle_time), next));
    next->release();
}
