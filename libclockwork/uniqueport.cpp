#include <iostream>
#include <iterator>
#include <string>
#include <list>
#include <stdio.h>
#include <boost/program_options.hpp>
#include <zmq.hpp>
#include <sstream>
#include <string.h>
#include "Logger.h"
#include <inttypes.h>
#include <stdlib.h>
#include <fstream>
#include <signal.h>
#include "cJSON.h"
#include <Channel.h>
#include <MessagingInterface.h>
#include <value.h>

using namespace std;

namespace po = boost::program_options;

int main(int argc, const char * argv[])
{
	zmq::context_t context;
	MessagingInterface::setContext(&context);
	int p = Channel::uniquePort();
	assert(p);
	return 0;
}