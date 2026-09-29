#include "Dispatcher.h"
#include "Logger.h"
#include "MachineClass.h"
#include "MachineCommandAction.h"
#include "Channel.h"
#include "CopyPropertiesAction.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "DbNotify.h"
#include "IODCommands.h"
#include "RecordApply.h"
#include "RecordClass.h"
#include "SetOperationAction.h"
#include "ThreadSafeQueue.h"
#include "cJSON.h"
#include "library_globals.cpp"
#include <boost/thread/mutex.hpp>
#include <iostream>
#include <sstream>
#include <list>
#include <vector>
#include <zmq.hpp>

int main() {
    zmq::context_t *context = new zmq::context_t;
    MessagingInterface::setContext(context);
    Logger::instance();
    MessageLog::setMaxMemory(10000);
    boost::condition_variable_any cond;
    boost::shared_mutex mutex;
    SharedThreadSafeQueue<Package *> queue(cond, mutex);
    Dispatcher::create(queue);

    MachineClass *mc = new MachineClass("Customer");
    RecordClass::mark(mc);
    RecordClass::setTable(mc, "customer");
    mc->setOption("id", Value(static_cast<int64_t>(0)));
    RecordClass::addKey(mc, "id");
    mc->setOption("name", Value("", Value::t_string));
    mc->setOption("password", Value("", Value::t_string));
    mc->addPrivateColumn("password");
    mc->local_properties.insert("tmp");

    MachineInstance *cust = MachineInstanceFactory::create("cust", "Customer");
    cust->setStateMachine(mc);
    if (std::string(cust->getCurrentStateString()) != "empty") {
        std::cerr << "cust not empty after declare: " << cust->getCurrentStateString() << "\n";
        return 30;
    }
    // constructor params (id) must not dirty
    cust->setRecordApplyMode(true);
    cust->setValue("id", Value(static_cast<int64_t>(1)));
    cust->setValue("name", Value("", Value::t_string));
    cust->setValue("tmp", Value(true));
    cust->setRecordApplyMode(false);
    if (std::string(cust->getCurrentStateString()) != "empty") {
        std::cerr << "constructor params dirtied cust: " << cust->getCurrentStateString() << "\n";
        return 31;
    }
    // live column assign -> dirty
    if (cust->getValue("dirty").asString() != "") {
        std::cerr << "dirty list not empty before edit: '" << cust->getValue("dirty").asString()
                  << "'\n";
        return 70;
    }
    cust->setValue("name", Value("Ann", Value::t_string));
    if (std::string(cust->getCurrentStateString()) != "dirty") {
        std::cerr << "live column assign did not dirty: " << cust->getCurrentStateString() << "\n";
        return 32;
    }
    if (cust->getValue("dirty").asString() != "name") {
        std::cerr << "dirty list after name: '" << cust->getValue("dirty").asString() << "'\n";
        return 71;
    }
    cust->setValue("name", Value("Ann", Value::t_string));
    if (cust->getValue("dirty").asString() != "name") {
        std::cerr << "identical assign changed dirty list\n";
        return 72;
    }
    cust->setValue("id", Value(static_cast<int64_t>(5)));
    cust->setValue("id", Value(static_cast<int64_t>(1)));
    if (cust->getValue("dirty").asString() != "name" ||
        std::string(cust->getCurrentStateString()) != "dirty") {
        std::cerr << "key assign entered the dirty list: '" << cust->getValue("dirty").asString()
                  << "'\n";
        return 73;
    }
    cust->setValue("tmp", Value(false));
    cust->setValue("tmp", Value(true));
    if (cust->getValue("dirty").asString() != "name") {
        std::cerr << "LOCAL assign entered the dirty list\n";
        return 74;
    }
    cust->setValue("password", Value("nope", Value::t_string));
    if (cust->getValue("dirty").asString() != "name,password") {
        std::cerr << "private column missing from dirty list: '"
                  << cust->getValue("dirty").asString() << "'\n";
        return 75;
    }
    cust->setValue("not_a_column", Value("z", Value::t_string));
    if (cust->getValue("dirty").asString() != "name,password") {
        std::cerr << "non-column entered the dirty list: '" << cust->getValue("dirty").asString()
                  << "'\n";
        return 76;
    }
    machines[cust->getName()] = cust;

    cJSON *row = cJSON_Parse("{\"id\":1,\"name\":\"Fred\",\"tmp\":false,\"password\":\"secret\"}");
    int n = RecordApply::applyRow("customer", 0, row);
    cJSON_Delete(row);
    if (n < 1) {
        std::cerr << "apply wrote " << n << " instances\n";
        return 1;
    }
    if (cust->getValue("name").asString() != "Fred") {
        std::cerr << "name not applied: '" << cust->getValue("name").asString()
                  << "' n=" << n << " id=" << cust->getValue("id") << "\n";
        MachineInstance *alt = MachineInstance::find("Customer#1");
        if (alt) {
            std::cerr << "Customer#1 name=" << alt->getValue("name") << "\n";
        }
        return 2;
    }
    bool tmp = false;
    cust->getValue("tmp").asBoolean(tmp);
    if (!tmp) {
        std::cerr << "LOCAL tmp was overwritten\n";
        return 3;
    }
    if (std::string(cust->getCurrentStateString()) != "clean") {
        std::cerr << "cust not clean after APPLY: " << cust->getCurrentStateString() << "\n";
        return 33;
    }
    if (cust->getValue("dirty").asString() != "") {
        std::cerr << "APPLY left dirty list: '" << cust->getValue("dirty").asString() << "'\n";
        return 77;
    }
    cust->setRecordApplyMode(true);
    cust->setValue("name", Value("Temp", Value::t_string));
    cust->setRecordApplyMode(false);
    if (std::string(cust->getCurrentStateString()) != "clean" ||
        cust->getValue("dirty").asString() != "") {
        std::cerr << "record apply mode recorded a dirty column\n";
        return 78;
    }
    cust->setValue("name", Value("Fred", Value::t_string));
    if (cust->getValue("password").asString() != "secret") {
        std::cerr << "PRIVATE column was not applied\n";
        return 34;
    }
    if (!mc->propertyIsPrivate("password") || mc->propertyIsLocal("password")) {
        std::cerr << "PRIVATE flag wrong\n";
        return 35;
    }
    std::ostringstream desc;
    cust->describe(desc);
    if (desc.str().find("secret") != std::string::npos) {
        std::cerr << "PRIVATE column leaked in describe\n";
        return 36;
    }
    // Channel publish gate: PRIVATE and LOCAL columns must not be published.
    if (!Channel::isLocalOrPrivate(cust, Value("password", Value::t_string)) ||
        !Channel::isLocalOrPrivate(cust, Value("tmp", Value::t_string)) ||
        Channel::isLocalOrPrivate(cust, Value("name", Value::t_string))) {
        std::cerr << "Channel publish gate wrong for PRIVATE/LOCAL column\n";
        return 21;
    }

    cJSON *row2 = cJSON_Parse("{\"id\":2,\"name\":\"Ada\"}");
    n = RecordApply::applyRow("customer", 0, row2);
    cJSON_Delete(row2);
    MachineInstance *created = MachineInstance::find("Customer#2");
    if (!created || created->getValue("name").asString() != "Ada") {
        std::cerr << "did not create Customer#2\n";
        return 4;
    }
    if (machines.find("Customer#2") == machines.end() || machines["Customer#2"] != created) {
        std::cerr << "Customer#2 not registered for lookup\n";
        return 7;
    }

    MachineInstance *shadow = MachineInstanceFactory::create("cust_b", "Customer");
    shadow->setStateMachine(mc);
    shadow->setValue("id", Value(static_cast<int64_t>(1)));
    shadow->setValue("name", Value("", Value::t_string));
    machines[shadow->getName()] = shadow;
    cJSON *row_both = cJSON_Parse("{\"id\":1,\"name\":\"Both\"}");
    n = RecordApply::applyRow("customer", 0, row_both);
    cJSON_Delete(row_both);
    if (n < 2 || cust->getValue("name").asString() != "Both" ||
        shadow->getValue("name").asString() != "Both") {
        std::cerr << "two holders of the same key did not both update n=" << n << "\n";
        return 8;
    }

    IODCommandRecordApply cmd;
    std::vector<Value> params;
    params.push_back(Value("RECORD", Value::t_symbol));
    params.push_back(Value("APPLY", Value::t_symbol));
    params.push_back(Value("customer", Value::t_string));
    params.push_back(Value("{\"id\":1}", Value::t_string));
    params.push_back(Value("{\"id\":1,\"name\":\"Ned\"}", Value::t_string));
    if (cmd(params) != IODCommand::Success) {
        std::cerr << "RECORD APPLY command failed: " << cmd.error() << "\n";
        return 5;
    }
    if (cust->getValue("name").asString() != "Ned") {
        std::cerr << "command apply missed named instance\n";
        return 6;
    }

    const char *fanout =
        "{\"action\":\"update\",\"type\":\"customer\",\"keys\":{},"
        "\"row\":[{\"id\":1,\"name\":\"One\"},{\"id\":2,\"name\":\"Two\"}]}";
    std::vector<DbNotifyRow> notes;
    if (parseDbNotify(fanout, notes) != 2) {
        std::cerr << "notify parse expected 2 rows\n";
        return 9;
    }
    for (size_t i = 0; i < notes.size(); ++i) {
        cJSON *keys = cJSON_Parse(notes[i].keys_json.c_str());
        cJSON *row = cJSON_Parse(notes[i].row_json.c_str());
        RecordApply::applyRow(notes[i].type, keys, row);
        cJSON_Delete(keys);
        cJSON_Delete(row);
    }
    if (cust->getValue("name").asString() != "One" ||
        created->getValue("name").asString() != "Two") {
        std::cerr << "multi-row notify apply missed holders\n";
        return 10;
    }

    const char *delall =
        "{\"action\":\"delete\",\"type\":\"customer\",\"keys\":{},\"row\":[]}";
    std::vector<DbNotifyRow> delnotes;
    if (parseDbNotify(delall, delnotes) != 1 || delnotes[0].action != "delete") {
        std::cerr << "delete-all notify should parse as one delete row\n";
        return 20;
    }

    MachineClass *itemc = new MachineClass("Item");
    RecordClass::mark(itemc);
    RecordClass::setTable(itemc, "item");
    itemc->setOption("id", Value(static_cast<int64_t>(0)));
    RecordClass::addKey(itemc, "id");
    itemc->setOption("station", Value("", Value::t_string));
    MachineClass *listc = new MachineClass("LIST");
    listc->addState("empty");
    listc->addState("nonempty");
    MachineClass *edc = new MachineClass("Editor");
    MachineInstance *items = MachineInstanceFactory::create("items", "LIST");
    items->setStateMachine(listc);
    MachineInstance *ed = MachineInstanceFactory::create("ed", "Editor");
    ed->setStateMachine(edc);
    machines[items->getName()] = items;
    machines[ed->getName()] = ed;
    cJSON *i1 = cJSON_Parse("{\"id\":1,\"station\":\"A\"}");
    cJSON *i2 = cJSON_Parse("{\"id\":2,\"station\":\"B\"}");
    RecordApply::applyRow("item", 0, i1);
    RecordApply::applyRow("item", 0, i2);
    cJSON_Delete(i1);
    cJSON_Delete(i2);
    SetOperationActionTemplate fill(-1, Value("Item"), SymbolTable::Null, Value("items"), Value(""),
                                    soSelect, 0, false);
    Action *fill_act = fill.factory(ed);
    if ((*fill_act)() != Action::Complete || items->parameters.size() != 2) {
        std::cerr << "QUERY fill path: COPY after apply expected 2, got "
                  << items->parameters.size() << "\n";
        return 11;
    }
    delete fill_act;

    cJSON *delkeys = cJSON_Parse("{\"id\":2}");
    n = RecordApply::removeRow("customer", delkeys);
    cJSON_Delete(delkeys);
    if (n < 1 || MachineInstance::find("Customer#2")) {
        std::cerr << "removeRow did not drop Customer#2 cache n=" << n << "\n";
        return 12;
    }
    if (!MachineInstance::find("cust")) {
        std::cerr << "removeRow must not destroy named instances\n";
        return 13;
    }

    // delete-all: empty keys clears every cache instance of the class but
    // leaves named instances in place.
    cJSON *r3 = cJSON_Parse("{\"id\":3,\"name\":\"C3\"}");
    RecordApply::applyRow("customer", 0, r3);
    cJSON_Delete(r3);
    cJSON *r4 = cJSON_Parse("{\"id\":4,\"name\":\"C4\"}");
    RecordApply::applyRow("customer", 0, r4);
    cJSON_Delete(r4);
    if (!MachineInstance::find("Customer#3") || !MachineInstance::find("Customer#4")) {
        std::cerr << "delete-all setup failed\n";
        return 16;
    }
    cJSON *empty = cJSON_Parse("{}");
    n = RecordApply::removeRow("customer", empty);
    cJSON_Delete(empty);
    if (n < 2) {
        std::cerr << "delete-all removed " << n << " (expected >=2)\n";
        return 17;
    }
    if (MachineInstance::find("Customer#3") || MachineInstance::find("Customer#4")) {
        std::cerr << "delete-all did not clear cache instances\n";
        return 18;
    }
    if (!MachineInstance::find("cust")) {
        std::cerr << "delete-all must not destroy named instances\n";
        return 19;
    }
    if (std::string(cust->getCurrentStateString()) != "empty") {
        std::cerr << "delete-all did not reset named instance to empty: "
                  << cust->getCurrentStateString() << "\n";
        return 37;
    }
    if (cust->getValue("dirty").asString() != "") {
        std::cerr << "delete-all left dirty list: '" << cust->getValue("dirty").asString()
                  << "'\n";
        return 83;
    }
    if (cust->getValue("name").asString() != "") {
        std::cerr << "delete-all did not reset name to default\n";
        return 38;
    }

    IODCommandRecordRemove rmc;
    std::vector<Value> rmparams;
    rmparams.push_back(Value("RECORD", Value::t_symbol));
    rmparams.push_back(Value("REMOVE", Value::t_symbol));
    rmparams.push_back(Value("item", Value::t_string));
    rmparams.push_back(Value("{\"id\":1}", Value::t_string));
    if (rmc(rmparams) != IODCommand::Success) {
        std::cerr << "RECORD REMOVE command failed: " << rmc.error() << "\n";
        return 14;
    }
    if (MachineInstance::find("Item#1")) {
        std::cerr << "RECORD REMOVE left Item#1 in the map\n";
        return 15;
    }

    // COPY PROPERTIES onto a RECORD: columns that change become the dirty list.
    {
        MachineInstance *src = MachineInstanceFactory::create("src", "Customer");
        src->setStateMachine(mc);
        src->setRecordApplyMode(true);
        src->setValue("id", Value(static_cast<int64_t>(9)));
        src->setValue("name", Value("Copied", Value::t_string));
        src->setRecordApplyMode(false);
        machines[src->getName()] = src;

        MachineInstance *dst = MachineInstanceFactory::create("dst", "Customer");
        dst->setStateMachine(mc);
        dst->setRecordApplyMode(true);
        dst->setValue("id", Value(static_cast<int64_t>(9)));
        dst->setRecordApplyMode(false);
        machines[dst->getName()] = dst;

        CopyPropertiesActionTemplate cpt(Value("src"), Value("dst"));
        Action *cp_act = cpt.factory(ed);
        if ((*cp_act)() != Action::Complete) {
            std::cerr << "COPY PROPERTIES onto RECORD failed\n";
            return 41;
        }
        if (dst->getValue("name").asString() != "Copied") {
            std::cerr << "COPY PROPERTIES did not copy name\n";
            return 42;
        }
        if (std::string(dst->getCurrentStateString()) != "dirty" ||
            dst->getValue("dirty").asString() != "name") {
            std::cerr << "COPY PROPERTIES dirty='" << dst->getValue("dirty").asString()
                      << "' state=" << dst->getCurrentStateString() << "\n";
            return 43;
        }
        Action *cp_again = cpt.factory(ed);
        if ((*cp_again)() != Action::Complete ||
            std::string(dst->getCurrentStateString()) != "clean" ||
            dst->getValue("dirty").asString() != "") {
            std::cerr << "second COPY PROPERTIES of the same row stayed dirty: "
                      << dst->getValue("dirty").asString() << "\n";
            return 60;
        }
        delete cp_act;
        delete cp_again;
    }

    // COPY PROPERTIES from a JSON object (a row taken from a query-result LIST
    // via `x := TAKE FIRST FROM rows`) onto a RECORD: no machine source needed.
    {
        MachineInstance *dst2 = MachineInstanceFactory::create("dst2", "Customer");
        dst2->setStateMachine(mc);
        dst2->setRecordApplyMode(true);
        dst2->setValue("id", Value(static_cast<int64_t>(9)));
        dst2->setRecordApplyMode(false);
        machines[dst2->getName()] = dst2;

        // A LIST member is a JSON object (machine == nullptr, val == t_json).
        Value row(cJSON_Parse("{\"id\":9,\"name\":\"FromJSON\",\"tmp\":true}"));
        ed->setValue("row", row);

        CopyPropertiesActionTemplate cpt2(Value("row"), Value("dst2"));
        Action *cp2 = cpt2.factory(ed);
        if ((*cp2)() != Action::Complete) {
            std::cerr << "COPY PROPERTIES from JSON object failed\n";
            return 44;
        }
        if (dst2->getValue("name").asString() != "FromJSON") {
            std::cerr << "COPY PROPERTIES from JSON did not copy name: "
                      << dst2->getValue("name").asString() << "\n";
            return 45;
        }
        // `tmp` is a LOCAL property: it must not be projected onto the RECORD.
        if (dst2->getValue("tmp") != SymbolTable::Null) {
            std::cerr << "COPY PROPERTIES from JSON copied LOCAL property tmp\n";
            return 46;
        }
        if (std::string(dst2->getCurrentStateString()) != "dirty" ||
            dst2->getValue("dirty").asString() != "name") {
            std::cerr << "COPY PROPERTIES from JSON dirty='" << dst2->getValue("dirty").asString()
                      << "' state=" << dst2->getCurrentStateString() << "\n";
            return 47;
        }
        delete cp2;
    }

    // Generic (non-RECORD) MACHINE: COPY PROPERTIES from a JSON object sets plain
    // properties on any machine, not just RECORDs.
    {
        MachineClass *plainc = new MachineClass("PlainThing");
        MachineInstance *plain = MachineInstanceFactory::create("plain", "PlainThing");
        plain->setStateMachine(plainc);
        machines[plain->getName()] = plain;

        Value row2(cJSON_Parse("{\"alpha\":\"A\",\"beta\":42}"));
        ed->setValue("row2", row2);

        CopyPropertiesActionTemplate cpt3(Value("row2"), Value("plain"));
        Action *cp3 = cpt3.factory(ed);
        if ((*cp3)() != Action::Complete) {
            std::cerr << "COPY PROPERTIES from JSON onto generic MACHINE failed\n";
            return 48;
        }
        if (plain->getValue("alpha").asString() != "A") {
            std::cerr << "COPY PROPERTIES from JSON did not copy alpha: "
                      << plain->getValue("alpha").asString() << "\n";
            return 49;
        }
        int64_t beta = 0;
        if (!plain->getValue("beta").asInteger(beta) || beta != 42) {
            std::cerr << "COPY PROPERTIES from JSON did not copy beta: "
                      << plain->getValue("beta").asString() << "\n";
            return 50;
        }
        delete cp3;
    }

    // MachineCommand leak fix: an instance with COMMAND/RECEIVE/ENTER handlers
    // must be destroyed cleanly (no double-free / crash).
    {
        MachineClass *cmdc = new MachineClass("WithCommands");
        cmdc->addState("idle", true);
        cmdc->initial_state = State("idle");
        cmdc->commands.insert(
            std::make_pair("clear", new MachineCommandTemplate("clear", "idle")));
        cmdc->receives.insert(
            std::make_pair(Message("foo"), new MachineCommandTemplate("on_foo", "idle")));
        cmdc->enter_functions[Message("idle")] = new MachineCommandTemplate("enter_idle", "idle");
        MachineInstance *cm = MachineInstanceFactory::create("cm", "WithCommands");
        cm->setStateMachine(cmdc);
        MachineInstance::delete_later(cm);
        MachineInstance::delete_pending();
    }

    // MACHINE TABLE binding: a table-bound MACHINE receives APPLY by (type,key)
    // with projection, and APPLY does NOT setState on it (WHEN owns state).
    {
        MachineClass *panelc = new MachineClass("CustomerPanel");
        RecordClass::setTable(panelc, "customer");
        panelc->setOption("id", Value(static_cast<int64_t>(0)));
        RecordClass::addKey(panelc, "id");
        panelc->setOption("name", Value("", Value::t_string));
        panelc->local_properties.insert("state");
        panelc->setOption("state", Value("empty", Value::t_string));
        panelc->addState("idle", true);
        panelc->addState("active");
        panelc->initial_state = State("idle");
        panelc->default_state = State("idle");
        MachineInstance *panel = MachineInstanceFactory::create("panel", "CustomerPanel");
        panel->setStateMachine(panelc);
        panel->setValue("id", Value(static_cast<int64_t>(1)));
        machines[panel->getName()] = panel;

        cJSON *prow = cJSON_Parse("{\"id\":1,\"name\":\"Ann\",\"email\":\"x\"}");
        int pn = RecordApply::applyRow("customer", 0, prow);
        cJSON_Delete(prow);
        if (pn < 1) {
            std::cerr << "MACHINE TABLE apply wrote " << pn << " instances\n";
            return 50;
        }
        if (panel->getValue("name").asString() != "Ann") {
            std::cerr << "MACHINE TABLE name not applied\n";
            return 51;
        }
        if (std::string(panel->getCurrentStateString()) == "clean") {
            std::cerr << "MACHINE TABLE Clockwork STATE was set to clean by APPLY\n";
            return 52;
        }
        if (panel->getValue("state").asString() != "clean") {
            std::cerr << "MACHINE TABLE LOCAL state not clean after APPLY: "
                      << panel->getValue("state") << "\n";
            return 53;
        }
        if (panel->getValue("dirty").asString() != "") {
            std::cerr << "MACHINE TABLE APPLY left dirty list\n";
            return 56;
        }
        panel->setValue("name", Value("Bob", Value::t_string));
        if (panel->getValue("state").asString() != "dirty") {
            std::cerr << "MACHINE TABLE LOCAL state not dirty after column assign: "
                      << panel->getValue("state") << "\n";
            return 54;
        }
        if (std::string(panel->getCurrentStateString()) == "dirty") {
            std::cerr << "MACHINE TABLE Clockwork STATE was set to dirty\n";
            return 55;
        }
        if (panel->getValue("dirty").asString() != "name") {
            std::cerr << "MACHINE TABLE dirty list: '" << panel->getValue("dirty").asString()
                      << "'\n";
            return 57;
        }
        panel->setValue("id", Value(static_cast<int64_t>(9)));
        if (panel->getValue("dirty").asString() != "name" ||
            panel->getValue("state").asString() != "dirty") {
            std::cerr << "MACHINE TABLE key entered dirty list\n";
            return 58;
        }
        panel->setRowLifecycle("empty");
        if (panel->getValue("state").asString() != "empty" ||
            panel->getValue("dirty").asString() != "") {
            std::cerr << "MACHINE TABLE empty did not clear dirty\n";
            return 59;
        }
        MachineInstance *psrc = MachineInstanceFactory::create("panel_src", "CustomerPanel");
        psrc->setStateMachine(panelc);
        psrc->setRecordApplyMode(true);
        psrc->setValue("id", Value(static_cast<int64_t>(3)));
        psrc->setValue("name", Value("Sam", Value::t_string));
        psrc->setRecordApplyMode(false);
        machines[psrc->getName()] = psrc;
        std::list<std::string> only_id;
        only_id.push_back("id");
        CopyPropertiesActionTemplate key_only(Value("panel_src"), Value("panel"), only_id);
        Action *key_copy = key_only.factory(ed);
        if ((*key_copy)() != Action::Complete ||
            panel->getValue("dirty").asString() != "" ||
            panel->getValue("state").asString() != "clean") {
            std::cerr << "MACHINE TABLE key-only copy dirty='"
                      << panel->getValue("dirty").asString()
                      << "' state=" << panel->getValue("state").asString() << "\n";
            delete key_copy;
            return 84;
        }
        delete key_copy;
        int64_t panel_id = 0;
        if (!panel->getValue("id").asInteger(panel_id) || panel_id != 3) {
            std::cerr << "MACHINE TABLE key-only copy did not write id\n";
            return 85;
        }
        if (std::string(panel->getCurrentStateString()) == "dirty" ||
            std::string(panel->getCurrentStateString()) == "clean") {
            std::cerr << "MACHINE TABLE copy moved Clockwork STATE\n";
            return 86;
        }
        CopyPropertiesActionTemplate name_copy(Value("panel_src"), Value("panel"));
        Action *name_act = name_copy.factory(ed);
        if ((*name_act)() != Action::Complete ||
            panel->getValue("name").asString() != "Sam" ||
            panel->getValue("dirty").asString() != "name" ||
            panel->getValue("state").asString() != "dirty") {
            std::cerr << "MACHINE TABLE copy dirty='" << panel->getValue("dirty").asString()
                      << "' name=" << panel->getValue("name").asString() << "\n";
            delete name_act;
            return 87;
        }
        delete name_act;
    }

    // Column list: first-change order, full copy replaces, unchanged partial copy keeps it.
    {
        mc->setOption("email", Value("", Value::t_string));
        mc->setOption("age", Value(static_cast<int64_t>(0)));
        mc->setOption("code", Value("", Value::t_string));
        RecordClass::addUnique(mc, "code");
        mc->setOption("dirty", Value(true));

        MachineInstance *ord = MachineInstanceFactory::create("ord", "Customer");
        ord->setStateMachine(mc);
        if (ord->getValue("dirty").asString() != "" || ord->getValue("dirty").kind != Value::t_string) {
            std::cerr << "author OPTION dirty survived setStateMachine: "
                      << ord->getValue("dirty") << "\n";
            return 88;
        }
        ord->setValue("email", Value("a@b", Value::t_string));
        ord->setValue("name", Value("Ann", Value::t_string));
        ord->setValue("email", Value("a@b", Value::t_string));
        if (ord->getValue("dirty").asString() != "email,name") {
            std::cerr << "first-change order: '" << ord->getValue("dirty").asString() << "'\n";
            return 89;
        }
        ord->setValue("age", Value(static_cast<int64_t>(4)));
        ord->setValue("code", Value("ab", Value::t_string));
        if (ord->getValue("dirty").asString() != "email,name,age,code") {
            std::cerr << "age/unique append: '" << ord->getValue("dirty").asString() << "'\n";
            return 90;
        }
        ord->setValue("dirty", Value("hacked", Value::t_string));
        if (ord->getValue("dirty").asString() != "email,name,age,code" ||
            std::string(ord->getCurrentStateString()) != "dirty") {
            std::cerr << "assignment to dirty stuck: '" << ord->getValue("dirty").asString()
                      << "'\n";
            return 91;
        }
        ord->setRowLifecycle("empty");
        if (std::string(ord->getCurrentStateString()) != "empty" ||
            ord->getValue("dirty").asString() != "") {
            std::cerr << "empty did not clear the column list\n";
            return 92;
        }

        MachineInstance *full_src = MachineInstanceFactory::create("full_src", "Customer");
        full_src->setStateMachine(mc);
        full_src->setRecordApplyMode(true);
        full_src->setValue("id", Value(static_cast<int64_t>(7)));
        full_src->setValue("name", Value("New", Value::t_string));
        full_src->setValue("email", Value("e@e", Value::t_string));
        full_src->setValue("password", Value("", Value::t_string));
        full_src->setRecordApplyMode(false);
        MachineInstance *full_dst = MachineInstanceFactory::create("full_dst", "Customer");
        full_dst->setStateMachine(mc);
        full_dst->setRecordApplyMode(true);
        full_dst->setValue("id", Value(static_cast<int64_t>(7)));
        full_dst->setValue("name", Value("Old", Value::t_string));
        full_dst->setValue("email", Value("", Value::t_string));
        full_dst->setValue("password", Value("", Value::t_string));
        full_dst->setRecordApplyMode(false);
        full_dst->setValue("name", Value("Keep", Value::t_string));
        if (full_dst->getValue("dirty").asString() != "name") {
            std::cerr << "full-copy setup: '" << full_dst->getValue("dirty").asString() << "'\n";
            return 93;
        }
        machines[full_src->getName()] = full_src;
        machines[full_dst->getName()] = full_dst;
        CopyPropertiesActionTemplate full(Value("full_src"), Value("full_dst"));
        Action *full_act = full.factory(ed);
        if ((*full_act)() != Action::Complete ||
            full_dst->getValue("name").asString() != "New" ||
            full_dst->getValue("email").asString() != "e@e" ||
            full_dst->getValue("dirty").asString() != "email,name" ||
            std::string(full_dst->getCurrentStateString()) != "dirty") {
            std::cerr << "full COPY replaced list with '" << full_dst->getValue("dirty").asString()
                      << "'\n";
            delete full_act;
            return 94;
        }
        delete full_act;

        MachineInstance *same_src = MachineInstanceFactory::create("same_src", "Customer");
        same_src->setStateMachine(mc);
        same_src->setRecordApplyMode(true);
        same_src->setValue("id", Value(static_cast<int64_t>(8)));
        same_src->setValue("email", Value("a@b", Value::t_string));
        same_src->setRecordApplyMode(false);
        MachineInstance *same_dst = MachineInstanceFactory::create("same_dst", "Customer");
        same_dst->setStateMachine(mc);
        same_dst->setRecordApplyMode(true);
        same_dst->setValue("id", Value(static_cast<int64_t>(1)));
        same_dst->setValue("email", Value("a@b", Value::t_string));
        same_dst->setRecordApplyMode(false);
        same_dst->setValue("name", Value("Stay", Value::t_string));
        machines[same_src->getName()] = same_src;
        machines[same_dst->getName()] = same_dst;
        std::list<std::string> only_email_same;
        only_email_same.push_back("email");
        CopyPropertiesActionTemplate same_email(Value("same_src"), Value("same_dst"), only_email_same);
        Action *same_act = same_email.factory(ed);
        if ((*same_act)() != Action::Complete ||
            same_dst->getValue("dirty").asString() != "name" ||
            std::string(same_dst->getCurrentStateString()) != "dirty") {
            std::cerr << "unchanged partial copy cleared dirty: '"
                      << same_dst->getValue("dirty").asString() << "'\n";
            delete same_act;
            return 95;
        }
        delete same_act;
        std::list<std::string> only_key;
        only_key.push_back("id");
        CopyPropertiesActionTemplate key_copy(Value("same_src"), Value("same_dst"), only_key);
        Action *key_act = key_copy.factory(ed);
        int64_t copied_id = 0;
        if ((*key_act)() != Action::Complete ||
            !same_dst->getValue("id").asInteger(copied_id) || copied_id != 8 ||
            same_dst->getValue("dirty").asString() != "name") {
            std::cerr << "key-only partial copy changed the list: '"
                      << same_dst->getValue("dirty").asString() << "'\n";
            delete key_act;
            return 96;
        }
        delete key_act;

        MachineClass *viewc = new MachineClass("CustomerView");
        RecordClass::mark(viewc);
        RecordClass::setView(viewc, "customer_with_address");
        viewc->setOption("id", Value(static_cast<int64_t>(0)));
        RecordClass::addKey(viewc, "id");
        viewc->setOption("name", Value("", Value::t_string));
        MachineInstance *view = MachineInstanceFactory::create("view", "CustomerView");
        view->setStateMachine(viewc);
        view->setValue("name", Value("Via", Value::t_string));
        if (view->getValue("dirty").asString() != "name" ||
            std::string(view->getCurrentStateString()) != "dirty") {
            std::cerr << "VIEW dirty='" << view->getValue("dirty").asString() << "'\n";
            return 97;
        }
    }

    // Partial COPY PROPERTIES keeps columns already dirty and adds the ones it changes.
    {
        MachineInstance *ps = MachineInstanceFactory::create("psrc", "Customer");
        ps->setStateMachine(mc);
        ps->setRecordApplyMode(true);
        ps->setValue("id", Value(static_cast<int64_t>(4)));
        ps->setValue("name", Value("Ann", Value::t_string));
        ps->setValue("email", Value("eve@example", Value::t_string));
        ps->setRecordApplyMode(false);
        MachineInstance *pd = MachineInstanceFactory::create("pdst", "Customer");
        pd->setStateMachine(mc);
        pd->setRecordApplyMode(true);
        pd->setValue("id", Value(static_cast<int64_t>(4)));
        pd->setValue("name", Value("Ann", Value::t_string));
        pd->setValue("email", Value("", Value::t_string));
        pd->setRecordApplyMode(false);
        pd->setValue("name", Value("Bea", Value::t_string));
        if (pd->getValue("dirty").asString() != "name") {
            std::cerr << "partial setup dirty: '" << pd->getValue("dirty").asString() << "'\n";
            return 80;
        }
        machines[ps->getName()] = ps;
        machines[pd->getName()] = pd;
        std::list<std::string> only_email;
        only_email.push_back("email");
        CopyPropertiesActionTemplate partial(Value("psrc"), Value("pdst"), only_email);
        Action *pp = partial.factory(ed);
        if ((*pp)() != Action::Complete) {
            std::cerr << "partial COPY PROPERTIES failed\n";
            return 81;
        }
        if (pd->getValue("name").asString() != "Bea" ||
            pd->getValue("email").asString() != "eve@example" ||
            pd->getValue("dirty").asString() != "name,email" ||
            std::string(pd->getCurrentStateString()) != "dirty") {
            std::cerr << "partial COPY dirty='" << pd->getValue("dirty").asString()
                      << "' name=" << pd->getValue("name").asString()
                      << " email=" << pd->getValue("email").asString() << "\n";
            return 82;
        }
        delete pp;
    }

    MachineInstance::delete_pending();

    std::cout << "ok\n";
    return 0;
}
