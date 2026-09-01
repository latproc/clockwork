/*
    Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

    This file is part of Latproc

    Latproc is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    Latproc is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Latproc; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

// Deterministic test for timeout-spec.md "Completion Races": after suspension,
// a TRY body's completion succeeds only if its recorded completion time is
// strictly earlier than the deadline; completion at or after the deadline loses
// to the timeout.
//
// The monotonic clock is pinned via Clock::setTestTime() so the before / at /
// after boundary is exercised exactly, rather than depending on scheduler
// dequeue order.

#include "Action.h"
#include "Dispatcher.h"
#include "Logger.h"
#include "MachineClass.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "Scheduler.h"
#include "ThreadSafeQueue.h"
#include "TryAction.h"
#include "clock.h"
#include "value.h"
#include "library_globals.cpp"
#include <boost/thread/mutex.hpp>
#include <iostream>
#include <zmq.hpp>

namespace {

// A body action that blocks until *release becomes true, then completes.
struct ControlledBlockActionTemplate : public ActionTemplate {
    bool *release;
    explicit ControlledBlockActionTemplate(bool *r) : release(r) {}
    Action *factory(MachineInstance *mi) override;
};
struct ControlledBlockAction : public Action {
    bool *release;
    ControlledBlockAction(MachineInstance *mi, ControlledBlockActionTemplate &t)
        : Action(mi), release(t.release) {}
    Status run() override {
        owner->start(this);
        return Running;
    }
    Status checkComplete() override {
        if (*release) {
            status = Complete;
            owner->stop(this);
            return Complete;
        }
        return Running;
    }
};
Action *ControlledBlockActionTemplate::factory(MachineInstance *mi) {
    return new ControlledBlockAction(mi, *this);
}

// A handler action that records that the ON TIMEOUT block ran.
struct FlagSetActionTemplate : public ActionTemplate {
    bool *ran;
    explicit FlagSetActionTemplate(bool *r) : ran(r) {}
    Action *factory(MachineInstance *mi) override;
};
struct FlagSetAction : public Action {
    bool *ran;
    FlagSetAction(MachineInstance *mi, FlagSetActionTemplate &t) : Action(mi), ran(t.ran) {}
    Status run() override {
        owner->start(this);
        *ran = true;
        status = Complete;
        owner->stop(this);
        return Complete;
    }
    Status checkComplete() override { return status; }
};
Action *FlagSetActionTemplate::factory(MachineInstance *mi) { return new FlagSetAction(mi, *this); }

struct RaceResult {
    Action::Status scope_status;
    bool handler_ran;
};

// Build TRY { controlled block } WITH TIMEOUT timeout_ms ON TIMEOUT { flag },
// run it under a pinned clock, release the body `completion_offset_us` after the
// scope started, and report the outcome.
RaceResult run_race(MachineInstance *m, long timeout_ms, uint64_t completion_offset_us) {
    bool *release = new bool(false);
    bool *handler_ran = new bool(false);

    ControlledBlockActionTemplate *body_at = new ControlledBlockActionTemplate(release);
    MachineCommandTemplate *body_mc = new MachineCommandTemplate("body", "");
    body_mc->setActionTemplate(body_at);

    FlagSetActionTemplate *handler_at = new FlagSetActionTemplate(handler_ran);
    MachineCommandTemplate *handler_mc = new MachineCommandTemplate("handler", "");
    handler_mc->setActionTemplate(handler_at);

    TryActionTemplate *tat =
        new TryActionTemplate(Value(static_cast<int64_t>(timeout_ms)), body_mc, handler_mc);
    TryAction *ta = static_cast<TryAction *>(tat->factory(m));

    const uint64_t base_us = 1000000;
    Clock::setTestTime(base_us);
    Action::Status st = (*ta)(); // body blocks; deadline = base_us + timeout_ms*1000
    (void)st;

    Clock::setTestTime(base_us + completion_offset_us);
    *release = true;
    ta->complete(); // records completion_us, compares against deadline_us
    st = ta->getStatus();

    RaceResult r{st, *handler_ran};
    return r;
}

} // namespace

int main() {
    zmq::context_t *context = new zmq::context_t;
    MessagingInterface::setContext(context);
    Logger::instance();
    MessageLog::setMaxMemory(10000);
    boost::condition_variable_any cond;
    boost::shared_mutex mutex;
    SharedThreadSafeQueue<Package *> queue(cond, mutex);
    Dispatcher::create(queue);

    MachineClass *mc = new MachineClass("RaceTester");
    MachineInstance *m = MachineInstanceFactory::create("t", "RaceTester");
    m->setStateMachine(mc);
    machines[m->getName()] = m;

    bool ok = true;

    // Strictly before the deadline: the body wins, the handler must NOT run.
    {
        RaceResult r = run_race(m, 100, 99999); // 99,999µs < 100,000µs deadline
        if (r.handler_ran || r.scope_status != Action::Complete) {
            std::cerr << "before-deadline: handler_ran=" << r.handler_ran
                      << " status=" << actionStatusName(r.scope_status)
                      << " (expected false/Complete)\n";
            ok = false;
        }
    }

    // Exactly at the deadline (tie): the timeout wins, the handler runs.
    {
        RaceResult r = run_race(m, 100, 100000);
        if (!r.handler_ran) {
            std::cerr << "at-deadline: handler did not run (expected timeout)\n";
            ok = false;
        }
    }

    // After the deadline: the timeout wins, the handler runs.
    {
        RaceResult r = run_race(m, 100, 100001);
        if (!r.handler_ran) {
            std::cerr << "after-deadline: handler did not run (expected timeout)\n";
            ok = false;
        }
    }

    Clock::clearTestTime();

    if (!ok) {
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}
