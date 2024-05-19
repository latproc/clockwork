#include <boost/context/fiber.hpp>
#include <deque>

namespace Process {

struct  Proc {
    static size_t next_id; // Unique identifier for an instance of a Proc
    size_t id = next_id++;
    boost::context::fiber f;
    bool ready = true;
    Proc(boost::context::fiber && f_) : f(std::move(f_)) {}
    bool operator==(const Proc &other) const { return id == other.id; }
};

class Scheduler;

class Scheduler {
public:
    using Task = boost::context::fiber;
    Scheduler();

    void run(std::deque<Proc> &procs, bool &done) {
        sch = std::move(sch).resume(); // start the scheduler
    }
    void add(Proc && p) {
        all_procs.push_back(std::move(p));
        runnable.push_back(&all_procs.back());
    }
    void start() { run(all_procs, done); }
    bool finished() { return all_procs.empty(); }
    void stop() { done = true; }
    void resume() { sch = std::move(sch).resume(); }
    void activate(Proc *p); // Make the Proc runnable and resume the scheduler.
    void wake(Proc *p);     // Make the Proc runnable but don't resume the scheduler.
    void join();            // Block the caller until all procs are finished.
    size_t num_active() { return runnable.size(); }
    size_t num_waiting() { return all_procs.size(); }

private:
    std::deque<Proc> all_procs; 
    std::deque<Proc*> runnable; // round robin scheduler
    bool done = false;

    boost::context::fiber sch;
};

class Signal {
public:
    Signal(Scheduler &scheduler_) : scheduler(scheduler_) {}
    void wait(Scheduler::Task && sink);
    void send();
    bool awaited();
    void broadcast();
private:
    std::deque<Proc*> procs; // Procs waiting for this signal
    Scheduler &scheduler;
};


}

