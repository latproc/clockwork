#pragma once

#include <string>

class EtherCATthreadInternals;
class EtherCATthread {
public:
    static EtherCATthread *instance();
    static void stop();
    void activate(const std::string &adapter);
    void operator()();
private:
    static EtherCATthread *instance_;
    EtherCATthreadInternals *m_pimpl;
    EtherCATthread();
    ~EtherCATthread();
};
