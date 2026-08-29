#include "RecordClass.h"
#include "MachineClass.h"
#include "symboltable.h"
#include "value.h"

namespace RecordClass {

static const char *kRecord = "RECORD";
static const char *kTable = "TABLE";
static const char *kView = "VIEW";
static const char *kKey = "KEY";
static const char *kUnique = "UNIQUE";
static const char *kNotNull = "NOT_NULL";

static std::string lowercase(const std::string &s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] >= 'A' && out[i] <= 'Z') {
            out[i] = static_cast<char>(out[i] - 'A' + 'a');
        }
    }
    return out;
}

static std::string prop(const MachineClass *mc, const char *name) {
    if (!mc) {
        return "";
    }
    const Value &v = mc->getProperties().lookup(name);
    if (v == SymbolTable::Null) {
        return "";
    }
    return v.asString();
}

static bool csvHas(const std::string &csv, const std::string &name) {
    if (csv.empty() || name.empty()) {
        return false;
    }
    size_t start = 0;
    while (start <= csv.size()) {
        size_t comma = csv.find(',', start);
        std::string one =
            (comma == std::string::npos) ? csv.substr(start) : csv.substr(start, comma - start);
        if (one == name) {
            return true;
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return false;
}

static unsigned csvCount(const std::string &csv) {
    if (csv.empty()) {
        return 0;
    }
    unsigned n = 1;
    for (size_t i = 0; i < csv.size(); ++i) {
        if (csv[i] == ',') {
            ++n;
        }
    }
    return n;
}

static void appendCsv(MachineClass *mc, const char *name, const std::string &column) {
    if (!mc || column.empty()) {
        return;
    }
    std::string have = prop(mc, name);
    if (csvHas(have, column)) {
        return;
    }
    if (have.empty()) {
        mc->setProperty(name, Value(column.c_str(), Value::t_string));
    }
    else {
        mc->setProperty(name, Value((have + "," + column).c_str(), Value::t_string));
    }
}

static void hideSchemaProps(MachineClass *mc) {
    mc->addPrivateProperty(kRecord);
    mc->addPrivateProperty(kTable);
    mc->addPrivateProperty(kView);
    mc->addPrivateProperty(kKey);
    mc->addPrivateProperty(kUnique);
    mc->addPrivateProperty(kNotNull);
}

void mark(MachineClass *mc) {
    if (!mc) {
        return;
    }
    hideSchemaProps(mc);
    mc->setProperty(kRecord, Value(true));
    if (prop(mc, kTable).empty()) {
        mc->setProperty(kTable, Value(lowercase(mc->name).c_str(), Value::t_string));
    }
    // System states owned by iod/dbd (setState), not by author WHEN.
    mc->addState("empty", true);
    mc->addState("dirty", true);
    mc->addState("clean", true);
    mc->initial_state = State("empty");
    mc->default_state = State("empty");
}

void setTable(MachineClass *mc, const std::string &name) {
    if (!mc || name.empty()) {
        return;
    }
    hideSchemaProps(mc);
    mc->setProperty(kTable, Value(name.c_str(), Value::t_string));
}

void setView(MachineClass *mc, const std::string &name) {
    if (!mc || name.empty()) {
        return;
    }
    hideSchemaProps(mc);
    mc->setProperty(kView, Value(name.c_str(), Value::t_string));
    mc->setProperty(kTable, Value(name.c_str(), Value::t_string));
}

void addKey(MachineClass *mc, const std::string &column) { appendCsv(mc, kKey, column); }

void addUnique(MachineClass *mc, const std::string &column) { appendCsv(mc, kUnique, column); }

void addNotNull(MachineClass *mc, const std::string &column) { appendCsv(mc, kNotNull, column); }

void addFlags(MachineClass *mc, const std::string &column, unsigned flags) {
    if (flags & COL_KEY) {
        addKey(mc, column);
    }
    if (flags & COL_UNIQUE) {
        addUnique(mc, column);
    }
    if (flags & COL_NOT_NULL) {
        addNotNull(mc, column);
    }
}

bool isRecord(const MachineClass *mc) {
    return mc && mc->getProperties().exists(kRecord);
}

bool isView(const MachineClass *mc) { return mc && !prop(mc, kView).empty(); }

std::string tableName(const MachineClass *mc) {
    if (!mc) {
        return "";
    }
    std::string t = prop(mc, kTable);
    if (!t.empty()) {
        return t;
    }
    return lowercase(mc->name);
}

std::string keyColumn(const MachineClass *mc) {
    std::string keys = prop(mc, kKey);
    size_t comma = keys.find(',');
    if (comma == std::string::npos) {
        return keys;
    }
    return keys.substr(0, comma);
}

unsigned keyColumnCount(const MachineClass *mc) { return csvCount(prop(mc, kKey)); }

unsigned columnFlags(const MachineClass *mc, const std::string &column) {
    unsigned flags = COL_NONE;
    if (csvHas(prop(mc, kKey), column)) {
        flags |= COL_KEY;
    }
    if (csvHas(prop(mc, kUnique), column)) {
        flags |= COL_UNIQUE;
    }
    if (csvHas(prop(mc, kNotNull), column)) {
        flags |= COL_NOT_NULL;
    }
    return flags;
}

} // namespace RecordClass
