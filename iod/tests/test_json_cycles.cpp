// Offline reproduction harness for activity-dependent live cJSON growth in the
// production JSON request/response property lifecycle.
//
// Production shape being modelled (generic names, no site tree):
//
//   response body  -> curl.Result        (Plugin::setJsonValue)
//   result         := curl.Result        (CW property copy)
//   ITEM ${k} OF result AS STRING        (apply() + Value(cJSON*))
//   ITEM ${k} OF PostData := v AS STRING (PutSubExpr -> PredicateAction::setValue)
//   end of cycle: result / PostData / curl.Result reset to fresh objects
//
// Every stage is repeated many times and the live cJSON node count is compared
// before and after. Any growth means an allocation survived a completed cycle.

#include "gtest/gtest.h"

#include <Expression.h>
#include <MachineInstance.h>
#include <MessageEncoding.h>
#include <dynamic_value.h>
#include <cJSON.h>
#include <json_expression.h>
#include <json_expr_parser.h>
#include <string>
#include <value.h>

#include "library_globals.cpp"

namespace {

// A representative catalog payload (array of objects with mixed scalar types).
std::string catalogBody(int records = 24) {
    std::string body = "[";
    for (int i = 0; i < records; ++i) {
        if (i) {
            body += ",";
        }
        body += "{\"bale_ref\":\"B2609051612" + std::to_string(10 + (i % 90)) +
                "\",\"station\":\"FeederChamber\",\"lot_size\":13,\"weight\":12.5,"
                "\"anonymous\":false,\"tags\":[\"a\",\"b\"],\"nested\":{\"folio\":\"F00001\"}}";
    }
    body += "]";
    return body;
}

std::string smallBody() {
    return R"({"bale_ref":"B260905161214","station":"FeederChamber","lot_size":13})";
}

class JsonCycleTest : public ::testing::Test {
  protected:
    void SetUp() override {
        machine_class_ = new MachineClass("JsonCycleTest");
        machine_class_->addState("Idle", true);
        machine_class_->initial_state = State("Idle");
        machine_class_->default_state = State("Idle");
        machine_class_->disableAutomaticStateChanges();
        api_ = MachineInstanceFactory::create("api", machine_class_->name);
        api_->setStateMachine(machine_class_);
        curl_ = MachineInstanceFactory::create("curl", machine_class_->name);
        curl_->setStateMachine(machine_class_);
    }

    void TearDown() override {
        delete api_;
        delete curl_;
        // machine_class_ is owned by the factory/registry in this harness
    }

    // Mirrors Plugin::setJsonValue(scope, "Result", body).
    static void setResponse(MachineInstance *scope, const std::string &body) {
        if (body.empty()) {
            scope->setValue("Result", Value{cJSON_CreateObject()});
            return;
        }
        cJSON *json = cJSON_Parse(body.c_str());
        if (json) {
            scope->setValue("Result", Value{json});
        }
        else {
            cJSON *obj = cJSON_CreateObject();
            cJSON_AddItemToObject(obj, "Result", cJSON_CreateString(body.c_str()));
            scope->setValue("Result", Value{obj});
        }
    }

    MachineClass *machine_class_;
    MachineInstance *api_;
    MachineInstance *curl_;
};

// 1. Response parse + property copy + reset. No CW expression involvement.
TEST_F(JsonCycleTest, ResponsePropertyLifecycleIsNodeStable) {
    const std::string body = catalogBody();

    // Warm up so one-time caches are not counted as growth.
    for (int i = 0; i < 5; ++i) {
        setResponse(curl_, body);
        api_->setValue("result", curl_->getValue("Result"));
        api_->setValue("result", Value{cJSON_CreateObject()});
        setResponse(curl_, "");
    }

    const long before = cJSON_LiveNodeCount();
    for (int i = 0; i < 500; ++i) {
        setResponse(curl_, body);
        api_->setValue("result", curl_->getValue("Result"));
        api_->setValue("result", Value{cJSON_CreateObject()});
        setResponse(curl_, "");
    }
    const long after = cJSON_LiveNodeCount();
    EXPECT_EQ(before, after) << "live cJSON nodes grew by " << (after - before)
                             << " over 500 response/copy/reset cycles";
}

// 2. Field read through apply()/Value on a live JSON property.
TEST_F(JsonCycleTest, FieldReadCycleIsNodeStable) {
    setResponse(curl_, smallBody());

    auto readField = [this](const char *path) -> Value {
        const Value &src = curl_->getValue("Result");
        EXPECT_EQ(Value::t_json, src.kind);
        Value resolved(apply(path, src.json, curl_));
        return resolved;
    };

    readField("$.bale_ref");
    const long before = cJSON_LiveNodeCount();
    for (int i = 0; i < 500; ++i) {
        Value v = readField("$.bale_ref");
        EXPECT_EQ(Value::t_string, v.kind);
    }
    const long after = cJSON_LiveNodeCount();
    EXPECT_EQ(before, after) << "live cJSON nodes grew by " << (after - before)
                             << " over 500 ITEM reads";
}

// 3. Write into a live JSON property via PutSubExpr, as production does when it
//    builds POST data one field at a time.
TEST_F(JsonCycleTest, PutSubExprWriteCycleIsNodeStable) {
    api_->setValue("PostData", Value{cJSON_CreateObject()});

    Predicate *target = new Predicate("PostData");
    target->json_expression = "$.station";
    Predicate *rhs = new Predicate(Value(std::string("FeederChamber"), Value::t_string));
    Predicate pred(target, opPutSubExpr, rhs); // pred owns target and rhs

    Evaluator eval;
    for (int i = 0; i < 5; ++i) {
        eval.evaluate(&pred, api_);
    }
    const long before = cJSON_LiveNodeCount();
    for (int i = 0; i < 500; ++i) {
        eval.evaluate(&pred, api_);
    }
    const long after = cJSON_LiveNodeCount();
    EXPECT_EQ(before, after) << "live cJSON nodes grew by " << (after - before)
                             << " over 500 PutSubExpr writes";
}

// 4. Full production cycle: response -> result copy -> field read -> POST field
//    writes -> resets. This is the combination the plant runs per API call.
TEST_F(JsonCycleTest, FullRequestResponseCycleIsNodeStable) {
    Predicate *target = new Predicate("PostData");
    target->json_expression = "$.station";
    Predicate *rhs = new Predicate(Value(std::string("FeederChamber"), Value::t_string));
    Predicate pred(target, opPutSubExpr, rhs); // pred owns target and rhs
    Evaluator eval;

    auto cycle = [&]() {
        // request side: build PostData from scratch
        api_->setValue("PostData", Value{cJSON_CreateObject()});
        eval.evaluate(&pred, api_);

        // response side
        setResponse(curl_, catalogBody(8));
        api_->setValue("result", curl_->getValue("Result"));

        const Value &res = api_->getValue("result");
        ASSERT_EQ(Value::t_json, res.kind);
        {
            Value v(apply("$[0].bale_ref", res.json, api_));
            ASSERT_EQ(Value::t_string, v.kind);
        }
        {
            Value v(apply("$[0].station", res.json, api_));
            ASSERT_EQ(Value::t_string, v.kind);
        }

        // end of cycle resets
        api_->setValue("result", Value{cJSON_CreateObject()});
        setResponse(curl_, "");
    };

    for (int i = 0; i < 5; ++i) {
        cycle();
    }

    const long before = cJSON_LiveNodeCount();
    for (int i = 0; i < 200; ++i) {
        cycle();
    }
    const long after = cJSON_LiveNodeCount();
    EXPECT_EQ(before, after) << "live cJSON nodes grew by " << (after - before)
                             << " over 200 full request/response cycles";
}

// 5. Property replacement with JSON_VALUE {} style resets must free the old tree.
TEST_F(JsonCycleTest, JsonPropertyResetReleasesOldTree) {
    for (int i = 0; i < 5; ++i) {
        api_->setValue("scratch", Value{cJSON_Parse(catalogBody(4).c_str())});
        api_->setValue("scratch", Value{cJSON_CreateObject()});
    }
    const long baseline = cJSON_LiveNodeCount();
    for (int i = 0; i < 300; ++i) {
        api_->setValue("scratch", Value{cJSON_Parse(catalogBody(4).c_str())});
        api_->setValue("scratch", Value{cJSON_CreateObject()});
    }
    EXPECT_EQ(baseline, cJSON_LiveNodeCount());
}

// 6. Channel property-change encoding: every production property change is
//    encoded to JSON for each subscribed channel client.
TEST_F(JsonCycleTest, ChannelPropertyChangeEncodingIsNodeStable) {
    api_->setValue("result", Value{cJSON_Parse(catalogBody(8).c_str())});

    const Value &jsonVal = api_->getValue("result");
    ASSERT_EQ(Value::t_json, jsonVal.kind);

    for (int i = 0; i < 20; ++i) {
        std::string s = MessageEncoding::encodeCommand("PROPERTY", Value{"chan"}, Value{"result"},
                                                       jsonVal, Value{(int64_t)7});
        ASSERT_FALSE(s.empty());
        std::string t = MessageEncoding::encodeCommand("PROPERTY", Value{"chan"}, Value{"Status"},
                                                       Value{(int64_t)200});
        ASSERT_FALSE(t.empty());
    }

    const long before = cJSON_LiveNodeCount();
    for (int i = 0; i < 500; ++i) {
        std::string s = MessageEncoding::encodeCommand("PROPERTY", Value{"chan"}, Value{"result"},
                                                       jsonVal, Value{(int64_t)7});
        std::string t = MessageEncoding::encodeCommand("PROPERTY", Value{"chan"}, Value{"Status"},
                                                       Value{(int64_t)200});
    }
    const long after = cJSON_LiveNodeCount();
    EXPECT_EQ(before, after) << "live cJSON nodes grew by " << (after - before)
                             << " over 500 channel property encodes";
}

// 7. Channel command decode: a received PROPERTY message is parsed back into a
//    Value and applied to the target machine property.
TEST_F(JsonCycleTest, ChannelCommandDecodeCycleIsNodeStable) {
    const std::string msg =
        MessageEncoding::encodeCommand("PROPERTY", Value{"chan"}, Value{"result"},
                                       Value{cJSON_Parse(catalogBody(8).c_str())},
                                       Value{(int64_t)7});

    auto applyMessage = [&]() {
        std::string cmd;
        std::list<Value> *params = nullptr;
        EXPECT_TRUE(MessageEncoding::getCommand(msg.c_str(), cmd, &params));
        if (params) {
            auto it = params->begin();
            if (it != params->end()) {
                api_->setValue("result", *it);
            }
            delete params;
        }
    };

    for (int i = 0; i < 20; ++i) {
        applyMessage();
    }

    const long before = cJSON_LiveNodeCount();
    for (int i = 0; i < 500; ++i) {
        applyMessage();
    }
    const long after = cJSON_LiveNodeCount();
    EXPECT_EQ(before, after) << "live cJSON nodes grew by " << (after - before)
                             << " over 500 channel command decodes";
}

// 8. `result := curl.Result` as the expression engine evaluates it (opAssign
//    with a cross-machine property on the right). This is the ENTER handler the
//    production API machines run on every completed request.
TEST_F(JsonCycleTest, EvaluatedJsonAssignmentIsNodeStable) {
    api_->setValue("Source", Value{cJSON_Parse(catalogBody(8).c_str())});

    Predicate *dest = new Predicate("result");
    Predicate *src = new Predicate("Source");
    Predicate assignPred(dest, opAssign, src); // owns dest and src
    Evaluator eval;

    for (int i = 0; i < 20; ++i) {
        eval.evaluate(&assignPred, api_);
    }
    ASSERT_EQ(Value::t_json, api_->getValue("result").kind);

    const long before = cJSON_LiveNodeCount();
    for (int i = 0; i < 500; ++i) {
        eval.evaluate(&assignPred, api_);
    }
    const long after = cJSON_LiveNodeCount();
    EXPECT_EQ(before, after) << "live cJSON nodes grew by " << (after - before)
                             << " over 500 evaluated JSON assignments";
}

// 9. `ITEM ${field} OF result AS STRING` as the expression engine evaluates it
//    (opGetSubExpr writing the extracted field onto a destination property).
TEST_F(JsonCycleTest, EvaluatedJsonFieldReadIsNodeStable) {
    api_->setValue("Source", Value{cJSON_Parse(catalogBody(8).c_str())});

    Predicate *dest = new Predicate("scratch");
    Predicate *src = new Predicate("Source");
    src->json_expression = "$[0].bale_ref";
    Predicate getPred(dest, opGetSubExpr, src); // owns dest and src
    Evaluator eval;

    for (int i = 0; i < 20; ++i) {
        eval.evaluate(&getPred, api_);
    }

    const long before = cJSON_LiveNodeCount();
    for (int i = 0; i < 500; ++i) {
        eval.evaluate(&getPred, api_);
    }
    const long after = cJSON_LiveNodeCount();
    EXPECT_EQ(before, after) << "live cJSON nodes grew by " << (after - before)
                             << " over 500 evaluated ITEM reads";
}

// 10. Machine lifecycle: production creates and destroys machine instances
//     (shadow/API machines). A per-instance resource that holds JSON and is not
//     released in ~MachineInstance leaks once per machine, which is exactly the
//     shape of the previously fixed MachineCommand-handler leak.
TEST_F(JsonCycleTest, MachineLifecycleWithJsonPropertiesIsNodeStable) {
    MachineClass *cls = new MachineClass("JsonLifecycle");
    cls->addState("Idle", true);
    cls->initial_state = State("Idle");
    cls->default_state = State("Idle");
    cls->disableAutomaticStateChanges();
    cls->setProperty("Status", Value{(int64_t)0});
    cls->setProperty("result", Value{cJSON_CreateObject()});

    auto make_and_destroy = [&]() {
        MachineInstance *m = MachineInstanceFactory::create("life", cls->name);
        m->setStateMachine(cls);
        m->setProperties(cls->getProperties());
        m->setValue("result", Value{cJSON_Parse(catalogBody(4).c_str())});
        m->setValue("Status", Value{(int64_t)200});
        delete m;
    };

    for (int i = 0; i < 10; ++i) {
        make_and_destroy();
    }
    const long before = cJSON_LiveNodeCount();
    for (int i = 0; i < 200; ++i) {
        make_and_destroy();
    }
    const long after = cJSON_LiveNodeCount();
    EXPECT_EQ(before, after) << "live cJSON nodes grew by " << (after - before)
                             << " over 200 machine create/destroy cycles";
}

// 11. Class-property projection: every instance copies class OPTION defaults
//     (including JSON defaults such as `OPTION result JSON_VALUE {}`).
TEST_F(JsonCycleTest, ClassPropertyProjectionIsNodeStable) {
    MachineClass *cls = new MachineClass("JsonProjection");
    cls->setProperty("result", Value{cJSON_Parse(catalogBody(4).c_str())});

    api_->setStateMachine(cls);
    for (int i = 0; i < 10; ++i) {
        api_->setProperties(cls->getProperties());
    }
    const long before = cJSON_LiveNodeCount();
    for (int i = 0; i < 300; ++i) {
        api_->setProperties(cls->getProperties());
    }
    const long after = cJSON_LiveNodeCount();
    EXPECT_EQ(before, after) << "live cJSON nodes grew by " << (after - before)
                             << " over 300 class-property projections";
}

// 12. ExpressionValue is the per-processing-loop consumer of JSON paths:
//     conditions of the form `WHEN ITEM ${k} OF prop ...` compile to a dynamic
//     value re-evaluated on every loop pass. A leak here grows with loop count,
//     not with requests.
TEST_F(JsonCycleTest, EvaluatedJsonDynamicValueIsNodeStable) {
    api_->setValue("Source", Value{cJSON_Parse(catalogBody(8).c_str())});

    Predicate *target = new Predicate("Source");
    target->json_expression = "$[0].bale_ref";
    ExpressionValue ev(target); // copies the predicate
    ev.setScope(api_);

    for (int i = 0; i < 20; ++i) {
        ev();
    }
    const long before = cJSON_LiveNodeCount();
    for (int i = 0; i < 500; ++i) {
        ev();
    }
    const long after = cJSON_LiveNodeCount();
    EXPECT_EQ(before, after) << "live cJSON nodes grew by " << (after - before)
                             << " over 500 dynamic JSON-path evaluations";
    delete target;
}

// 13. Same, but with a symbol-substituted index (`ITEM ${idx} OF ...`), which is
//     how the generic JSON walkers address array elements, and with a path that
//     fails to resolve (the error/default branch).
TEST_F(JsonCycleTest, EvaluatedJsonSymbolIndexIsNodeStable) {
    api_->setValue("Source", Value{cJSON_Parse(catalogBody(8).c_str())});
    api_->setValue("idx", Value{(int64_t)1});

    Predicate *target = new Predicate("Source");
    target->json_expression = "$[${idx}].bale_ref";
    ExpressionValue ev(target);
    ev.setScope(api_);

    Predicate *missing = new Predicate("Source");
    missing->json_expression = "$.no_such_field";
    ExpressionValue ev_missing(missing);
    ev_missing.setScope(api_);

    for (int i = 0; i < 20; ++i) {
        ev();
        ev_missing();
    }
    const long before = cJSON_LiveNodeCount();
    for (int i = 0; i < 500; ++i) {
        ev();
        ev_missing();
    }
    const long after = cJSON_LiveNodeCount();
    EXPECT_EQ(before, after) << "live cJSON nodes grew by " << (after - before)
                             << " over 500 symbol-indexed / failing path evaluations";
    delete target;
    delete missing;
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
    SharedThreadSafeQueue<Package *> queue(cond_var, cond_var_mutex);
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
