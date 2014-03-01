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
#include <zmq.hpp>
#include <sstream>
#include <string>
#include <string.h>
#include "Logger.h"
#include <inttypes.h>
#include <iomanip>
#include <sys/types.h>
#define USE_READLINE 1
#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif
#define _MAIN_
#include "value.h"
#include "MessagingInterface.h"
#include <list>
#include "cmdline.h"

void sendMessage(zmq::socket_t &socket, const char *message) {
    const char *msg = (message) ? message : "";
    size_t len = strlen(msg);
    zmq::message_t reply (len);
    memcpy ((void *) reply.data (), msg, len);
    socket.send (reply);
}

#ifdef USE_READLINE

void initialise_machine_names(char *data);

/* A static variable for holding the line. */
static char *line_read = (char *)NULL;
static char *last_read = (char *)NULL;

/* Read a string, and return a pointer to it.
   Returns NULL on EOF. */
char *
rl_gets (const char *prompt)
{
  /* If the buffer has already been allocated,
     return the memory to the free pool. */
  if (line_read)
    {
      free (line_read);
      line_read = 0;
    }

  /* Get a line from the user. */
  line_read = readline (prompt);

  /* If the line has any text in it,
     save it on the history. */
    if (line_read && strlen(line_read)) {
        if (last_read && strcmp(line_read, last_read) != 0) {
            free(last_read);
            last_read = 0;
        }
        if (!last_read) {
            add_history (line_read);
            last_read = strdup(line_read);
        }
    }

  return (line_read);
}

zmq::socket_t *psocket = 0;
std::list<Value> params;
char *send_command(std::list<Value> &params) {
	if (params.size() == 0) return 0;
	Value cmd_val = params.front();
	params.pop_front();
	std::string cmd = cmd_val.asString();
	char *msg = MessagingInterface::encodeCommand(cmd, &params);
	sendMessage(*psocket, msg);
	size_t size = strlen(msg);
    free(msg);
	zmq::message_t reply;
	if (psocket->recv(&reply)) {
		size = reply.size();
		char *data = (char *)malloc(size+1);
		memcpy(data, reply.data(), size);
		data[size] = 0;
        
        if (cmd == "LIST") initialise_machine_names(data);
        return data;
	}
    return 0;
}
void process_command(std::list<Value> &params) {
    char * data = send_command(params);
    if (data) {
        std::cout << data << "\n";
        free(data);
    }
}

#endif

extern void yyparse();
extern FILE *yyin;

bool cmdline_done = false;

void usage(const char *name) {
	std::cout << name << " [-h host] [-p port]\n";
}

std::list<char *>machine_names;
std::list<const char *>commands;
char *machine_name_generator (const char *text, int state);
char *command_generator (const char *text, int state);

/* Attempt to complete on the contents of TEXT.  START and END bound the
 region of rl_line_buffer that contains the word to complete.  TEXT is
 the word to complete.  We can use the entire contents of rl_line_buffer
 in case we want to do some simple parsing.  Return the array of matches,
 or NULL if there aren't any. */
char **my_rl_completion (const char *text, int start, int end)
{
    char **matches;
    
    matches = (char **)NULL;
    
    if (start == 0)
        matches = rl_completion_matches (text, command_generator);
    else
        matches = rl_completion_matches (text, machine_name_generator);
    return (matches);
}

/* Generator function for command completion.  STATE lets us know whether
 to start from scratch; without any state (i.e. STATE == 0), then we
 start at the top of the list. */
char *command_generator (const char *text, int state)
{
    static std::list<const char *>::iterator iter;
    static int len;
    char *name;
    
    /* If this is a new word to complete, initialize now.  This includes
     saving the length of TEXT for efficiency, and initializing the iterator
     */
    if (!state)
    {
        iter = commands.begin();
        len = strlen (text);
    }
    
    /* Return the next name which partially matches from the command list. */
    while (iter != commands.end())
    {
        const char *name = *iter++;
        if (strncmp (name, text, len) == 0)
            return (strdup(name));
    }
    rl_attempted_completion_over = 1;
    /* If no names matched, then return NULL. */
    return ((char *)NULL);
}

/* Generator function for machine name completion.  STATE lets us know whether
 to start from scratch; without any state (i.e. STATE == 0), then we
 start at the top of the list. */
char *machine_name_generator (const char *text, int state)
{
    static std::list<char *>::iterator iter;
    static int len;
    char *name;
    
    /* If this is a new word to complete, initialize now.  This includes
     saving the length of TEXT for efficiency, and initializing the iterator
     */
    if (!state)
    {
        iter = machine_names.begin();
        len = strlen (text);
    }
    
//    const char *search = text + strlen(text)-1;
//    while (search>text && (isalnum(*search) || strchr("_.-",*search))) search--;
//    if (search>text) ++search;
//    std::cout << text << "\n" << search << "\n";

    /* Return the next name which partially matches from the command list. */
    while (iter != machine_names.end())
    {
        char *name = *iter++;
        if (strncmp (name, text, len) == 0)
            return (strdup(name));
    }
    rl_attempted_completion_over = 1;
    /* If no names matched, then return NULL. */
    return ((char *)NULL);
}

void cleanup() {
    std::list<char *>::iterator iter = machine_names.begin();
    while (iter != machine_names.end()) { char *name = *iter; free(name); iter = machine_names.erase(iter); }
}

void initialise_machine_names(char *data) {
    std::list<Value> params;
    params.push_back("LIST");
    bool did_alloc = false;
    if (!data) data = send_command(params);
    if (data) {
        cleanup();
        char buf[500];
        char *p = data, *q = buf;
        while (*p) {
            if (*p != ' ' && *p != '\n') {
                if (q-buf<499) *q++ = *p++;
            }
            else {
                *q = 0; machine_names.push_back(strdup(buf));
                q = buf;
                while (*p && *p++ != '\n') ;
            }
        }
    }
    if (did_alloc) free(data);
}

void initialise_commands() {
    commands.push_back("DESCRIBE");
    commands.push_back("DISABLE");
    commands.push_back("ENABLE");
    commands.push_back("GET");
    commands.push_back("LIST");
    commands.push_back("MESSAGES");
    commands.push_back("MODBUS");
    commands.push_back("PROPERTY");
    commands.push_back("RESUME");
    commands.push_back("SEND");
    commands.push_back("SET");
    commands.push_back("TRACING");
    commands.push_back("TOGGLE");
}

int main(int argc, const char * argv[])
{
    try {
		int port = 5555;
		bool quiet = false;
		std::string host = "127.0.0.1";
		for (int i=1; i<argc; ++i) {
			if (i<argc-1 && strcmp(argv[i],"-p") == 0) {
				port = strtol(argv[++i], 0, 0);
			} 
			else if (i<argc-1 && strcmp(argv[i],"-h") == 0) {
				host = argv[++i];
			} 
			else if (strcmp(argv[i],"-?") == 0) {
				usage(argv[0]); exit(0);
			}
			else if (strcmp(argv[i],"-q") == 0)
				quiet = true;
		}
		std::stringstream ss;
		ss << "tcp://" << host << ":" << port;
		std::string url(ss.str());
		if (!quiet) {
			 std::cout << "Connecting to " << url << "\n"
			<< "\nEnter HELP; for help. Note that ';' is required at the end of each command\n"
			<< "  use exit; or ctrl-D to exit this program\n\n";
		}
		zmq::context_t context (1);
		zmq::socket_t socket (context, ZMQ_REQ);
		int linger = 0; // do not wait at socket close time
		socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
		psocket = &socket;

		socket.connect(url.c_str());
		std::string word;
		std::string msg;
		std::string line;
		std::stringstream line_input(line);
        
        // readline completion function
        rl_attempted_completion_function = my_rl_completion;
        initialise_machine_names(0);
        initialise_commands();

		do yyparse(); while (!cmdline_done);
   }
    catch(std::exception& e) {
        if (zmq_errno())
            std::cerr << zmq_strerror(zmq_errno()) << "\n";
        else
            std::cerr << e.what() << "\n";
        cleanup();
        return 1;
    }
    catch(...) {
        std::cerr << "Exception of unknown type!\n";
    }
    cleanup();
    return 0;
}

