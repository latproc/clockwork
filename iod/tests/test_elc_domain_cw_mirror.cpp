#include "ElcDomainCwMirror.h"
#include "gtest/gtest.h"

using ElcDomainCwMirror::Coalesce;
using ElcDomainCwMirror::Snapshot;
using ElcDomainCwMirror::Slot;
using ElcDomainCwMirror::anyIncomplete;
using ElcDomainCwMirror::anyNonPrimaryIncomplete;
using ElcDomainCwMirror::cwState;
using ElcDomainCwMirror::kIncompleteApplyMinUs;
using ElcDomainCwMirror::snapshotsEqual;

static Slot completeSlot(uint32_t id) {
    Slot s;
    s.id = id;
    s.status_known = true;
    s.active = true;
    s.valid = true;
    s.wc_state = 2;
    s.wc = 15;
    return s;
}

static Slot incompleteSlot(uint32_t id) {
    Slot s = completeSlot(id);
    s.valid = false;
    s.wc_state = 1;
    s.wc = 9;
    s.faults = 0x30;
    return s;
}

TEST(ElcDomainCwMirror, IncompleteDoesNotForceInvalid) {
    Slot s = incompleteSlot(2);
    EXPECT_STREQ("INCOMPLETE", cwState(s));
    s.status_known = false;
    EXPECT_EQ(nullptr, cwState(s));
}

TEST(ElcDomainCwMirror, LastWriteWinsAndSkipsIdentical) {
    Coalesce c;
    Snapshot a{};
    a.nslots = 2;
    a.slots[0] = completeSlot(1);
    a.slots[1] = completeSlot(2);
    c.store(a);
    Snapshot first;
    ASSERT_TRUE(c.takeIfDue(1000, &first));
    EXPECT_TRUE(snapshotsEqual(a, first));

    c.store(a);
    Snapshot again;
    EXPECT_FALSE(c.takeIfDue(2000, &again));
}

TEST(ElcDomainCwMirror, IncompleteEdgeAppliesImmediately) {
    Coalesce c;
    Snapshot a{};
    a.nslots = 2;
    a.primary_domain_id = 1;
    a.slots[0] = completeSlot(1);
    a.slots[1] = completeSlot(2);
    c.store(a);
    Snapshot out;
    ASSERT_TRUE(c.takeIfDue(0, &out));

    Snapshot b = a;
    b.slots[1] = incompleteSlot(2);
    c.store(b);
    ASSERT_TRUE(c.takeIfDue(1, &out));
    EXPECT_TRUE(anyNonPrimaryIncomplete(out));
    EXPECT_STREQ("COMPLETE", cwState(out.slots[0]));
    EXPECT_STREQ("INCOMPLETE", cwState(out.slots[1]));
}

TEST(ElcDomainCwMirror, IncompleteNonEdgeIsRateLimited) {
    Coalesce c;
    Snapshot a{};
    a.nslots = 2;
    a.slots[0] = completeSlot(1);
    a.slots[1] = incompleteSlot(2);
    c.store(a);
    Snapshot out;
    ASSERT_TRUE(c.takeIfDue(1000, &out));

    Snapshot b = a;
    b.slots[1].wc = 8;
    c.store(b);
    EXPECT_FALSE(c.takeIfDue(1000 + kIncompleteApplyMinUs - 1, &out));
    ASSERT_TRUE(c.takeIfDue(1000 + kIncompleteApplyMinUs, &out));
    EXPECT_EQ(8u, out.slots[1].wc);
}
