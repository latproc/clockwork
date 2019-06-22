#ifndef __WIN32_HELPER_H__
#define __WIN32_HELPER_H__

#ifdef _WIN32
#include <stdint.h>
void usleep(uint64_t);
#endif

#ifdef __MINGW32__
#if _GLIBCXX_HAS_GTHREADS
#include <mutex>
#define RECURSIVE_LOCK std::lock_guard<std::recursive_mutex>
#else
#define MINGW_USE_BOOST_MUTEX
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#define RECURSIVE_LOCK boost::lock_guard<boost::recursive_mutex>
#endif
#else
#include <mutex>
#define RECURSIVE_LOCK std::lock_guard<std::recursive_mutex>
#endif

#endif
