#include "gtest/gtest.h"
#include <Expression.h>
#include <IOComponent.h>
#include <MachineClass.h>
#include <MachineInstance.h>
#include <MessageLog.h>
#include <cstdlib>
#include <memory>
#include <string>
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

class EvaluatorTest : public ::testing::Test {
  protected:
    void SetUp() override {}
};

std::string messageLogContents() {
    char *messages = MessageLog::instance()->toString(MessageLog::instance()->count());
    std::string result(messages);
    free(messages);
    return result;
}

void expectSquareRootResultAndWarning(const Value &input, const Value &expected,
                                      const std::string &warning = "") {
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    MessageLog::instance()->purge();
    Evaluator eval;
    Predicate pred(0, opSquareRoot, new Predicate(input));
    EXPECT_EQ(expected, eval.evaluate(&pred, scope));
    std::string messages = messageLogContents();
    if (warning.empty()) {
        EXPECT_EQ(std::string::npos, messages.find("Warning: attempt to take sqrt of"));
    }
    else {
        EXPECT_NE(std::string::npos, messages.find(warning));
    }
    delete scope;
}

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

TEST_F(EvaluatorTest, convert_value_to_symbol) {
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    Evaluator eval;
    Predicate pred(0, opSymbol, new Predicate(Value(42)));
    Value res = eval.evaluate(&pred, scope);
    EXPECT_EQ(res.kind, Value::t_symbol);
    EXPECT_EQ(res.asString(), std::string("42"));
    delete scope;
}

TEST_F(EvaluatorTest, convert_string_property_to_symbol_looks_up_name) {
    MachineClass *flag_class = new MachineClass("FLAG");
    ASSERT_GT(MachineClass::machine_classes.size(), 0u);
    MachineInstance *scope = MachineInstanceFactory::create("test_sym_prop", "FLAG");
    scope->setStateMachine(flag_class);
    scope->setValue("label", Value("target", Value::t_string));
    scope->setValue("target", Value("armed", Value::t_string));
    Evaluator eval;
    // Evaluating property `label` yields string "target"; AS SYMBOL then looks up that name.
    Predicate pred(0, opSymbol, new Predicate("label"));
    Value res = eval.evaluate(&pred, scope);
    EXPECT_EQ(res.kind, Value::t_symbol);
    EXPECT_EQ(res.asString(), std::string("armed"));
    delete scope;
}

TEST_F(EvaluatorTest, convert_literal_string_to_symbol_via_lookup) {
    MachineClass *flag_class = new MachineClass("FLAG");
    ASSERT_GT(MachineClass::machine_classes.size(), 0u);
    MachineInstance *scope = MachineInstanceFactory::create("test_sym", "FLAG");
    scope->setStateMachine(flag_class);
    scope->setValue("target", Value("armed", Value::t_string));
    Evaluator eval;
    Predicate pred(0, opSymbol, new Predicate(Value("target", Value::t_string)));
    Value res = eval.evaluate(&pred, scope);
    EXPECT_EQ(res.kind, Value::t_symbol);
    EXPECT_EQ(res.asString(), std::string("armed"));
    delete scope;
}

TEST_F(EvaluatorTest, predicate_self_assignment_is_safe) {
    Predicate pred(new Predicate(1), opPlus, new Predicate(2));
    pred = pred;
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    Evaluator eval;
    EXPECT_EQ(eval.evaluate(&pred, scope), Value(3));
    delete scope;
}

TEST_F(EvaluatorTest, condition_self_assignment_is_safe) {
    Condition cond(new Predicate(new Predicate(2), opEQ, new Predicate(2)));
    cond = cond;
    MachineInstance *scope = MachineInstanceFactory::create("test", "FLAG");
    EXPECT_TRUE(cond(scope));
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

TEST_F(EvaluatorTest, square_root_integer_expression) {
    expectSquareRootResultAndWarning(9, Value(3.0));
}

TEST_F(EvaluatorTest, square_root_float_expression) {
    expectSquareRootResultAndWarning(Value(2.25), Value(1.5));
}

TEST_F(EvaluatorTest, square_root_numeric_string_expression) {
    expectSquareRootResultAndWarning(Value("16", Value::t_string), Value(4.0));
}

TEST_F(EvaluatorTest, square_root_negative_expression_logs_warning) {
    expectSquareRootResultAndWarning(-1, Value(0), "Warning: attempt to take sqrt of -1");
}

TEST_F(EvaluatorTest, square_root_negative_float_expression_logs_warning) {
    expectSquareRootResultAndWarning(Value(-2.25), Value(0),
                                     "Warning: attempt to take sqrt of -2.250000");
}

TEST_F(EvaluatorTest, square_root_negative_numeric_string_expression_logs_warning) {
    expectSquareRootResultAndWarning(Value("-4", Value::t_string), Value(0),
                                     "Warning: attempt to take sqrt of \"-4\"");
}

TEST_F(EvaluatorTest, square_root_string_conversion_failure_logs_warning) {
    expectSquareRootResultAndWarning(Value("not-a-number", Value::t_string), Value(0),
                                     "Warning: attempt to take sqrt of \"not-a-number\"");
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
    source->json_expression = "$.b";
    res = eval.evaluate(&pred, scope);
    result = scope->getValue("result");
    EXPECT_EQ(result.kind, Value::t_string) << "doesn't return a string";
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


#include "ExpressionPcode.h"

TEST_F(EvaluatorTest, pcode_matches_eval_pid_style_math) {
    MachineInstance *scope = MachineInstanceFactory::create("pcode_pid", "FLAG");
    scope->setValue("target", 100.0);
    scope->setValue("meas", 80.0);
    scope->setValue("dt", 0.01);
    scope->setValue("Kp", 2.0);
    Evaluator eval;
    Predicate pred(new Predicate(new Predicate("target"), opMinus, new Predicate("meas")), opTimes,
                   new Predicate("Kp"));
    Value interp = eval.evaluate(&pred, scope);
    ExpressionPcode *code = ExpressionPcode::tryCompile(&pred);
    ASSERT_TRUE(code != 0);
    Value fast = code->run(scope);
    EXPECT_EQ(interp, fast);
    delete code;
    delete scope;
}

TEST_F(EvaluatorTest, pcode_matches_eval_cast_and_compare) {
    MachineInstance *scope = MachineInstanceFactory::create("pcode_cast", "FLAG");
    scope->setValue("raw", 10);
    Evaluator eval;
    Predicate *asf = new Predicate(0, opFloat, new Predicate("raw"));
    Predicate pred(asf, opGT, new Predicate(Value(5.0)));
    Value interp = eval.evaluate(&pred, scope);
    ExpressionPcode *code = ExpressionPcode::tryCompile(&pred);
    ASSERT_TRUE(code != 0);
    EXPECT_EQ(interp, code->run(scope));
    delete code;
    delete scope;
}

TEST_F(EvaluatorTest, pcode_rejects_json_subexpr) {
    Predicate *dest = new Predicate("x");
    Predicate *src = new Predicate("json");
    src->json_expression = "$.a";
    Predicate pred(dest, opGetSubExpr, src);
    EXPECT_EQ(static_cast<ExpressionPcode *>(0), ExpressionPcode::tryCompile(&pred));
}

namespace {

MachineInstance *makeEnabledTimerMachine(const char *name) {
    auto *cls = new MachineClass(name);
    cls->addState("idle");
    MachineInstance *mi = MachineInstanceFactory::create(name, name);
    mi->setStateMachine(cls);
    mi->enable();
    mi->resetNeedsCheck();
    return mi;
}

TEST(TimerOverduePolicy, ArmFutureOnlyDoesNotRequeueOverdueTimerLt) {
    MachineInstance *scope = makeEnabledTimerMachine("timer_arm_future");
    scope->start_time = microsecs() - 5000 * 1000; // TIMER ~5000 ms
    scope->resetNeedsCheck();

    Predicate pred(new Predicate("TIMER"), opLT, new Predicate(1));
    PredicateTimerDetails *ptd =
        pred.scheduleTimerEvents(nullptr, scope, TimerOverduePolicy::ArmFutureOnly);
    EXPECT_EQ(static_cast<PredicateTimerDetails *>(nullptr), ptd);
    EXPECT_FALSE(scope->needsCheck());
    delete scope;
}

TEST(TimerOverduePolicy, RecoverOverdueRequeuesOverdueTimerLt) {
    MachineInstance *scope = makeEnabledTimerMachine("timer_recover");
    scope->start_time = microsecs() - 5000 * 1000;
    scope->resetNeedsCheck();

    Predicate pred(new Predicate("TIMER"), opLT, new Predicate(1));
    PredicateTimerDetails *ptd =
        pred.scheduleTimerEvents(nullptr, scope, TimerOverduePolicy::RecoverOverdue);
    EXPECT_EQ(static_cast<PredicateTimerDetails *>(nullptr), ptd);
    EXPECT_TRUE(scope->needsCheck());
    delete scope;
}

TEST(TimerOverduePolicy, FutureTimerStillArmsUnderArmFutureOnly) {
    MachineInstance *scope = makeEnabledTimerMachine("timer_future");
    scope->start_time = microsecs();
    scope->resetNeedsCheck();

    Predicate pred(new Predicate("TIMER"), opGE, new Predicate(10));
    PredicateTimerDetails *ptd =
        pred.scheduleTimerEvents(nullptr, scope, TimerOverduePolicy::ArmFutureOnly);
    ASSERT_NE(static_cast<PredicateTimerDetails *>(nullptr), ptd);
    EXPECT_GT(ptd->delay, 0);
    EXPECT_FALSE(scope->needsCheck());
    delete ptd;
    delete scope;
}

TEST(DigitalValueMask, UnmaskedBitDoesNotTriggerWork) {
    IOAddress addr(0, 0, 0, 0, 16);
    DigitalValue dv(addr);
    MachineInstance *owner = MachineInstanceFactory::create("dv_owner", "FLAG");
    owner->properties.add("MASK", Value(0x0001), SymbolTable::ST_REPLACE);
    dv.addOwner(owner);

    EXPECT_TRUE(dv.inputBitTriggersWork(0));
    EXPECT_FALSE(dv.inputBitTriggersWork(1));
    EXPECT_FALSE(dv.inputBitTriggersWork(12));
    delete owner;
}

TEST(DigitalValueMask, ZeroMaskMatchesFilterAndWakes) {
    IOAddress addr(0, 0, 0, 0, 16);
    DigitalValue dv(addr);
    MachineInstance *owner = MachineInstanceFactory::create("dv_owner_zero", "FLAG");
    owner->properties.add("MASK", Value(0), SymbolTable::ST_REPLACE);
    dv.addOwner(owner);

    EXPECT_TRUE(dv.inputBitTriggersWork(12));
    delete owner;
}

TEST(DigitalValueMask, NoMaskWakesOnAnyBit) {
    IOAddress addr(0, 0, 0, 0, 16);
    DigitalValue dv(addr);
    MachineInstance *owner = MachineInstanceFactory::create("dv_owner_nomask", "FLAG");
    dv.addOwner(owner);

    EXPECT_TRUE(dv.inputBitTriggersWork(12));
    delete owner;
}

} // namespace

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
    Logger::cleanup();
    LogState::cleanup();
    return result;
}
