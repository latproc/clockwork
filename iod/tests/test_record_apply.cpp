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
    cust->setValue("name", Value("Ann", Value::t_string));
    if (std::string(cust->getCurrentStateString()) != "dirty") {
        std::cerr << "live column assign did not dirty: " << cust->getCurrentStateString() << "\n";
        return 32;
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

    // COPY PROPERTIES onto a RECORD: projection + clean (not dirty).
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
        if (std::string(dst->getCurrentStateString()) != "clean") {
            std::cerr << "COPY PROPERTIES did not set clean: "
                      << dst->getCurrentStateString() << "\n";
            return 43;
        }
        delete cp_act;
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
            std::cerr << "MACHINE TABLE state was set to clean by APPLY\n";
            return 52;
        }
    }

    MachineInstance::delete_pending();

    std::cout << "ok\n";
    return 0;
}
