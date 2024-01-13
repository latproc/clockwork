#include <zmq.hpp>
#include "AutoStats.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "ProcessingThread.h"

void ProcessingThread::handle_scheduler(
#ifdef KEEPSTATS
    AutoStatStorage &scheduler_delay,
#endif
    zmq::socket_t &sched_sync,
    char *buf,
    Status &status,
    ProcessingStates &processing_state
    ) {
#ifdef KEEPSTATS
    if (!scheduler_delay.running()) {
        scheduler_delay.start();
    }
#endif
    if (status == e_waiting && processing_state == ProcessingStates::eIdle) {
        size_t len = safeRecv(sched_sync, buf, 10, false, len, 0);
        if (len) {
            status = e_handling_sched;
#ifdef KEEPSTATS
            scheduler_delay.stop();
            avg_scheduler_time.start();
#endif
        }
        else {
            char buf[100];
            snprintf(buf, 100, "WARNING: scheduler sync returned zero length message");
            MessageLog::instance()->add(buf);
        }
    }
    else if (status == e_waiting_sched) {
        size_t len = safeRecv(sched_sync, buf, 10, false, len, 0);
        if (len) {
            safeSend(sched_sync, "bye", 3);
            status = e_waiting;
#ifdef KEEPSTATS
            avg_scheduler_time.update();
#endif
        }
        else {
            char buf[100];
            snprintf(buf, 100, "WARNING: scheduler sync returned zero length message");
            MessageLog::instance()->add(buf);
        }
    }
}
