#include "RateEstimatorInstance.h"
#include "CounterRateFilterSettings.h"
#include "IOComponent.h"
#include "Scheduler.h"
#include "SharedWorkSet.h"
#include <boost/thread/recursive_mutex.hpp>
#include <stdint.h>

extern uint64_t rate_calc_process_time;

void RateEstimatorInstance::setNeedsCheck() {
    //std::cout << _name << "::setNeedsCheck(), enabled: " << is_enabled
    //  << " has state machine? " << ( (state_machine) ? "yes" : "no") << "\n";
    MachineInstance::setNeedsCheck();
    SharedWorkSet::instance()->add(this);
}

void RateEstimatorInstance::idle() {
    MachineInstance *pos_m = lookup(parameters[0]);
    if (!pos_m) {
        return;
    }
    if (!settings) {
        if (settings) {
            delete settings;
        }
        if (pos_m->io_interface) {
            settings = new CounterRateFilterSettings(8);
        }
        else {
            settings = new CounterRateFilterSettings(8);
        }
    }
    if (is_enabled) { // && hasWork()) {
        double delta = 0;
        uint64_t curr_t =
            (pos_m->io_interface) ? pos_m->io_interface->read_time : rate_calc_process_time;

        delta = (double)(curr_t - settings->update_t) / 1000.0;
        if (!delta) {
            return;
        }
        MachineInstance::idle();

        const Value &pos_v = pos_m->getValue("VALUE");
        assert(pos_v.kind == Value::t_integer);
        assert(pos_v != SymbolTable::Null);
        long pos = pos_v.iValue;

        /*
                std::cout << _name
                    << " last_pos: " << settings->last_pos << " pos: " << pos_v.iValue
                    << " pos: " << pos_v.iValue
                    << " read time: " << curr_t
                    << " upd: " << settings->update_t
                    << " delta: " << delta
                    << " vel: " << ( (delta) ? (float)(pos - last_pos) * 1000.0/ delta : 0)<< "\n";
        */
        if (pos_m && pos_m->getValue("VALUE").asInteger(pos)) {
            setValue("VALUE", pos);
        }
        if (pos != settings->last_pos || settings->velocity) {
            Trigger *trigger = new Trigger(this, "RateEstimatorTimer");
            Scheduler::instance()->add(new ScheduledItem(10000, trigger));

            //Scheduler::instance()->add(
            //  new ScheduledItem(10000, new FireTriggerAction(this, trigger)));
            trigger->release();
        }
        settings->last_pos = pos;
    }
}

RateEstimatorInstance::RateEstimatorInstance(InstanceType instance_type)
    : MachineInstance(instance_type) {
    settings = 0;
    if (!idle_time) {
        idle_time = 50000;
    }
}
RateEstimatorInstance::RateEstimatorInstance(CStringHolder name, const char *type,
                                             InstanceType instance_type)
    : MachineInstance(name, type, instance_type) {
    settings = 0;

    if (!idle_time) {
        idle_time = 50000;
    }
}
RateEstimatorInstance::~RateEstimatorInstance() { delete settings; }

bool RateEstimatorInstance::setValue(const std::string &property, const Value &update,
                                     uint64_t authority) {
    Value new_value(update);
    if (property == "VALUE") {
        if (new_value.kind == Value::t_symbol) {
            new_value = lookup(new_value.sValue.c_str());
        }
        MachineInstance *pos = lookup(parameters[0]);
        if (pos && pos->io_interface) {
            settings->update_t = pos->io_interface->read_time;
        }
        else {
            settings->update_t = rate_calc_process_time;
        }
        long val;
        if (!new_value.asInteger(val)) {
            val = 0;
        }
        if (settings->property_changed) {
            settings->property_changed = false;
        }

        // use current time - start time as t0 to avoid floating point resolution issues
        if (settings->start_t == 0) {
            settings->start_t = settings->update_t;
        }
        uint64_t delta_t = settings->update_t - settings->start_t;
        settings->position = (int32_t)val;
        settings->readings.append(settings->position, delta_t);
        settings->velocity = settings->readings.rate() * 1000000;
        //std::cout << _name << " adding reading: " << (int32_t)val <<" " << delta_t << " "
        //<<  settings->velocity << " "<< settings->readings.front << " " << settings->readings.back <<"\n";
        //settings->velocity = (int32_t)filter((int32_t)settings->position);

        //reset once the buffers have been filled with zeros
        if (!settings->velocity) {
            ++settings->zero_count;
        }
        else {
            settings->zero_count = 0;
        }
        if (settings->zero_count > settings->readings.BUFSIZE) {
            // reset buffers;
            //settings->start_t = settings->update_t;
            settings->readings.reset();
        }

        MachineInstance::setValue(property, settings->velocity);
        MachineInstance::setValue("position", settings->position);
        return true;
    }
    else {
        return MachineInstance::setValue(property, new_value);
    }
}

long RateEstimatorInstance::filter(long val) {
    if (settings->readings.length() < 2) {
        return 0;
    }
    float speed = settings->readings.rate() * 1000000;
    return speed;
}
