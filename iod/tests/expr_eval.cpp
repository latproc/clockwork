// this test evaluates expressions without using the googletest framework
// because valgrind complains about some googletest issues
//
#include "cw_test.h"
#include <Expression.h>
#include <MachineInstance.h>
#include <MessageLog.h>
#include <memory>
#include <symboltable.h>
#include <json_expression.h>
#include <cJSON.h>
#include <json_expr_parser.h>
#include <ThreadSafeQueue.h>
#include <Message.h>

#include "library_globals.cpp"

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

} // namespace

#include <Dispatcher.h>
#include <Logger.h>
#include <MessagingInterface.h>
#include <zmq.hpp>

class ExpressionTests {
  public:
    ExpressionTests() : machine_class_{new MachineClass("ExprTest")} {
        machine_class_->setOption("x", 7);
        scope_ = MachineInstanceFactory::create("test", machine_class_->name);
        scope_->setStateMachine(machine_class_);
        tests_.push_back(TestCase([this]() { return evalates_a_simple_equality(); }));
        tests_.push_back(TestCase([this]() { return evaluates_an_equality_with_an_expression(); }));
        tests_.push_back(TestCase([this]() { return evaluates_a_machine_timer(); }));
        tests_.push_back(TestCase([this]() { return evaluates_an_expression_timer_comparison(); }));
        tests_.push_back(TestCase([this]() { return evaluates_a_timer_expression_comparison(); }));
        tests_.push_back(
            TestCase([this]() { return evaluates_a_simple_equality_that_returns_false(); }));
        tests_.push_back(TestCase([this]() { return evaluates_a_local_property(); }));
        tests_.push_back(TestCase([this]() { return evaluates_comparison_of_properties(); }));
        tests_.push_back(TestCase([this]() { return condition_evaluates_expressions(); }));
        tests_.push_back(TestCase([this]() { return evaluates_false_eq_false(); }));
        tests_.push_back(TestCase([this]() { return assigns_a_value(); }));
        tests_.push_back(TestCase([this]() { return assigns_a_json_value(); }));
        tests_.push_back(TestCase([this]() { return assigns_a_json_subexpression(); }));
        tests_.push_back(TestCase([this]() { return puts_a_json_subexpression(); }));
    }
    ~ExpressionTests() { delete scope_; }
    std::list<TestCase> tests() { return tests_; }

  private:
    std::list<TestCase> tests_;
    MachineClass *machine_class_;
    MachineInstance *scope_;
    Evaluator eval;

    TestResult evalates_a_simple_equality() {
        Predicate pred(new Predicate(0), opEQ, new Predicate(0));
        Value res = eval.evaluate(&pred, scope_);
        EXPECT_BOOL(res);
        EXPECT_TRUE(res);
        PASS;
    }

    TestResult evaluates_an_equality_with_an_expression() {
        Predicate pred(new Predicate(3), opEQ,
                       new Predicate(new Predicate(1), opPlus, new Predicate(2)));
        Value res = eval.evaluate(&pred, scope_);
        EXPECT_BOOL(res);
        EXPECT_TRUE(res);
        PASS;
    }

    TestResult evaluates_a_machine_timer() {
        Predicate pred(new Predicate(0), opEQ, new Predicate("TIMER"));
        Value res = eval.evaluate(&pred, scope_);
        EXPECT_BOOL(res);
        EXPECT_TRUE(res);
        PASS;
    }

    TestResult evaluates_an_expression_timer_comparison() {
        Predicate pred(new Predicate(new Predicate(3), opMinus, new Predicate(3)), opEQ,
                       new Predicate("TIMER"));
        Value res = eval.evaluate(&pred, scope_);
        EXPECT_BOOL(res);
        EXPECT_TRUE(res);
        PASS;
    }

    TestResult evaluates_a_timer_expression_comparison() {
        Predicate pred(new Predicate(new Predicate(3), opMinus, new Predicate(3)), opEQ,
                       new Predicate("TIMER"));
        Value res = eval.evaluate(&pred, scope_);
        EXPECT_BOOL(res);
        EXPECT_TRUE(res);
        PASS;
    }

    TestResult evaluates_a_simple_equality_that_returns_false() {
        Predicate pred(new Predicate(1), opEQ, new Predicate(0));
        Value res = eval.evaluate(&pred, scope_);
        EXPECT_BOOL(res);
        EXPECT_FALSE(res);
        PASS;
    }

    TestResult evaluates_a_local_property() {
        Predicate pred(new Predicate("x"), opEQ, new Predicate(7));
        Value res = eval.evaluate(&pred, scope_);
        EXPECT_BOOL(res);
        EXPECT_TRUE(res);
        PASS;
    }

    TestResult evaluates_comparison_of_properties() {
        MachineInstance *one = MachineInstanceFactory::create("one", machine_class_->name);
        one->setStateMachine(machine_class_);
        one->addLocal("test", scope_);
        one->addDependancy(scope_);
        one->properties.add("x", 7);
        Predicate pred(new Predicate("test.x"), opEQ, new Predicate("x"));
        Value res = eval.evaluate(&pred, one);
        delete one;
        EXPECT_BOOL(res);
        EXPECT_TRUE(res);
        PASS;
    }

    TestResult condition_evaluates_expressions() {
        Condition cond(new Predicate(new Predicate("x"), opEQ, new Predicate(7)));
        Value res = cond(scope_);
        EXPECT_BOOL(res);
        EXPECT_TRUE(res);
        PASS;
    }

    TestResult evaluates_false_eq_false() {
        Condition cond(new Predicate(new Predicate(Value("FALSE")), opEQ, new Predicate(false)));
        Value res = cond(scope_);
        EXPECT_BOOL(res);
        EXPECT_TRUE(res);
        PASS;
    }

    TestResult assigns_a_value() {
        Predicate pred(new Predicate("x"), opAssign, new Predicate(1));
        eval.evaluate(&pred, scope_);
        Value x = scope_->properties.lookup("x");
        std::cout << "x: " << x << std::endl;
        EXPECT_TRUE(x.kind == Value::t_integer);
        EXPECT_TRUE(x == 1);
        PASS;
    }

    TestResult assigns_a_json_value() {
        cJSON *json = cJSON_Parse("{\"a\": 1}");
        Predicate pred(new Predicate("x"), opAssign, new Predicate(Value(json)));
        eval.evaluate(&pred, scope_);
        Value x = scope_->properties.lookup("x");
        std::cout << "x: " << x << std::endl;
        EXPECT_TRUE(x.kind == Value::t_json);
        PASS;
    }

    TestResult assigns_a_json_subexpression() {
        cJSON *json = cJSON_Parse(R"JSON({"a": 1, "b": {"c": 2}})JSON");
        auto source = new Predicate(Value(json));
        source->json_expression = "$.b.c";
        Predicate pred(new Predicate("x"), opGetSubExpr, source);
        Value res = eval.evaluate(&pred, scope_);
        std::cout << "res: " << res << std::endl;
        Value x = scope_->properties.lookup("x");
        std::cout << "x: " << x << std::endl;
        EXPECT_TRUE(x.kind == Value::t_integer);
        EXPECT_TRUE(x == 2);
        PASS;
    }

    TestResult puts_a_json_subexpression() {
        cJSON *json = cJSON_Parse(R"JSON({"a": 1, "b": {"c": 2}})JSON");
        scope_->setValue("x", Value(json));
        auto target = new Predicate("x");
        target->json_expression = "$.b.c";
        Predicate pred(target, opPutSubExpr, new Predicate(3));
        Value res = eval.evaluate(&pred, scope_);
        std::cout << "res: " << res << std::endl;
        Value x = scope_->properties.lookup("x");
        EXPECT_TRUE(x.kind == Value::t_json);
        auto x_str = cJSON_PrintUnformatted(x.json);
        auto expected = cJSON_Parse(R"JSON({"a":1,"b":{"c":3}})JSON");
        auto expected_str = cJSON_PrintUnformatted(expected);
        std::cout << "x: " << x_str << " expected: " << expected_str << std::endl;
        EXPECT_TRUE(strcmp(x_str, expected_str) == 0);
        free(x_str);
        free(expected_str);
        cJSON_Delete(expected);
        PASS;
    }
};

int main(int, char **) {
    zmq::context_t *context = new zmq::context_t;
    MessagingInterface::setContext(context);
    boost::condition_variable_any cond_var;
    boost::shared_mutex cond_var_mutex;
    SharedThreadSafeQueue<Package*> queue(cond_var, cond_var_mutex);
    Dispatcher::create(queue);
    Logger::instance();
    zmq::socket_t dispatch_sync(*MessagingInterface::getContext(), ZMQ_REQ);
    dispatch_sync.connect("inproc://dispatcher_sync");

    int result = 0;
    {
        TestRunner tests;
        ExpressionTests expression_tests;
        tests.add(expression_tests.tests());
        auto success_pct = tests.run_all() * 100;
        std::cout << std::fixed << std::setprecision(2) << success_pct << "% passed ("
                  << tests.count() << " cases)\n";
        result = success_pct == 100 ? 0 : 1;
    }

    MessagingInterface::abort();
    Dispatcher::instance()->stop();
    LogState::cleanup();
    Logger::cleanup();

    return result;
}
