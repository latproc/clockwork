#include <gtest/gtest.h>
#include "monitor.h"
#include "options.h"
#include <zmq.hpp>
#include "cw_interface.h"
#include "monitor.h"
#include <set>

TEST(sendChanges, doesNotSendWhenNoChanges) {
    std::set<ModbusMonitor*> changes;
    bool did_send = false;
    sendChanges(changes, nullptr, Options(), [&did_send](ModbusMonitor *mm, bool which) {
        did_send = true;
    });
    EXPECT_FALSE(did_send);
}

TEST(sendChanges, sendsWhenChanges) {

    Options options;
    options.verbose = true;
    uint8_t buffer[10];
    for (int i=0; i<10; i++) {
        buffer[i] = 0;
    }
    ModbusMonitor mm("test", 1, 10, 1, "bool", true);
    mm.add();
    EXPECT_EQ(*mm.value->getBitData(), 0);

    std::set<ModbusMonitor*> changes;
    changes.insert(&mm);
    bool did_send = false;
    bool value = false;
    sendChanges(changes, buffer, options, [&did_send, &value](ModbusMonitor *mm, bool which) {
        did_send = true;
        value = which;
    });
    EXPECT_TRUE(did_send);
    EXPECT_FALSE(value);
}
