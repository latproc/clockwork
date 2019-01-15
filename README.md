Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

See the LICENCES file for license information.

Martin Leadbeater,  martin.leadbeater@gmail.com


   Latproc - Language and Tools for Process Control

Introduction

This software provides a high level, finite statemachine-based language 
called Clockwork that can be used to describe process control systems. 
Currently, this software only supports one hardware interface; the Beckhoff EtherCat
system (http://www.beckhoff.de/) through the IgH EtherCAT Master for Linux
(http://www.etherlab.org/en/ethercat/index.php)

Below are some instructions to help you build the program please
see the documentation for details about the language itself.

Instructions

   Note: this software requires that the following software be installed:

	* libmodbus (http://libmodbus.org/)  - for communication with modbus/tcp terminals

	* zeromq (http://www.zeromq.org/) - for inter-program messaging

	* boost (http://boost.org/) - various c++ bits and pieces

	* mosquitto (http://mosquitto.org) - MQTT broker and protocol implementation


   Part A - Building Latproc tools for standalone experimentation

The instructions in this part build the Clockwork programming language interpreter
without the need for the IgH EtherCAT Master.

Note, the Makefile used here assumes libzmq is installed in /usr/local/


* pull the latproc project from git

  git clone git://github.com/latproc/latproc.git latproc

* change to the latproc directory and build the interpreter

  cd latproc/iod
  make -f Makefile.cw





  
   Part B - setting up the user database for the web interface

   The user database can be created using scripts/create_webiodb.

   By default it will be created in www/app with the name 'webio.db' these
     settings can be changed in settings.php




   Part C - Setting up the IgH EtherCAT software to be used but the Latproc software



The following instructions are brutally terse at present and we apologise
for that. Please watch this space for more precise instructions.

* pull the latproc project from git

  git clone git://github.com/latproc/latproc.git latproc

  please do not cd into the latproc directory yet.

* extract the etherlab kit from the EtherLab website:
    see http://www.etherlab.org/en/ethercat/index.php

  tar/hg/... etc

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

