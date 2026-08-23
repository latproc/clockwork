#include "Dispatcher.h"
#include "Logger.h"
#include "MachineClass.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "SetOperationAction.h"
#include "ThreadSafeQueue.h"
#include "library_globals.cpp"
#include <boost/thread/mutex.hpp>
#include <iostream>
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

    MachineClass *listc = new MachineClass("LIST");
    listc->addState("empty");
    listc->addState("nonempty");
    MachineClass *edc = new MachineClass("Editor");

    MachineInstance *a = MachineInstanceFactory::create("cust_a", "Customer");
    a->setStateMachine(mc);
    a->setValue("id", Value(static_cast<int64_t>(1)));
    a->setValue("name", Value("Ann", Value::t_string));
    MachineInstance *b = MachineInstanceFactory::create("cust_b", "Customer");
    b->setStateMachine(mc);
    b->setValue("id", Value(static_cast<int64_t>(2)));
    b->setValue("name", Value("Bob", Value::t_string));

    MachineInstance *all = MachineInstanceFactory::create("all", "LIST");
    all->setStateMachine(listc);
    MachineInstance *ed = MachineInstanceFactory::create("ed", "Editor");
    ed->setStateMachine(edc);
    machines[a->getName()] = a;
    machines[b->getName()] = b;
    machines[all->getName()] = all;
    machines[ed->getName()] = ed;

    SetOperationActionTemplate tmpl(-1, Value("Customer"), SymbolTable::Null, Value("all"),
                                    Value(""), soSelect, 0, false);
    Action *act = tmpl.factory(ed);
    if (!act) {
        std::cerr << "no action\n";
        return 1;
    }
    Action::Status st = (*act)();
    if (st != Action::Complete) {
        std::cerr << "copy failed: " << act->error() << "\n";
        return 2;
    }
    if (all->parameters.size() != 2) {
        std::cerr << "expected 2 list members, got " << all->parameters.size() << "\n";
        return 3;
    }
    delete act;
    std::cout << "ok\n";
    return 0;
}
