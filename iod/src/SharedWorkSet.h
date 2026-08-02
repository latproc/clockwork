#ifndef __SHAREDWORKSET_H
#define __SHAREDWORKSET_H

#include <boost/thread/recursive_mutex.hpp>
#include <set>
#include <vector>

class MachineInstance;
class SharedWorkSet {
  public:
    static SharedWorkSet *instance();
    void add(MachineInstance *m);
    void remove(MachineInstance *m);
    std::set<MachineInstance *>::iterator erase(std::set<MachineInstance *>::iterator &iter);
    bool empty();
    size_t size();
    std::vector<MachineInstance *> snapshot();
    std::set<MachineInstance *>::iterator begin();
    std::set<MachineInstance *>::iterator end();
    boost::recursive_mutex &getMutex() { return mutex; }

  private:
        boost::recursive_mutex mutex;
    SharedWorkSet() {}
    std::set<MachineInstance *> busy_machines; // machines that have work queued to them
};

#endif
