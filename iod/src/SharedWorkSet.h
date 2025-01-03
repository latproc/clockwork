#pragma once
#include <boost/thread/recursive_mutex.hpp>
#include <set>

class MachineInstance;
class SharedWorkSet {
  public:
    static SharedWorkSet *instance();
    void add(MachineInstance *m);
    void remove(MachineInstance *m);
    bool empty() const;
    size_t size() const;
    std::set<MachineInstance *>::iterator erase(std::set<MachineInstance *>::iterator &iter);
    std::set<MachineInstance *>::iterator begin();
    std::set<MachineInstance *>::iterator end();
    boost::recursive_mutex &getMutex() { return mutex; }

  private:
    static SharedWorkSet *instance_;
    mutable boost::recursive_mutex mutex;
    std::set<MachineInstance *> busy_machines; // machines that have work queued to them
};
