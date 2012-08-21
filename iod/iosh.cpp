/*
  Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

  This file is part of Latproc

  Latproc is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
  
  Latproc is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Latproc; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <iostream>
#include <iterator>
#include <stdio.h>
#include <boost/program_options.hpp>
#include <zmq.hpp>
#include <sstream>
#include <string.h>
#include "Logger.h"
#include <inttypes.h>
#include <iomanip>
#include <sys/types.h>

namespace po = boost::program_options;

void sendMessage(zmq::socket_t &socket, const char *message) {
    const char *msg = (message) ? message : "";
    size_t len = strlen(msg);
    zmq::message_t reply (len);
    memcpy ((void *) reply.data (), msg, len);
    socket.send (reply);
}

int main(int argc, const char * argv[])
{
    try {
        
#if 0
        po::options_description desc("Allowed options");
        desc.add_options()
        ("help", "produce help message")
        ("server", "run as server")
        ;
        po::variables_map vm;        
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);    
        if (vm.count("help")) {
            std::cout << desc << "\n";
            return 1;
        }
        if (vm.count("server")) {
#endif
		int port = 5555;
		bool quiet = false;
		for (int i=1; i<argc; ++i) {
			if (i<argc-1 && strcmp(argv[i],"-p") == 0) {
				port = strtol(argv[++i], 0, 0);
			} 
			else if (strcmp(argv[i],"-q") == 0)
				quiet = true;
		}
		std::stringstream ss;
		ss << "tcp://127.0.0.1:" << port;
		if (!quiet) {
			 std::cout << "Connecting to " << ss.str() << "\n"
			<< "\nEnter HELP; for help. Note that ';' is required at the end of each command\n"
			<< "  use exit; or ctrl-D to exit this program\n\n";
		}
        zmq::context_t context (1);
        zmq::socket_t socket (context, ZMQ_REQ);
        socket.connect(ss.str().c_str());
		std::string word;
		std::string msg;
        for (;;) {
			if (!quiet) std::cout << "> " << std::flush;
			if (!(std::cin >> word)) break;
			size_t stmt_end;
			if ( (stmt_end = word.rfind(';')) != std::string::npos) {
				if (stmt_end > word.length()) continue;
				word.erase(stmt_end); // ignore everything after the ';'
				if (word.length()) {
					if (msg.length()) msg += " ";
					msg += word;
				}
				if (msg == "exit" || msg == "EXIT") break;
				sendMessage(socket, msg.c_str());
				size_t size = msg.length();
	            zmq::message_t reply;
				socket.recv(&reply);
				size = reply.size();
	            char *data = (char *)malloc(size+1);
	            memcpy(data, reply.data(), size);
	            data[size] = 0;
				std::cout << data << "\n";
				free(data);
				msg = "";
			}
			else
				msg += " " + word;
        }
   }
    catch(std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    catch(...) {
        std::cerr << "Exception of unknown type!\n";
    }

    return 0;
}

