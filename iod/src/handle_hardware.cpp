#include "DebugExtra.h"
#include "Logger.h"
#include "AutoStats.h"
#include "IOComponent.h"
#include "ProcessingThread.h"
#include "IOInterface.h"

void ProcessingThread::handle_hardware(
#ifdef KEEPSTATS
    AutoStatStorage &avg_update_time,
#endif
    UpdateStates & update_state,
    zmq::socket_t & ecat_out
    ) {
        static bool defaults_sent = false;
#ifdef KEEPSTATS
        avg_update_time.start();
#endif
        if (update_state == UpdateStates::s_update_idle) {
            if (defaults_sent && debug_block_ethercat) {
                DBG_PROCESSING << "EtherCAT is blocked for debug";
                return;
            }
            IOUpdate upd;
            if (IOComponent::getHardwareState() == IOComponent::s_hardware_init) {
                DBG_INITIALISATION << "Sending defaults to EtherCAT\n";
                upd = IOComponent::getDefaults();
            }
            else {
                upd = IOComponent::getUpdates();
            }
            if (upd.data_size() > 0) {
                size_t size = upd.data_size();
                uint8_t stage = 1;
                while (true) {
                    try {
                        switch (stage) {
                        case 1: {
                            zmq::message_t iomsg(4);
                            memcpy(iomsg.data(), (void *)&size, 4);
                            ecat_out.send(iomsg, ZMQ_SNDMORE);
                            ++stage;
                        }
                        case 2: {
                            auto packet_type = defaults_sent
                                ? IOInterface::MessageType::PROCESS_DATA
                                : IOInterface::MessageType::DEFAULT_DATA;
                            zmq::message_t iomsg(1);
                            memcpy(iomsg.data(), (void *)&packet_type, 1);
                            ecat_out.send(iomsg, ZMQ_SNDMORE);
                            ++stage;
                        }
                        case 3: {
                            zmq::message_t iomsg(size);
                            memcpy(iomsg.data(), (void *)upd.data(), size);
                            ecat_out.send(iomsg, ZMQ_SNDMORE);
#if VERBOSE_DEBUG
                            DBG_ETHERCAT << "sending to EtherCAT: ";
                            DBG_ETHERCAT << buffer_to_string(upd.data());
                            DBG_ETHERCAT << "\n";
#endif
                            ++stage;
                        }
                        case 4: {
                            zmq::message_t iomsg(size);
                            memcpy(iomsg.data(), (void *)upd.mask(), size);
#if VERBOSE_DEBUG
                            DBG_ETHERCAT << "using mask: ";
                            DBG_ETHERCAT << buffer_to_string(std::cout, upd.mask());
                            DBG_ETHERCAT << "\n";
#endif
                            ecat_out.send(iomsg);
                            ++stage;
                        }
                        default:;
                        }
                        break;
                    }
                    catch (const zmq::error_t &err) {
                        if (zmq_errno() == EINTR) {
                            DBG_PROCESSING << "interrupted when sending update ("
                                           << (unsigned int)stage << ")\n";
                            continue;
                        }
                        else {
                            std::cerr << zmq_strerror(zmq_errno());
                        }
                        assert(false);
                    }
                }
                IOComponent::updatesSent(true);
            }
            else {
                std::cout << "update data is empty\n";
            }
            if (!defaults_sent) {
                defaults_sent = true;
                IOComponent::setHardwareState(IOComponent::s_operational);
            }
        }
}
