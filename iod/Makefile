# Copyright (C) 2012 Martin Leadbeater, Michael O'Connor
# 
# This file is part of Latproc
#
# Latproc is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# Latproc is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with Latproc; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
# 

APPS = iod beckhoffd iosh persistd modbusd cw

#SIMULATED=-DEC_SIMULATOR=1
SIMULATED=-I/usr/local/include -Iethercat/include/ -Lethercat/lib/.libs -lethercat
CFLAGS = $(SIMULATED) -Iethercat -Iethercat/master -Iethercat/include -Iethercat/tool -g -pedantic -Wall #-pg
CC = g++ $(CFLAGS)
LDFLAGS = -L/usr/local/lib  #-pg
TOOLLIB = ../../tool/build/*.o

all:	$(APPS)

outputtest:	outputtest.cpp
		$(CC) -o $@ $(LDFLAGS) -I ../ethercat/include/ -L ../ethercat/lib/ outputtest.cpp -l ethercat

ex1:	ex1.c
		gcc -o $@ -I ../ethercat/include/ -L ../ethercat/lib/ ex1.c -l ethercat

ex2:	ex2.c
		gcc -o $@ -I ../ethercat/include/ -L ../ethercat/lib/ ex2.c -l ethercat

ex3:	ex3.c
		gcc -o $@ -I ../ethercat/include/ -L ../ethercat/lib/ ex2.c -l ethercat

DebugExtra.o:	DebugExtra.cpp DebugExtra.h Logger.h
		$(CC) -c -o $@ DebugExtra.cpp

ECInterface.o:	ECInterface.cpp ECInterface.h
		$(CC) -c -o $@ ECInterface.cpp

IOComponent.o:	IOComponent.cpp IOComponent.h
		$(CC) -c -o $@ IOComponent.cpp

Message.o:	Message.cpp Message.h
		$(CC) -c -o $@ Message.cpp

Scheduler.o:	Scheduler.cpp Scheduler.h
		$(CC) -c -o $@ Scheduler.cpp

MessagingInterface.o:	MessagingInterface.cpp MessagingInterface.h
		$(CC) -c -o $@ MessagingInterface.cpp

#Output.o:	Output.cpp Output.h
#		$(CC) -c -o $@ Output.cpp
#
#Input.o:	Input.cpp Input.h RawInput.h SimulatedRawInput.h
#		$(CC) -c -o $@ Input.cpp

iod.o:	iod.cpp	MessagingInterface.h
		$(CC) -c -o $@ iod.cpp

iod:	iod.o IODCommand.h ECInterface.o IOComponent.o Message.o MessagingInterface.o \
			Dispatcher.o symboltable.o DebugExtra.o Logger.o State.o cJSON.o options.o MachineInstance.o \
			latprocc.tab.o latprocc.yy.o Scheduler.o FireTriggerAction.o IfCommandAction.o Expression.o \
			DisableAction.o EnableAction.o ExpressionAction.o LogAction.o PredicateAction.o \
			LockAction.o UnlockAction.o ModbusInterface.o ResumeAction.o ShutdownAction.o \
			SetStateAction.o WaitAction.o SendMessageAction.o HandleMessageAction.o \
			CallMethodAction.o ExecuteMessageAction.o MachineCommandAction.o IODCommands.o
	$(CC) -o $@ iod.o ECInterface.o IOComponent.o Message.o MessagingInterface.o -lzmq -lboost_thread \
			-lboost_system -lboost_filesystem \
			Dispatcher.o symboltable.o DebugExtra.o Logger.o State.o cJSON.o options.o MachineInstance.o \
			latprocc.tab.o latprocc.yy.o Scheduler.o $(TOOLLIB) Expression.o FireTriggerAction.o IfCommandAction.o \
			DisableAction.o EnableAction.o ExpressionAction.o LogAction.o PredicateAction.o \
			LockAction.o UnlockAction.o ModbusInterface.o ResumeAction.o ShutdownAction.o \
			SetStateAction.o WaitAction.o SendMessageAction.o HandleMessageAction.o \
			CallMethodAction.o ExecuteMessageAction.o MachineCommandAction.o IODCommands.o

beckhoffd:	beckhoffd.cpp IODCommand.h ECInterface.o IOComponent.o Message.o MessagingInterface.o  \
			Logger.o State.o DebugExtra.o cJSON.o options.o MachineInstance.o \
			latprocc.tab.o latprocc.yy.o Scheduler.o FireTriggerAction.o IfCommandAction.o Expression.o \
			ModbusInterface.o SetStateAction.o ExecuteMessageAction.o MachineCommandAction.o HandleMessageAction.o \
			CallMethodAction.o IODCommands.o
	$(CC) -o $@ beckhoffd.cpp ECInterface.o DebugExtra.o IOComponent.o Message.o MessagingInterface.o \
			-lzmq -lboost_thread \
			Logger.o State.o cJSON.o options.o MachineInstance.o Dispatcher.o symboltable.o Scheduler.o \
			Expression.o FireTriggerAction.o IfCommandAction.o ModbusInterface.o \
			SetStateAction.o ExecuteMessageAction.o MachineCommandAction.o HandleMessageAction.o \
			CallMethodAction.o $(TOOLLIB) IODCommands.o

cw.o:	cw.cpp	MessagingInterface.h
		$(CC) -DEC_SIMULATOR -c -o $@ cw.cpp

cw:	cw.o IODCommand.h ECInterface.o IOComponent.o Message.o MessagingInterface.o \
			Dispatcher.o symboltable.o DebugExtra.o Logger.o State.o cJSON.o options.o MachineInstance.o \
			latprocc.tab.o latprocc.yy.o Scheduler.o FireTriggerAction.o IfCommandAction.o Expression.o \
			DisableAction.o EnableAction.o ExpressionAction.o LogAction.o PredicateAction.o \
			LockAction.o UnlockAction.o ModbusInterface.o ResumeAction.o ShutdownAction.o \
			SetStateAction.o WaitAction.o SendMessageAction.o HandleMessageAction.o \
			CallMethodAction.o ExecuteMessageAction.o MachineCommandAction.o IODCommands.o
	$(CC) -o $@ cw.o ECInterface.o IOComponent.o Message.o MessagingInterface.o -lzmq -lboost_thread \
			-lboost_system -lboost_filesystem \
			Dispatcher.o symboltable.o DebugExtra.o Logger.o State.o cJSON.o options.o MachineInstance.o \
			latprocc.tab.o latprocc.yy.o Scheduler.o Expression.o FireTriggerAction.o IfCommandAction.o \
			DisableAction.o EnableAction.o ExpressionAction.o LogAction.o PredicateAction.o \
			LockAction.o UnlockAction.o ModbusInterface.o ResumeAction.o ShutdownAction.o \
			SetStateAction.o WaitAction.o SendMessageAction.o HandleMessageAction.o CallMethodAction.o \
			ExecuteMessageAction.o MachineCommandAction.o IODCommands.o

persistd:	persistd.cpp symboltable.o Logger.o DebugExtra.o
	$(CC) -o persistd persistd.cpp -lzmq -I. -L/usr/local/lib -lboost_program_options \
			symboltable.o Logger.o DebugExtra.o

modbusd:	modbusd.cpp symboltable.o Logger.o DebugExtra.o MessagingInterface.o
	$(CC) -o modbusd modbusd.cpp -lzmq -I. -L/usr/local/lib -lboost_program_options -lboost_thread \
			symboltable.o Logger.o DebugExtra.o -lmodbus MessagingInterface.o

iosh:		iosh.cpp
			g++ $(CFLAGS) -o iosh iosh.cpp -lzmq

CallMethodAction.o:	CallMethodAction.cpp CallMethodAction.h
		$(CC) -c -o $@ CallMethodAction.cpp

DisableAction.o:	DisableAction.cpp DisableAction.h
		$(CC) -c -o $@ DisableAction.cpp

Dispatcher.o:	Dispatcher.cpp Dispatcher.h
		$(CC) -c -o $@ Dispatcher.cpp

ExecuteMessageAction.o:	ExecuteMessageAction.cpp ExecuteMessageAction.h
		$(CC) -c -o $@ ExecuteMessageAction.cpp

EnableAction.o:	EnableAction.cpp EnableAction.h
		$(CC) -c -o $@ EnableAction.cpp

LockAction.o:	LockAction.cpp LockAction.h
		$(CC) -c -o $@ LockAction.cpp

UnlockAction.o:	UnlockAction.cpp UnlockAction.h
		$(CC) -c -o $@ UnlockAction.cpp

Expression.o:	Expression.cpp Expression.h
		$(CC) -c -o $@ Expression.cpp

ExpressionAction.o:	ExpressionAction.cpp ExpressionAction.h
		$(CC) -c -o $@ ExpressionAction.cpp

IfCommandAction.o:	IfCommandAction.cpp IfCommandAction.h
		$(CC) -c -o $@ IfCommandAction.cpp

FireTriggerAction.o:	FireTriggerAction.cpp FireTriggerAction.h Expression.h
		$(CC) -c -o $@ FireTriggerAction.cpp

HandleMessageAction.o:	HandleMessageAction.cpp HandleMessageAction.h
		$(CC) -c -o $@ HandleMessageAction.cpp

MachineCommandAction.o:	MachineCommandAction.cpp MachineCommandAction.h
		$(CC) -c -o $@ MachineCommandAction.cpp

PredicateAction.o:	PredicateAction.cpp PredicateAction.h Expression.h
		$(CC) -c -o $@ PredicateAction.cpp

SetStateAction.o:	SetStateAction.cpp SetStateAction.h
		$(CC) -c -o $@ SetStateAction.cpp

SendMessageAction.o:	SendMessageAction.cpp SendMessageAction.h
		$(CC) -c -o $@ SendMessageAction.cpp

symboltable.o:	symboltable.cpp symboltable.h
		$(CC) -c -o $@ symboltable.cpp

ModbusInterface.o:	ModbusInterface.cpp
		$(CC) -c -o $@ ModbusInterface.cpp

Logger.o:	Logger.cpp Logger.h
		$(CC) -c -o $@ Logger.cpp

ResumeAction.o:	ResumeAction.cpp ResumeAction.h
		$(CC) -c -o $@ ResumeAction.cpp

ShutdownAction.o:	ShutdownAction.cpp ShutdownAction.h
		$(CC) -c -o $@ ShutdownAction.cpp

WaitAction.o:	WaitAction.cpp WaitAction.h
		$(CC) -c -o $@ WaitAction.cpp

State.o:	State.cpp State.h
		$(CC) -c -o $@ State.cpp

cJSON.o:	cJSON.c cJSON.h
		$(CC) -c -o $@ cJSON.c

options.o:	options.cpp options.h
		$(CC) -c -o $@ options.cpp

MasterDevice.o:	MasterDevice.cpp MasterDevice.h
		$(CC) -c -o $@ MasterDevice.cpp

MachineInstance.o:	MachineInstance.cpp MachineInstance.h
		$(CC) -c -o $@ MachineInstance.cpp

latprocc.tab.cpp:	latprocc.ypp latprocc.lpp
	yacc -o $@ -g -v -d latprocc.ypp

latprocc.yy.cpp:	latprocc.lpp
	lex -o $@ latprocc.lpp

latprocc.tab.o:	latprocc.tab.cpp
		$(CC) -c -o $@ latprocc.tab.cpp

latprocc.yy.o:	latprocc.yy.cpp
		$(CC) -c -o $@ latprocc.yy.cpp


clean:
	rm -f $(APPS) *.o latprocc.tab.cpp latprocc.tab.hpp latprocc.yy.cpp
	rm -rf iod.dSYM

