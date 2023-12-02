
void ProcessingThread::handle_command(zmq::pollitem_t items[],
        int dynamic_poll_start_idx,
        unsigned int num_channels,
        zmq::socket_t &command_sync
    ) {
    // check the command interface and any command channels for activity
    bool have_command = false;
    if (items[internals->CMD_SYNC_ITEM].revents & ZMQ_POLLIN) {
        have_command = true;
    }
    else {
        for (unsigned int i = dynamic_poll_start_idx;
             i < dynamic_poll_start_idx + num_channels; ++i) {
            if (items[i].revents & ZMQ_POLLIN) {
                have_command = true;
                break;
            }
        }
    }
    if (have_command) {
        uint64_t start_time = microsecs();
        uint64_t now = start_time;
#ifdef KEEPSTATS
        AutoStat stats(avg_cmd_processing);
#endif
        int count = 0;
        while (have_command && (long)(now - start_time) < internals->cycle_delay / 2) {
            have_command = false;
            std::list<CommandSocketInfo *>::iterator csi_iter =
                internals->channel_sockets.begin();
            unsigned int i = internals->CMD_SYNC_ITEM;
            while (i <= CommandSocketInfo::lastIndex() &&
                   (long)(now - start_time) < internals->cycle_delay / 2) {
                zmq::socket_t *sock = 0;
                CommandSocketInfo *info = 0;
                if (i == internals->CMD_SYNC_ITEM) {
                    sock = &command_sync;
                }
                else {
                    if (csi_iter == internals->channel_sockets.end()) {
                        break;
                    }
                    info = *csi_iter++;
                    sock = info->sock;
                }
                { int rc = zmq::poll(&items[i], 1, 0); }
                if (!(items[i].revents & ZMQ_POLLIN)) {
                    ++i;
                    continue;
                }
                have_command = true;

                zmq::message_t msg;
                char *buf = 0;
                size_t len = 0;
                MessageHeader mh;
                uint32_t default_id = mh.getId(); // save the msgid to following check
                if (safeRecv(*sock, &buf, &len, false, 0, mh)) {
                    ++count;
                    if (false && len > 10) {
                        FileLogger fl(program_name);
                        fl.f() << "Processing thread received command ";
                        if (buf) {
                            fl.f() << buf << " ";
                        }
                        else {
                            fl.f() << "NULL";
                        }
                        fl.f() << "\n";
                    }
                    if (!buf) {
                        continue;
                    }
                    IODCommand *command = parseCommandString(buf);
                    if (command) {
                        bool ok = false;
                        try {
                            ok = (*command)();
                        }
                        catch (const std::exception &e) {
                            FileLogger fl(program_name);
                            fl.f() << "command execution threw an exception " << e.what()
                                   << "\n";
                        }
                        delete[] buf;

                        if (mh.needsReply() || mh.getId() == default_id) {
                            char *response =
                                strdup((ok) ? command->result() : command->error());
                            MessageHeader rh(mh);
                            rh.source = mh.dest;
                            rh.dest = mh.source;
                            rh.start_time = microsecs();
                            safeSend(*sock, response, strlen(response), rh);
                            free(response);
                        }
                        else {
                            //char *response = strdup(command->result());
                            //safeSend(*sock, response, strlen(response));
                            //free(response);
                        }
                    }
                    else {
                        if (mh.needsReply() || mh.getId() == default_id) {
                            char *response = new char[len + 40];
                            snprintf(response, len + 40, "Unrecognised command: %s", buf);
                            MessageHeader rh(mh);
                            rh.source = mh.dest;
                            rh.dest = mh.source;
                            rh.start_time = microsecs();
                            safeSend(*sock, response, strlen(response), rh);
                            delete[] response;
                        }
                        else {
                            /*
                                char *response = new char[len+40];
                                snprintf(response, len+40, "Unrecognised command: %s", buf);
                                safeSend(*sock, response, strlen(response));
                                delete[] response;
                            */
                        }
                        delete[] buf;
                    }
                    delete command;
                }
                ++i;
            }
            usleep(0);
            now = microsecs();
        }
    }
}
