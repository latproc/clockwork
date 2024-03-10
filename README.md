Copyright (C) 2012-2019 Martin Leadbeater, Michael O'Connor

See the LICENCES file for license information.

<img align="right" width="140" height="61" src="http://www.valeparksoftwaredevelopment.com.au/img/vpsd-logo.png">

Martin Leadbeater,  
Trading as Vale Park Software Development
[![paypal](https://www.paypalobjects.com/en_AU/i/btn/btn_donate_SM.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=BPB7XTK7UH6LA&source=url)

<br/>
  
# Latproc - Language and Tools for Process Control

## Introduction

This software provides a high level, finite statemachine-based language 
called Clockwork that can be used to describe process control systems. 
Currently, this software only supports one hardware interface; the Beckhoff EtherCat
system (http://www.beckhoff.de/) through the IgH EtherCAT Master for Linux
(http://www.etherlab.org/en/ethercat/index.php)

Below are some instructions to help you build the program please
see the documentation for details about the language itself.

### Dependencies

   Note: this software requires that the following software be installed:

	* libmodbus (http://libmodbus.org/)  - for communication with modbus/tcp terminals (you don't actually need one of these terminals to use Clockwork though.

	* zeromq (http://www.zeromq.org/) - for inter-program messaging
	
	* zmq-pp (https://github.com/zeromq/zmqpp.git) - C++ interface for zmq

	* boost (http://boost.org/) - various c++ bits and pieces

	* mosquitto (http://mosquitto.org) - MQTT broker and protocol implementation
	
	also we use flex, bison, the GNU compiler suite

## Getting Started

### Part A - Building Latproc tools for standalone experimentation

The instructions in this part build the Clockwork programming language interpreter
without the need for the IgH EtherCAT Master.

Note, the Makefile used here assumes libzmq is installed in /usr/local/

* pull the latproc project from git

  git clone git://github.com/latproc/latproc.git latproc

* change to the latproc directory and build the interpreter

  ```
  cd latproc/iod
  make debug
  cp build/iosh .
  cp build/cw .

 * the 'cw' program is the clockwork interpreter and 'iosh' is the commandline shell that lets you monitor, debug and control your running clockwork programs.

<br/>
<br/>

  
### Part B - setting up the user database for the web interface (optional)

Latproc comes with a basic web server and some php scripts that display the state of the system in web panels or display a 3D representation of the model (advanced usage). These pages require a login using a local database for the accounts. The user database can be created using scripts/create_webiodb.

   By default it will be created in www/app with the name 'webio.db' these
     settings can be changed in settings.php

  TODO: fix the above documentation and revisit the web implementation (old and clunky)
  
  TODO: Add a comment about our GUI toolkit, humid (https://github.com/latproc/humid as an alternative to the web interface)
  
  TODO: Mention that iosh can be used to get started)

<br/>
<br/>

### Part C - Setting up the IgH EtherCAT software to be used but the Latproc software

This is only needed when you are using Clockwork with your EtherCAT hardware. 

The following instructions are brutally terse at present and we apologise
for that. Please watch this space for more precise instructions.

* pull the latproc project from git

  git clone git://github.com/latproc/latproc.git latproc

  please do not cd into the latproc directory yet.

* extract the etherlab kit from the EtherLab website:
    see http://www.etherlab.org/en/ethercat/index.php

  tar/hg/... etc

  There is also an unofficial set of patches maintained by Gavin Lambert:
   https://sourceforge.net/u/uecasm/etherlab-patches/ci/default/tree/

  Note: The following instructions assume your extract was placed into 
        a directory called 'ethercat' next to the latproc directory.
  
* configure and build etherlabs ethercat component

  cd ethercat
  ./configure --enable-generic --enable-e1000e=no --enable-8139too=no
  make

  (your configure options may be different from ours)

* the latproc io daemon (iod) uses some parts from ethercat that are 
  not built into the ethercat library; the following script prepares
  a build area for these bits (ec_tool). The script needs to know
  the location of the ethercat extract, passed as the first parameter. 

  cd latproc
  scripts/prepare_ec_tool ethercat-path

* build the object files in the ec_tool build area

  cd ec_tool
  make
  
* build the latproc tools (in the iod directory)

  cd ../iod
  make
  
At this point, the following tools should have been created:

  iod  : a daemon that talks to EtherCAT to interact with io
  iosh : a shell to interact with iod
  zmq_monitor : a program to monitor zmq messages published by iod
  persistd : a basic persistence daemon to record state changes from iod

You may also like to install libmodbus and edit the makefile to build:

  modbusd : a modbus interface to iod

### Part D - Building a Docker image

* pull the latproc project from git

  git clone git://github.com/latproc/latproc.git latproc

* build the docker image:

  cd latproc
  docker build -t latproc .

* run cw in a container:

  cd tests
  docker run -it --rm --user $UID:$GID -v ${PWD}:/app -w /app  cw cw run_tests.cw arith.cw bitset.cw anyon.cw prop.lpc test_set_prop.cw
