#include "Win32Helper.h"
#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>

#ifdef _WIN32
#include <stdint.h>
void usleep(uint64_t) {
    boost::this_thread::sleep_for(boost::chrono::microseconds(200));
}
#endif
