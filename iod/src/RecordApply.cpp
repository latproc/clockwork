#include "RecordApply.h"
#include "MachineClass.h"
#include "MachineInstance.h"
#include "RecordClass.h"
#include "cJSON.h"
#include "value.h"
#include <map>
#include <sstream>
#include <vector>

namespace RecordApply {

std::string lowercase(const std::string &s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] >= 'A' && out[i] <= 'Z') {
            out[i] = static_cast<char>(out[i] - 'A' + 'a');
        }
    }
    return out;
}

std::string tableName(const MachineClass *mc) { return RecordClass::tableName(mc); }

bool typeMatches(const MachineClass *mc, const std::string &type) {
    if (!mc || type.empty()) {
        return false;
    }
    std::string table = RecordClass::tableName(mc);
    return table == type || lowercase(mc->name) == lowercase(type);
}

Value jsonToValue(cJSON *item) {
    if (!item) {
        return Value();
    }
    if (item->type == cJSON_Array || item->type == cJSON_Object) {
        return Value(clone_json(item));
    }
    return get_value(item);
}

static bool valuesMatch(const Value &have, const Value &want) {
    if (have == want) {
        return true;
    }
    if (have.asString() == want.asString()) {
        return true;
    }
    int64_t a = 0, b = 0;
    if (have.asInteger(a) && want.asInteger(b) && a == b) {
        return true;
    }
    return false;
}

bool keyMatches(MachineInstance *m, const MachineClass *mc, cJSON *keys) {
    if (!m || !mc) {
        return false;
    }
    std::string keycol = RecordClass::keyColumn(mc);
    if (keycol.empty()) {
        return false;
    }
    cJSON *item = 0;
    if (keys && keys->type == cJSON_Object) {
        item = cJSON_GetObjectItem(keys, keycol.c_str());
    }
    if (!item) {
        return false;
    }
    return valuesMatch(m->getValue(keycol), jsonToValue(item));
}

static void applyFields(MachineInstance *m, const MachineClass *mc, cJSON *row) {
    if (!m || !mc || !row || row->type != cJSON_Object) {
        return;
    }
    // Projection: write only declared non-LOCAL OPTIONs (ignore extra fields).
    const std::map<std::string, Value> &opts = mc->getOptions();
    m->setRecordApplyMode(true);
    m->beginDeferredPropertyNotify();
    cJSON *f = row->child;
    while (f) {
        if (f->string && opts.find(f->string) != opts.end() &&
            !mc->propertyIsLocal(f->string)) {
            m->setValue(f->string, jsonToValue(f));
        }
        f = f->next;
    }
    m->endDeferredPropertyNotify();
    m->setRecordApplyMode(false);
    m->setRowLifecycle("clean");
}

// Cache name only (`Customer#1`). Not a MachineClass field; named instances keep
// the names they were given in the program.
static std::string instanceName(const MachineClass *mc, cJSON *keys, cJSON *row) {
    std::string keycol = RecordClass::keyColumn(mc);
    cJSON *item = 0;
    if (keys) {
        item = cJSON_GetObjectItem(keys, keycol.c_str());
    }
    if (!item && row) {
        item = cJSON_GetObjectItem(row, keycol.c_str());
    }
    if (!item) {
        return ""; // no key column present; cannot name a cache instance
    }
    Value v = jsonToValue(item);
    return mc->name + "#" + v.asString();
}

int applyRow(const std::string &type, cJSON *keys, cJSON *row) {
    cJSON *effective_keys = keys;
    if ((!effective_keys || !effective_keys->child) && row && row->type == cJSON_Object) {
        effective_keys = row;
    }
    int n = 0;
    // A table may have several row classes (the RECORD plus any table-bound
    // MACHINEs); apply the row to every matching instance.
    std::list<MachineClass *>::const_iterator cit = MachineClass::all_machine_classes.begin();
    while (cit != MachineClass::all_machine_classes.end()) {
        MachineClass *mc = *cit++;
        if (!mc || !RecordClass::isRow(mc) || !typeMatches(mc, type)) {
            continue;
        }
        int class_hits = 0;
        std::list<MachineInstance *>::iterator it = MachineInstance::begin_records();
        while (it != MachineInstance::end_records()) {
            MachineInstance *m = *it++;
            if (!m || !m->getStateMachine() || m->getStateMachine() != mc) {
                continue;
            }
            if (keyMatches(m, mc, effective_keys)) {
                applyFields(m, mc, row);
                ++class_hits;
            }
        }
        // Only RECORD classes materialise a cache instance for a missing row.
        if (class_hits == 0 && row && RecordClass::isRecord(mc)) {
            std::string name = instanceName(mc, effective_keys, row);
            if (!name.empty()) {
                MachineInstance *existing = MachineInstance::find(name.c_str());
                MachineInstance *m = existing;
                if (!m) {
                    m = MachineInstanceFactory::create(name.c_str(), mc->name.c_str());
                    if (m) {
                        m->setStateMachine(mc);
                        machines[name] = m;
                    }
                }
                if (m) {
                    applyFields(m, mc, row);
                    ++class_hits;
                }
            }
        }
        n += class_hits;
    }
    return n;
}

static bool isCacheInstance(MachineInstance *m, const MachineClass *mc) {
    if (!m || !mc) {
        return false;
    }
    const std::string prefix = mc->name + "#";
    const std::string &nm = m->getName();
    return nm.size() > prefix.size() && nm.compare(0, prefix.size(), prefix) == 0;
}

static void unlinkFromLists(MachineInstance *target) {
    if (!target) {
        return;
    }
    std::list<MachineInstance *>::iterator it = MachineInstance::begin();
    while (it != MachineInstance::end()) {
        MachineInstance *m = *it++;
        if (!m || (m->_type != "LIST" && m->_type != "REFERENCE")) {
            continue;
        }
        size_t i = m->parameters.size();
        while (i > 0) {
            --i;
            if (m->parameters[i].machine == target) {
                if (MachineInstance::polling_delay) {
                    m->removeParameter(i);
                }
                else {
                    m->parameters.erase(m->parameters.begin() + i);
                }
            }
        }
    }
}

int removeRow(const std::string &type, cJSON *keys) {
    // Empty/absent keys means "delete all rows of this type": clear every held
    // cache instance. Named instances are program-owned and stay (only unlinked).
    const bool delete_all = !keys || keys->type != cJSON_Object || !keys->child;
    int n = 0;
    std::list<MachineClass *>::const_iterator cit = MachineClass::all_machine_classes.begin();
    while (cit != MachineClass::all_machine_classes.end()) {
        MachineClass *mc = *cit++;
        if (!mc || !RecordClass::isRow(mc) || !typeMatches(mc, type)) {
            continue;
        }
        std::vector<MachineInstance *> hit;
        std::list<MachineInstance *>::iterator it = MachineInstance::begin_records();
        while (it != MachineInstance::end_records()) {
            MachineInstance *m = *it++;
            if (!m || m->getStateMachine() != mc) {
                continue;
            }
            if (delete_all || keyMatches(m, mc, keys)) {
                hit.push_back(m);
            }
        }
        for (size_t i = 0; i < hit.size(); ++i) {
            MachineInstance *m = hit[i];
            unlinkFromLists(m);
            if (isCacheInstance(m, mc)) {
                machines.erase(m->getName());
                m->unregisterRecord();
                MachineInstance::delete_later(m);
            }
            else {
                // Named instance: reset to empty + non-KEY columns to class defaults.
                m->setRecordApplyMode(true);
                const std::map<std::string, Value> &opts = mc->getOptions();
                std::map<std::string, Value>::const_iterator oi = opts.begin();
                while (oi != opts.end()) {
                    if (!mc->propertyIsLocal(oi->first) &&
                        !(RecordClass::columnFlags(mc, oi->first) & RecordClass::COL_KEY)) {
                        m->setValue(oi->first, oi->second);
                    }
                    ++oi;
                }
                m->setRecordApplyMode(false);
                m->setRowLifecycle("empty");
            }
            ++n;
        }
    }
    return n;
}

} // namespace RecordApply
