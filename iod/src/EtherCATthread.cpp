#include "EtherCATthread.h"
#include <iostream>
#include <inttypes.h>
#include <ethercat.h>

EtherCATthread *EtherCATthread::instance_ = nullptr;
class EtherCATthreadInternals {
public:
    std::string adapter;
};

EtherCATthread::EtherCATthread() {
    m_pimpl = new EtherCATthreadInternals;
}

EtherCATthread::~EtherCATthread() {
    delete m_pimpl;
}

EtherCATthread *EtherCATthread::instance() {
    if (instance_ == nullptr) { instance_ = new EtherCATthread; }
    return instance_;
}

void EtherCATthread::stop() {
    delete instance_;
    instance_ = nullptr;
}

void EtherCATthread::activate(const std::string & adapter) {
    m_pimpl->adapter = adapter;
#ifdef SOEM_ETHERCAT
    if (ec_init(m_pimpl->adapter.c_str())) {
        std::cerr << "SOEM EtherCAT initialised.";
        if ( ec_config_init(false) > 0 ) {
            std::cerr << ec_slavecount << " slaves found.\n";
        }
    }
    ec_close();
#endif
}

void EtherCATthread::operator()() {

}
