#include "Dispatcher.h"
#include "Logger.h"
#include "MachineClass.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "Expression.h"
#include "RecordClass.h"
#include "SetOperationAction.h"
#include "SortListAction.h"
#include "ThreadSafeQueue.h"
#include "dynamic_value.h"
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
    RecordClass::mark(mc);
    RecordClass::setTable(mc, "customer");
    mc->setOption("id", Value(static_cast<int64_t>(0)));
    RecordClass::addKey(mc, "id");
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

    Predicate where_ann(new Predicate(Value("ITEM.name", Value::t_symbol)), opEQ,
                        new Predicate(Value("Ann", Value::t_string)));
    SetOperationActionTemplate tmpl_ann(-1, Value("Customer"), SymbolTable::Null, Value("all"),
                                        Value(""), soSelect, &where_ann, false);
    Action *act_ann = tmpl_ann.factory(ed);
    if ((*act_ann)() != Action::Complete || all->parameters.size() != 1) {
        std::cerr << "COPY WHERE ITEM.name==Ann expected 1, got " << all->parameters.size()
                  << "\n";
        return 10;
    }
    if (!all->parameters[0].machine ||
        all->parameters[0].machine->getValue("name").asString() != "Ann") {
        std::cerr << "COPY WHERE should keep Ann\n";
        return 11;
    }
    delete act_ann;
    all->parameters.clear();

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

    SizeValue sz("all");
    sz.setScope(ed);
    const Value &szv = sz();
    int64_t nsz = 0;
    if (!szv.asInteger(nsz) || nsz != 2) {
        std::cerr << "SIZE OF all expected 2\n";
        return 5;
    }
    SortListActionTemplate sortt(Value("all"), Value("name"), true);
    Action *sorta = sortt.factory(ed);
    if ((*sorta)() != Action::Complete) {
        std::cerr << "SORT BY PROPERTY name failed\n";
        return 6;
    }
    delete sorta;
    if (all->parameters.empty() || !all->parameters[0].machine ||
        all->parameters[0].machine->getValue("name").asString() != "Ann") {
        std::cerr << "sorted list should start with Ann\n";
        return 7;
    }
    PopListFrontValue take("all");
    Value first = take(ed);
    if (!first.cached_machine || first.cached_machine->getValue("name").asString() != "Ann") {
        std::cerr << "TAKE FIRST should be Ann\n";
        return 8;
    }
    if (all->parameters.size() != 1) {
        std::cerr << "TAKE FIRST should leave 1 member\n";
        return 9;
    }

    SetOperationActionTemplate bad_dest(-1, Value("Customer"), SymbolTable::Null, Value("cust_a"),
                                        Value(""), soSelect, 0, false);
    Action *bad_act = bad_dest.factory(ed);
    if ((*bad_act)() != Action::Failed) {
        std::cerr << "COPY ALL FROM RECORD to a non-LIST should fail\n";
        delete bad_act;
        return 12;
    }
    std::string berr = bad_act->error();
    delete bad_act;
    if (berr.find("LIST") == std::string::npos) {
        std::cerr << "expected LIST destination error, got: " << berr << "\n";
        return 13;
    }

    MachineClass *view = new MachineClass("CustomerWithCity");
    RecordClass::mark(view);
    RecordClass::setView(view, "customer_with_city");
    view->setOption("id", Value(static_cast<int64_t>(0)));
    RecordClass::addKey(view, "id");
    view->setOption("name", Value("", Value::t_string));
    view->setOption("city", Value("", Value::t_string));
    MachineInstance *v1 = MachineInstanceFactory::create("row_a", "CustomerWithCity");
    v1->setStateMachine(view);
    v1->setValue("id", Value(static_cast<int64_t>(1)));
    v1->setValue("name", Value("Ann", Value::t_string));
    v1->setValue("city", Value("Perth", Value::t_string));
    machines[v1->getName()] = v1;

    MachineInstance *from_view = MachineInstanceFactory::create("from_view", "LIST");
    from_view->setStateMachine(listc);
    machines[from_view->getName()] = from_view;
    SetOperationActionTemplate vcopy(-1, Value("CustomerWithCity"), SymbolTable::Null,
                                     Value("from_view"), Value(""), soSelect, 0, false);
    Action *vact = vcopy.factory(ed);
    if ((*vact)() != Action::Complete || from_view->parameters.size() != 1) {
        std::cerr << "COPY FROM view RECORD expected 1 member, got "
                  << from_view->parameters.size() << "\n";
        return 4;
    }
    delete vact;

    std::cout << "ok\n";
    return 0;
}
