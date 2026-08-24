#include "Dispatcher.h"
#include "Logger.h"
#include "MachineClass.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "DbNotify.h"
#include "IODCommands.h"
#include "RecordApply.h"
#include "ThreadSafeQueue.h"
#include "cJSON.h"
#include "library_globals.cpp"
#include <boost/thread/mutex.hpp>
#include <iostream>
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
    mc->is_record = true;
    mc->table_name = "customer";
    mc->setOption("id", Value(static_cast<int64_t>(0)));
    mc->setColumnFlags("id", MachineClass::COL_KEY);
    mc->setOption("name", Value("", Value::t_string));
    mc->local_properties.insert("dirty");

    MachineInstance *cust = MachineInstanceFactory::create("cust", "Customer");
    cust->setStateMachine(mc);
    cust->setValue("id", Value(static_cast<int64_t>(1)));
    cust->setValue("name", Value("", Value::t_string));
    cust->setValue("dirty", Value(true));
    machines[cust->getName()] = cust;

    cJSON *row = cJSON_Parse("{\"id\":1,\"name\":\"Fred\",\"dirty\":false}");
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
    bool dirty = false;
    cust->getValue("dirty").asBoolean(dirty);
    if (!dirty) {
        std::cerr << "LOCAL dirty was overwritten\n";
        return 3;
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
    params.push_back(Value("RECORD_APPLY", Value::t_symbol));
    params.push_back(Value("customer", Value::t_string));
    params.push_back(Value("{\"id\":1}", Value::t_string));
    params.push_back(Value("{\"id\":1,\"name\":\"Ned\"}", Value::t_string));
    if (cmd(params) != IODCommand::Success) {
        std::cerr << "RECORD_APPLY command failed: " << cmd.error() << "\n";
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

    std::cout << "ok\n";
    return 0;
}
