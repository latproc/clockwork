cmake_minimum_required (VERSION 2.6)
project (Latproc)

set (Latproc_VERSION_MAJOR 0)
set (Latproc_VERSION_MINOR 8)

set (CMAKE_VERBOSE_MAKEFILE false)

set (CMAKE_CXX_FLAGS "-std=gnu++0x -Wall --std=gnu++0x -DUSE_EXPERIMENTAL_IDLE_LOOP -DUSE_READLINE ")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-c++11-extensions -Wno-unused-variable -Wno-deprecated-register")
set (CMAKE_CXX_FLAGS_DEBUG "-g")
set (CMAKE_CXX_FLAGS_RELEASE "-O")
set (CMAKE_EXE_FLAGS_DEBUG "-g")
set (CMAKE_EXE_FLAGS_RELEASE "-O")

set (CLOCKWORK_DIR ${PROJECT_SOURCE_DIR}/src/)
set (BUILD_DIR ${PROJECT_SOURCE_DIR}/build)

configure_file (
  "${PROJECT_SOURCE_DIR}/LatprocConfig.h.in"
  "${PROJECT_BINARY_DIR}/LatprocConfig.h"
)

set (CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})
FIND_PACKAGE(Boost COMPONENTS system thread program_options filesystem REQUIRED )
FIND_PACKAGE(Mosquitto)
FIND_PACKAGE(MODBUS)

add_library (Clockwork 
    ${CLOCKWORK_DIR}/MessagingInterface.cpp 
    ${CLOCKWORK_DIR}/MessageLog.cpp 
    ${CLOCKWORK_DIR}/MessageEncoding.cpp 
    ${CLOCKWORK_DIR}/regular_expressions.cpp 
    ${CLOCKWORK_DIR}/Logger.cpp
    ${CLOCKWORK_DIR}/anet.c
    ${CLOCKWORK_DIR}/cJSON.c
    ${CLOCKWORK_DIR}/DebugExtra.cpp
    ${CLOCKWORK_DIR}/value.cpp 
    ${CLOCKWORK_DIR}/symboltable.cpp
    ${CLOCKWORK_DIR}/Message.cpp )

include_directories("/usr/local/include")
include_directories("/opt/local/include")
include_directories("${PROJECT_SOURCE_DIR}/include")
include_directories("${PROJECT_BINARY_DIR}")
include_directories("${CLOCKWORK_DIR}")

LINK_DIRECTORIES("/usr/local/lib")
LINK_DIRECTORIES("/opt/local/lib")

#	src/iosh.cpp
#	src/modbusd.cpp
#	src/mqttd.cpp
#	src/iod.cpp
#	src/beckhoffd.cpp
#	src/MQTTDCommands.cpp
#	src/ecat_thread.cpp
#	src/schedule.cpp
#	src/test_client.cpp
#	src/zmq_ecat_monitor.cpp
#	src/device_connector.cpp
#	src/persistd.cpp

# yacc instructions from Andy Cedilnik 2002.

set (FLEX_EXECUTABLE lex)
set (BISON_EXECUTABLE bison)

set(cw_lex_output ${BUILD_DIR}/cwlang.yy.cpp)
set(cw_yacc_output ${BUILD_DIR}/cwlang.tab.cpp)
# Create custom command for flex/lex (note the outputs)
 ADD_CUSTOM_COMMAND(
   SOURCE ${CLOCKWORK_DIR}/cwlang.lpp
   COMMAND ${FLEX_EXECUTABLE} 
   ARGS -o${BUILD_DIR}/cwlang.yy.cpp
		${CLOCKWORK_DIR}/cwlang.lpp
   TARGET ClockworkParser
   OUTPUTS ${cw_lex_output})

# Create custom command for bison/yacc (note the DEPENDS)
 ADD_CUSTOM_COMMAND(
   SOURCE ${CLOCKWORK_DIR}/cwlang.ypp
   COMMAND ${BISON_EXECUTABLE} 
   ARGS -y 
        -o ${BUILD_DIR}/cwlang.tab.cpp
		-g -v -d ${CLOCKWORK_DIR}/cwlang.ypp
   TARGET ClockworkParser
   DEPENDS ${CLOCKWORK_DIR}/cwlang.lpp
   OUTPUTS ${cw_yacc_output})

# Add generated source to the list of sources
 SET(Clockwork_SRCS ${Clockwork_SRCS} ${cw_yacc_output} ${cw_lex_output})

# Since the parser source does not exists yet when cmake is run, mark
# it as generated
SET_SOURCE_FILES_PROPERTIES(${cw_lex_output} GENERATED)
SET_SOURCE_FILES_PROPERTIES(${cw_yacc_output} GENERATED)

# Include binary directory to include lexer.c in parser.c
INCLUDE_DIRECTORIES(${PROJECT_BINARY_DIR}/src)

set (Clockwork_SRCS 
	${cw_yacc_output} ${cw_lex_output}
	src/cwlang.ypp
	src/cwlang.lpp
	src/cmdline.ypp
	src/cmdline.lpp
	src/CallMethodAction.cpp
	src/Channel.cpp
	src/ClearListAction.cpp
	src/ClientInterface.cpp
	src/CopyPropertiesAction.cpp
	src/DisableAction.cpp
	src/Dispatcher.cpp
	src/ECInterface.cpp
	src/EnableAction.cpp
	src/ExecuteMessageAction.cpp
	src/Expression.cpp
	src/ExpressionAction.cpp
	src/FireTriggerAction.cpp
	src/HandleMessageAction.cpp
	src/IOComponent.cpp
	src/IODCommands.cpp
	src/IfCommandAction.cpp
	src/IncludeAction.cpp
	src/LockAction.cpp
	src/LogAction.cpp
	src/MQTTInterface.cpp
	src/MachineCommandAction.cpp
	src/MachineInstance.cpp
	src/ModbusInterface.cpp
	src/PatternAction.cpp
	src/PersistentStore.cpp
	src/Plugin.cpp
	src/PolledMessageHandler.cpp
	src/PopListAction.cpp
	src/PredicateAction.cpp
	src/ProcessingThread.cpp
	src/ResumeAction.cpp
	src/Scheduler.cpp
	src/SendMessageAction.cpp
	src/SetListEntriesAction.cpp
	src/SetOperationAction.cpp
	src/SetStateAction.cpp
	src/ShutdownAction.cpp
	src/SortListAction.cpp
	src/State.cpp
	src/UnlockAction.cpp
	src/WaitAction.cpp
	src/clockwork.cpp
	src/dynamic_value.cpp
	src/filtering.cpp
	src/options.cpp
	)
add_executable(cw src/cw.cpp ${Clockwork_SRCS})
set_target_properties (cw PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(cw Clockwork "dl" "zmq" ${Boost_LIBRARIES} ${MOSQUITTO_LIBRARIES})

if(EXISTS "${PROJECT_SOURCE_DIR}/../ethercat/")
set (ETHERCAT_DIR ${PROJECT_SOURCE_DIR}/../ethercat)
add_library (ec_tool 
${ETHERCAT_DIR}/tool/CommandAlias.cpp
${ETHERCAT_DIR}/tool/CommandConfig.cpp
${ETHERCAT_DIR}/tool/Command.cpp
${ETHERCAT_DIR}/tool/CommandCStruct.cpp
${ETHERCAT_DIR}/tool/CommandData.cpp
${ETHERCAT_DIR}/tool/CommandDebug.cpp
${ETHERCAT_DIR}/tool/CommandDomains.cpp
${ETHERCAT_DIR}/tool/CommandDownload.cpp
${ETHERCAT_DIR}/tool/CommandEoe.cpp
${ETHERCAT_DIR}/tool/CommandFoeRead.cpp
${ETHERCAT_DIR}/tool/CommandFoeWrite.cpp
${ETHERCAT_DIR}/tool/CommandGraph.cpp
${ETHERCAT_DIR}/tool/CommandMaster.cpp
${ETHERCAT_DIR}/tool/CommandPdos.cpp
${ETHERCAT_DIR}/tool/CommandRegRead.cpp
${ETHERCAT_DIR}/tool/CommandRegWrite.cpp
${ETHERCAT_DIR}/tool/CommandRescan.cpp
${ETHERCAT_DIR}/tool/CommandSdos.cpp
${ETHERCAT_DIR}/tool/CommandSiiRead.cpp
${ETHERCAT_DIR}/tool/CommandSiiWrite.cpp
${ETHERCAT_DIR}/tool/CommandSlaves.cpp
${ETHERCAT_DIR}/tool/CommandSoeRead.cpp
${ETHERCAT_DIR}/tool/CommandSoeWrite.cpp
${ETHERCAT_DIR}/tool/CommandStates.cpp
${ETHERCAT_DIR}/tool/CommandUpload.cpp
${ETHERCAT_DIR}/tool/CommandVersion.cpp
${ETHERCAT_DIR}/tool/CommandXml.cpp
${ETHERCAT_DIR}/tool/DataTypeHandler.cpp
${ETHERCAT_DIR}/tool/FoeCommand.cpp
${ETHERCAT_DIR}/tool/MasterDevice.cpp
${ETHERCAT_DIR}/tool/NumberListParser.cpp
${ETHERCAT_DIR}/tool/SdoCommand.cpp
${ETHERCAT_DIR}/tool/sii_crc.cpp
../ec_tool/SoeCommand.cpp
../ec_tool/tool_main.cpp
)
INCLUDE_DIRECTORIES(${ETHERCAT_DIR})
INCLUDE_DIRECTORIES(${ETHERCAT_DIR}/tool)
INCLUDE_DIRECTORIES(${ETHERCAT_DIR}/include)
INCLUDE_DIRECTORIES(${ETHERCAT_DIR}/master)
LINK_DIRECTORIES(${ETHERCAT_DIR}/lib/.libs)

add_executable(iod src/iod.cpp src/ecat_thread.cpp src/EtherCATSetup.cpp ${Clockwork_SRCS})
#set_target_properties (iod PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_target_properties (iod PROPERTIES COMPILE_DEFINITIONS "USE_ETHERCAT" )
target_link_libraries(iod Clockwork ec_tool "ethercat" "dl" "zmq" ${Boost_LIBRARIES} ${MOSQUITTO_LIBRARIES})
install(TARGETS iod RUNTIME DESTINATION ${PROJECT_SOURCE_DIR})
endif()

set(iosh_lex_output ${BUILD_DIR}/cmdline.yy.cpp)
set(iosh_yacc_output ${BUILD_DIR}/cmdline.tab.cpp)
# Create custom command for flex/lex (note the outputs)
 ADD_CUSTOM_COMMAND(
   SOURCE ${CLOCKWORK_DIR}/cmdline.lpp
   COMMAND ${FLEX_EXECUTABLE} 
   ARGS -o${BUILD_DIR}/cmdline.yy.cpp
		${CLOCKWORK_DIR}/cmdline.lpp
   TARGET ClockworkParser
   OUTPUTS ${iosh_lex_output})

# Create custom command for bison/yacc (note the DEPENDS)
 ADD_CUSTOM_COMMAND(
   SOURCE ${CLOCKWORK_DIR}/cmdline.ypp
   COMMAND ${BISON_EXECUTABLE} 
   ARGS -y 
        -o ${BUILD_DIR}/cmdline.tab.cpp
		-g -v -d ${CLOCKWORK_DIR}/cmdline.ypp
   TARGET ClockworkParser
   DEPENDS ${CLOCKWORK_DIR}/cmdline.lpp
   OUTPUTS ${iosh_yacc_output})

# Since the parser source does not exists yet when cmake is run, mark
# it as generated
SET_SOURCE_FILES_PROPERTIES(${iosh_lex_output} GENERATED)
SET_SOURCE_FILES_PROPERTIES(${iosh_yacc_output} GENERATED)

add_executable(iosh src/iosh.cpp ${iosh_yacc_output} ${iosh_lex_output})
target_link_libraries(iosh Clockwork "readline" "zmq" ${Boost_LIBRARIES} )

add_executable(device_connector src/device_connector.cpp src/options.cpp )
target_link_libraries(device_connector Clockwork "zmq" ${Boost_LIBRARIES} )

add_executable(persistd src/persistd.cpp src/PersistentStore.cpp )
target_link_libraries(persistd Clockwork "zmq" ${Boost_LIBRARIES} )

add_executable(modbusd src/modbusd.cpp )
target_link_libraries(modbusd Clockwork modbus "zmq" ${Boost_LIBRARIES} )

install(TARGETS cw iosh device_connector modbusd persistd 
        RUNTIME DESTINATION ${PROJECT_SOURCE_DIR})
