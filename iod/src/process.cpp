#include <process.h>

namespace Process {

size_t Proc::next_id = 0;
Proc *proc = nullptr;

class Scheduler;
namespace ctx = boost::context;

Scheduler::Scheduler() : 
    sch([&](ctx::fiber && main) {
        while (!done) {
            if (runnable.empty()) {
                main = std::move(main).resume(); // resume main
                continue;
            }
            // TODO: Use a circular buffer for the runnable queue.
            proc = runnable.front();
            runnable.pop_front();
            if (proc->ready && proc->f) {
                proc->f = std::move(proc->f).resume();
            }
            if (proc->f) {
                runnable.push_back(proc);
            }
            else {
                auto found = std::find(all_procs.begin(), all_procs.end(), *proc);
                if (found != all_procs.end()) {
                    all_procs.erase(found);
                }
            }
            main = std::move(main).resume(); // resume main
        }
        return std::move(main);
    })
{
}
void Scheduler::activate(Proc *p) {
    // Some procs may have been added or have become ready.
    for (auto & proc : all_procs) {
        if (*p == proc) {
            assert(proc.ready);
            runnable.push_front(&proc); // Signalled procs run immediately
            resume();
            return;
        }
    }
    assert("Attempt to activate an unknown proc" && false);
}

void Scheduler::join() {
    while (!finished()) {
        resume();
    }
}

void Signal::wait(Scheduler::Task && sink) {
    if (proc) {
        procs.push_back(proc);
        proc->ready = false;
        sink = std::move(sink).resume();
    }
}

void Signal::send() {
    if (!procs.empty()) {
        auto proc = procs.front();
        procs.pop_front();
        proc->ready = true;
        scheduler.activate(proc);
    }
}

bool Signal::awaited() {
    return !procs.empty();
}

} // namespace Process
