#include "gtest/gtest.h"
#include <Expression.h>
#include <MachineInstance.h>
#include <MessageLog.h>
#include <memory>
#include <symboltable.h>
#include <ThreadSafeQueue.h>
#include <Message.h>

#include "library_globals.cpp"
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

TEST_F(EvaluatorTest, compare_integer_and_float) {
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    Evaluator eval;
    Predicate pred(new Predicate(3), opEQ, new Predicate(Value(3.0)));
    EXPECT_EQ(Value(true), eval.evaluate(&pred, scope))
        << "evaluates a simple equality with a simple expression";
    delete scope;
}

TEST_F(EvaluatorTest, compare_float_and_integer) {
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    Evaluator eval;
    Predicate pred(new Predicate(Value(3.0)), opEQ, new Predicate(3));
    EXPECT_EQ(Value(true), eval.evaluate(&pred, scope))
        << "evaluates a simple equality with a simple expression";
    delete scope;
}

TEST_F(EvaluatorTest, compare_float_and_float) {
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    Evaluator eval;
    Predicate pred(new Predicate(Value(3.0)), opEQ, new Predicate(Value(3.0)));
    EXPECT_EQ(Value(true), eval.evaluate(&pred, scope))
        << "evaluates a simple equality with a simple expression";
    delete scope;
}

TEST_F(EvaluatorTest, compare_float_and_float_2) {
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    Evaluator eval;
    Predicate pred(new Predicate(Value(3.0)), opEQ, new Predicate(Value(3.1)));
    EXPECT_EQ(Value(false), eval.evaluate(&pred, scope))
        << "evaluates a simple equality with a simple expression";
    delete scope;
}

TEST_F(EvaluatorTest, convert_integer_to_float) {
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    Evaluator eval;
    Predicate pred(0, opFloat, new Predicate(Value(3.0)));
    Value res = eval.evaluate(&pred, scope);
    EXPECT_EQ(res.kind, Value::t_float) << "converts an integer to a float";
    EXPECT_EQ(res, 3.0) << "converts an integer to a float";
    delete scope;
}

TEST_F(EvaluatorTest, convert_string_to_json) {
    MachineClass *flag_class = new MachineClass("FLAG");
    assert(MachineClass::machine_classes.size() > 0);
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    Evaluator eval;
    std::string json_str = "[1,2,3]";
    scope->setValue("json", json_str);
    Predicate pred(0, opJson, new Predicate("json"));
    Value res = eval.evaluate(&pred, scope);
    EXPECT_EQ(res.kind, Value::t_json) << "converts a string to json";
    auto res_str = cJSON_PrintUnformatted(res.json);
    EXPECT_EQ(json_str, res_str) << "converts a string to json";
    delete scope;
}
TEST_F(EvaluatorTest, assigns_value_if_key_is_found) {
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    Evaluator eval;
    std::string json_str = R"JSON({"a":[1,2,3],"b":"hello"})JSON";
    cJSON *json = cJSON_Parse(json_str.c_str());
    scope->setValue("json", json);
    scope->setValue("result", "");
    Predicate *dest = new Predicate("test.result");
    Predicate *source = new Predicate("json");
    source->json_expression = "$.a";
    Predicate pred(dest, opGetSubExpr, source);
    Value res = eval.evaluate(&pred, scope);
    auto result = scope->getValue("result");
    EXPECT_EQ(result.kind, Value::t_json) << "doesn't return json";
    auto result_array_str = cJSON_PrintUnformatted(result.json);
    EXPECT_EQ(std::string("[1,2,3]"), std::string(result_array_str));
    free(result_array_str);
    auto res_str = cJSON_PrintUnformatted(res.json);
    EXPECT_EQ(json_str, std::string(res_str)) << "changed the original json";
}
TEST_F(EvaluatorTest, assigns_value_if_key_is_found_but_null) {
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    Evaluator eval;
    std::string json_str = R"JSON({"a":null,"b":"hello"})JSON";
    cJSON *json = cJSON_Parse(json_str.c_str());
    scope->setValue("json", json);
    scope->setValue("result", "");
    Predicate *dest = new Predicate("test.result");
    Predicate *source = new Predicate("json");
    source->json_expression = "$.a";
    source->default_value = Value("default-string", Value::t_string);
    Predicate pred(dest, opGetSubExpr, source);
    Value res = eval.evaluate(&pred, scope);
    auto result = scope->getValue("result");
    EXPECT_EQ(result.kind, Value::t_string) << "doesn't return string";
    EXPECT_EQ(*source->default_value, result);
    auto res_str = cJSON_PrintUnformatted(res.json);
    EXPECT_EQ(json_str, std::string(res_str)) << "changed the original json";
}

TEST_F(EvaluatorTest, assigns_default_value_if_key_not_found) {
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    Evaluator eval;
    std::string json_str = R"JSON({"a":[1,2,3],"b":"hello"})JSON";
    cJSON *json = cJSON_Parse(json_str.c_str());
    scope->setValue("json", json);
    scope->setValue("result", "");
    Predicate *dest = new Predicate("test.result");
    Predicate *source = new Predicate("json");
    source->json_expression = "$.c"; // non-existent key
    source->default_value = 7;
    Predicate pred(dest, opGetSubExpr, source);
    Value res = eval.evaluate(&pred, scope);
    auto result = scope->getValue("result");
    EXPECT_EQ(result.kind, Value::t_integer) << "doesn't return json";
    EXPECT_EQ(7, result.iValue);
    EXPECT_EQ(res.kind, Value::t_json) << "changes the original json";
    auto res_str = cJSON_PrintUnformatted(res.json);
    EXPECT_EQ(json_str, std::string(res_str)) << "changed the original json";
}

TEST_F(EvaluatorTest, assigns_null_value_if_key_not_found_and_no_default) {
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    Evaluator eval;
    std::string json_str = R"JSON({"a":[1,2,3],"b":"hello"})JSON";
    cJSON *json = cJSON_Parse(json_str.c_str());
    scope->setValue("json", json);
    scope->setValue("result", "");
    Predicate *dest = new Predicate("test.result");
    Predicate *source = new Predicate("json");
    source->json_expression = "$.c"; // non-existent key
    Predicate pred(dest, opGetSubExpr, source);
    Value res = eval.evaluate(&pred, scope);
    auto result = scope->getValue("result");
    EXPECT_EQ(result.kind, Value::t_empty);
    EXPECT_EQ(res.kind, Value::t_json) << "changes the original json";
    auto res_str = cJSON_PrintUnformatted(res.json);
    EXPECT_EQ(json_str, std::string(res_str)) << "changed the original json";
}


#include <Dispatcher.h>
#include <Logger.h>
#include <MessagingInterface.h>
#include <zmq.hpp>

int main(int argc, char *argv[]) {
    zmq::context_t *context = new zmq::context_t;
    MessagingInterface::setContext(context);
    Logger::instance();
    boost::condition_variable_any cond_var;
    boost::shared_mutex cond_var_mutex;
    SharedThreadSafeQueue<Package*> queue(cond_var, cond_var_mutex);
    Dispatcher::create(queue);
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
