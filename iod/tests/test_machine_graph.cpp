// Unit tests for the dependency graph export used by cw / iod / iod_sdo /
// iod-elc (-g FILE).
//
// The end-to-end checks live in iod/CMakeLists.txt (dependency_graph_export*),
// which exercise the CLI and the written file. These tests cover the library API
// directly, in particular the Options that the command line does not expose.

#include "Dispatcher.h"
#include "Logger.h"
#include "MachineGraph.h"
#include "MachineInstance.h"
#include "Message.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "Scheduler.h"
#include "ThreadSafeQueue.h"
#include "clockwork.h"
#include "gtest/gtest.h"
#include "library_globals.cpp"

#include <sstream>
#include <string>
#include <zmq.hpp>

#ifndef MACHINE_GRAPH_FIXTURE_DIR
#error MACHINE_GRAPH_FIXTURE_DIR must be set
#endif

namespace {

std::string graphWith(const MachineGraph::Options &opts) {
    std::ostringstream out;
    MachineGraph::writeDot(out, opts);
    return out.str();
}

std::string graph() { return graphWith(MachineGraph::Options()); }

bool has(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

class MachineGraphTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        zmq::context_t *ctx = new zmq::context_t;
        MessagingInterface::setContext(ctx);
        Logger::instance();
        MessageLog::setMaxMemory(20000);
        static boost::condition_variable_any cond;
        static boost::shared_mutex mutex;
        static SharedThreadSafeQueue<Package *> queue(cond, mutex);
        Dispatcher::create(queue);

        std::list<std::string> files;
        files.push_back(std::string(MACHINE_GRAPH_FIXTURE_DIR) + "/dependency_graph.cw");
        ASSERT_EQ(loadConfig(files), 0) << "fixture load failed (see stderr)";
    }
};

// Class OPTIONs are class-level constants; a property the class declares is
// reported against the instance only where the instance value differs.
TEST_F(MachineGraphTest, options_and_overrides) {
    const std::string g = graph();
    EXPECT_TRUE(has(g, "class:Gate::opt:TravelTime\" [shape=note, label=\"TravelTime = 150\""))
        << g;
    EXPECT_TRUE(has(g, "prop:gate_two::TravelTime\" [shape=note, label=\"TravelTime = 40\""))
        << g;
    EXPECT_FALSE(has(g, "prop:gate_one::TravelTime")) << g;
    // Auto-generated properties that no class declares are not overrides.
    EXPECT_FALSE(has(g, "::NAME\"")) << g;
}

// Relations are labelled with the relation they are, since parameter binding and
// depends are different edges and used to be written unlabelled.
TEST_F(MachineGraphTest, relations_are_labelled) {
    const std::string g = graph();
    EXPECT_TRUE(has(g, "\"gate_one\" -> \"ctrl\" [label=\"parameter\"]")) << g;
    EXPECT_TRUE(has(g, "\"gate_one\" -> \"ctrl\" [label=\"depends\"]")) << g;
    EXPECT_TRUE(has(g, "\"gate_one\" -> \"class:Gate\" [label=\"instance_of\"]")) << g;
}

// Rule edges carry evaluation order, the guard text and whether the target state
// has ENTER / LEAVE actions.
TEST_F(MachineGraphTest, rules_keep_order_guards_and_actions) {
    const std::string g = graph();
    EXPECT_TRUE(has(g, "\"class:Gate::rules\" -> \"class:Gate::state:open\" [rule=\"0\", "
                       "kind=\"when\", enter=\"true\", leave=\"true\""))
        << g;
    EXPECT_TRUE(has(g, "\"class:Gate::rules\" -> \"class:Gate::state:closed\" [rule=\"1\", "
                       "kind=\"when\", enter=\"false\", leave=\"false\""))
        << g;
    // TRANSITION statements continue the same ordering.
    EXPECT_TRUE(has(g, "kind=\"transition\", trigger=\"NOTRIGGER\", guard=\"( gate ==  stopped)\""))
        << g;
}

// A class local is instantiated once per owner: the two must stay distinct
// nodes, while still reporting the short name they were written with.
TEST_F(MachineGraphTest, same_named_locals_stay_distinct) {
    const std::string g = graph();
    EXPECT_TRUE(has(g, "\"gate_one.demand\" [label=\"demand\", name=\"demand\"")) << g;
    EXPECT_TRUE(has(g, "\"gate_two.demand\" [label=\"demand\", name=\"demand\"")) << g;
}

// INITIAL and DEFAULT are separate declarations: a state can be either, both or
// neither, and the closing ']' in these needles pins the absence of the other
// flag. A state named only by one of the two still gets a node.
TEST_F(MachineGraphTest, initial_and_default_are_distinct) {
    const std::string g = graph();
    EXPECT_TRUE(has(g, "class:Gate::state:stopped\" [shape=ellipse, label=\"stopped\", "
                       "initial=\"true\"]"))
        << g;
    EXPECT_TRUE(has(g, "class:Gate::state:parked\" [shape=ellipse, label=\"parked\", "
                       "default=\"true\"]"))
        << g;
    EXPECT_TRUE(has(g, "class:GateController::state:idle\" [shape=ellipse, label=\"idle\", "
                       "initial=\"true\"]"))
        << g;
    // The built-in classes set both flags on one state.
    EXPECT_TRUE(has(g, "class:FLAG::state:off\" [shape=ellipse, label=\"off\", initial=\"true\", "
                       "default=\"true\""))
        << g;
}

// Two runs over the same loaded configuration must agree exactly. depends is a
// pointer-keyed set, so without sorting its edges come out in address order.
TEST_F(MachineGraphTest, output_is_deterministic) { EXPECT_EQ(graph(), graph()); }

TEST_F(MachineGraphTest, rules_can_be_suppressed) {
    MachineGraph::Options opts;
    opts.rules = false;
    const std::string g = graphWith(opts);
    EXPECT_FALSE(has(g, "kind=\"when\"")) << g;
    EXPECT_FALSE(has(g, "kind=\"transition\"")) << g;
    // The rest of the graph is unaffected.
    EXPECT_TRUE(has(g, "\"gate_one\" -> \"ctrl\" [label=\"parameter\"]")) << g;
    EXPECT_TRUE(has(g, "class:Gate::opt:TravelTime")) << g;
}

TEST_F(MachineGraphTest, values_can_be_suppressed) {
    MachineGraph::Options opts;
    opts.values = false;
    const std::string g = graphWith(opts);
    EXPECT_FALSE(has(g, "shape=note")) << g;
    EXPECT_TRUE(has(g, "kind=\"when\"")) << g;
    EXPECT_TRUE(has(g, "\"gate_one\" -> \"ctrl\" [label=\"depends\"]")) << g;
}

TEST_F(MachineGraphTest, root_restricts_to_the_connected_component) {
    MachineGraph::Options opts;
    opts.root = "ctrl";
    const std::string g = graphWith(opts);
    EXPECT_TRUE(has(g, "\"ctrl\" -> \"class:GateController\" [label=\"instance_of\"]")) << g;
    EXPECT_TRUE(has(g, "\"gate_one\" -> \"ctrl\" [label=\"parameter\"]")) << g;
    EXPECT_TRUE(has(g, "\"gate_one.demand\" -> \"gate_one\" [label=\"owner\"]")) << g;
    // Neither the unreachable instance nor its local appears.
    EXPECT_FALSE(has(g, "gate_two")) << g;
}

} // namespace
