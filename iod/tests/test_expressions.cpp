#include "gtest/gtest.h"
#include <Expression.h>
#include <MachineInstance.h>
#include <MessageLog.h>
#include <memory>
#include <symboltable.h>

#include "library_globals.c"
#if 0
#include "Statistic.h"
#include "Statistics.h"
#include <list>
    bool program_done = false;
    bool machine_is_ready = false;

    Statistics *statistics = NULL;
    std::list<Statistic *> Statistic::stats;
#endif

bool prep(Stack &stack, Predicate *p, MachineInstance *m, bool left, bool reevaluate);
ExprNode eval_stack(MachineInstance *m, std::list<ExprNode>::const_iterator &stack_iter);

namespace {

class Evaluator {
  private:
    Stack stack;

  public:
    Value evaluate(Predicate *p, MachineInstance *m);
};

Value Evaluator::evaluate(Predicate *predicate, MachineInstance *m) {
    if (!predicate || !m) {
        return SymbolTable::Null;
    }
    if (stack.stack.size() != 0) {
        stack.stack.clear();
    }
    if (stack.stack.size() == 0)
        if (!prep(stack, predicate, m, true, true)) {
            std::stringstream ss;
            ss << m->getName() << " Predicate failed to resolve: " << *predicate << "\n";
            MessageLog::instance()->add(ss.str().c_str());
            return false;
        }
    std::list<ExprNode>::const_iterator work = stack.stack.begin();
    ExprNode evaluated(eval_stack(m, work));
    return *(evaluated.val);
}

class EvaluatorTest : public ::testing::Test {
  protected:
    void SetUp() override {}
};

} // namespace

TEST_F(EvaluatorTest, simple_equality) {
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    Evaluator eval;
    Predicate pred(new Predicate(0), opEQ, new Predicate(0));
    EXPECT_EQ(Value(true), eval.evaluate(&pred, scope)) << "evaluates a simple equality";
    delete scope;
}

TEST_F(EvaluatorTest, simple_inequality) {
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    Evaluator eval;
    Predicate pred(new Predicate(1), opEQ, new Predicate(0));
    EXPECT_EQ(Value(false), eval.evaluate(&pred, scope))
        << "evaluates a simple equality that returns false";
    delete scope;
}

TEST_F(EvaluatorTest, simple_expression_equality) {
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    Evaluator eval;
    Predicate pred(new Predicate(3), opEQ,
                   new Predicate(new Predicate(1), opPlus, new Predicate(2)));
    EXPECT_EQ(Value(true), eval.evaluate(&pred, scope))
        << "evaluates a simple equality with a simple expression";
    delete scope;
}

#include <Dispatcher.h>
#include <Logger.h>
#include <MessagingInterface.h>
#include <zmq.hpp>

int main(int argc, char *argv[]) {
    zmq::context_t *context = new zmq::context_t;
    MessagingInterface::setContext(context);
    Logger::instance();
    Dispatcher::instance();
    zmq::socket_t dispatch_sync(*MessagingInterface::getContext(), ZMQ_REQ);
    dispatch_sync.connect("inproc://dispatcher_sync");

    ::testing::InitGoogleTest(&argc, argv);
    auto result = RUN_ALL_TESTS();

    MessagingInterface::abort();
    Dispatcher::instance()->stop();
    LogState::cleanup();
    Logger::cleanup();
    return result;
}
