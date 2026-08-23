#include "RecordApply.h"
#include "MachineClass.h"
#include "MachineInstance.h"
#include "cJSON.h"
#include "value.h"
#include <sstream>

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

std::string tableName(const MachineClass *mc) {
    if (!mc) {
        return "";
    }
    if (!mc->table_name.empty()) {
        return mc->table_name;
    }
    return lowercase(mc->name);
}

bool typeMatches(const MachineClass *mc, const std::string &type) {
    if (!mc || type.empty()) {
        return false;
    }
    if (mc->table_name == type || lowercase(mc->name) == lowercase(type) || mc->name == type) {
        return true;
    }
    return false;
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
    std::string keycol = mc->keyColumn();
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

static MachineClass *classForType(const std::string &type) {
    std::list<MachineClass *>::const_iterator it = MachineClass::all_machine_classes.begin();
    while (it != MachineClass::all_machine_classes.end()) {
        MachineClass *mc = *it++;
        if (mc && mc->is_record && typeMatches(mc, type)) {
            return mc;
        }
    }
    return 0;
}

static void applyFields(MachineInstance *m, const MachineClass *mc, cJSON *row) {
    if (!m || !mc || !row || row->type != cJSON_Object) {
        return;
    }
    m->beginDeferredPropertyNotify();
    cJSON *f = row->child;
    while (f) {
        if (f->string && !mc->propertyIsLocal(f->string)) {
            m->setValue(f->string, jsonToValue(f));
        }
        f = f->next;
    }
    m->endDeferredPropertyNotify();
}

static std::string instanceName(const MachineClass *mc, cJSON *keys, cJSON *row) {
    std::string keycol = mc->keyColumn();
    cJSON *item = 0;
    if (keys) {
        item = cJSON_GetObjectItem(keys, keycol.c_str());
    }
    if (!item && row) {
        item = cJSON_GetObjectItem(row, keycol.c_str());
    }
    Value v = jsonToValue(item);
    return mc->name + "#" + v.asString();
}

int applyRow(const std::string &type, cJSON *keys, cJSON *row) {
    MachineClass *mc = classForType(type);
    if (!mc) {
        return 0;
    }
    cJSON *effective_keys = keys;
    if ((!effective_keys || !effective_keys->child) && row && row->type == cJSON_Object) {
        effective_keys = row;
    }
    int n = 0;
    std::list<MachineInstance *>::iterator it = MachineInstance::begin();
    while (it != MachineInstance::end()) {
        MachineInstance *m = *it++;
        if (!m || !m->getStateMachine() || m->getStateMachine() != mc) {
            continue;
        }
        if (keyMatches(m, mc, effective_keys)) {
            applyFields(m, mc, row);
            ++n;
        }
    }
    if (n == 0 && row) {
        std::string name = instanceName(mc, effective_keys, row);
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
            ++n;
        }
    }
    return n;
}

} // namespace RecordApply
