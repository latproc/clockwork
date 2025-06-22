cmake_minimum_required (VERSION 3.5)
project (Latproc)

set (Latproc_VERSION_MAJOR 0)
set (Latproc_VERSION_MINOR 9.7)

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/../ethercat/")
  set(USE_ETHERCAT true)
	set(USE_ETHERCAT_SOURCE true)
  # By default support EtherLab builds with EoE enabled
  # this can be overridden in the LocalCMakeLists.txt file
  set (SUPPORT_EOE true)
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEC_EOE")
endif()

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
		  set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/stage" CACHE PATH "..." FORCE)
endif()

if (EXISTS "${PROJECT_SOURCE_DIR}/LocalCMakeLists.txt")
	include("${PROJECT_SOURCE_DIR}/LocalCMakeLists.txt")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (USE_ETHERCAT)
if (USE_SOEM)
	add_subdirectory(ext/SOEM)
	set(CW_ETHERCAT_DIR "${CMAKE_BINARY_DIR}/ext/SOEM")

    link_directories(${PROJECT_SOURCE_DIR}/stage/lib)
	if("${CMAKE_SYSTEM}" MATCHES "Linux")
		set(CW_ETHERCAT_LIB "-lsoem -lpthread -lrt")
	else()
		set(CW_ETHERCAT_LIB "-lsoem -lpthread -lpcap")
	endif()

endif()
endif() # USE_ETHERCAT

cmake_policy(SET CMP0008 NEW)
cmake_policy(SET CMP0015 NEW)

#set (CMAKE_VERBOSE_MAKEFILE false)
set (CMAKE_EXPORT_COMPILE_COMMANDS true)

# Apply -Werror only if using Clang
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options($<$<CONFIG:Debug>:-Werror>)
    add_compile_options($<$<CONFIG:Debug>:-Wall>)
    add_compile_options($<$<CONFIG:Debug>:-Wpedantic>)
endif()

add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=c++14>)
add_compile_options(-DUSE_READLINE )
add_compile_options(-Wno-deprecated-declarations)
add_compile_options(-Wno-unused-variable)
add_compile_options(-Wno-unused-function)
add_compile_options(-Wno-unused-value)
add_compile_options(-Wno-unused-parameter)
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-strict-prototypes>)
IF("${CMAKE_SYSTEM}" MATCHES "Linux")
add_compile_options(-rdynamic)
endif()
set (CMAKE_CXX_FLAGS_RELEASE "-O3")

set (CLOCKWORK_DIR ${PROJECT_SOURCE_DIR}/src/)
set (BUILD_DIR ${PROJECT_SOURCE_DIR}/build)

configure_file (
  "${PROJECT_SOURCE_DIR}/LatprocConfig.h.in"
  "${PROJECT_BINARY_DIR}/LatprocConfig.h"
)

set (CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake/Modules/")
FIND_PACKAGE(Boost COMPONENTS system thread program_options filesystem chrono date_time context REQUIRED)
FIND_PACKAGE(Mosquitto)
FIND_PACKAGE(MODBUS)
FIND_PACKAGE(LibXml2)
FIND_PACKAGE(ZeroMQ REQUIRED)
FIND_PACKAGE(CURL)

INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIR} ${ZeroMQ_INCLUDE_DIR})

set(HEADER_FILES
	${CLOCKWORK_DIR}/cw_client.h
	${CLOCKWORK_DIR}/MessagingInterface.h
	${CLOCKWORK_DIR}/SocketMonitor.h
	${CLOCKWORK_DIR}/ConnectionManager.h
	${CLOCKWORK_DIR}/MessageLog.h
	${CLOCKWORK_DIR}/MessageEncoding.h
	${CLOCKWORK_DIR}/MessageHeader.h
	${CLOCKWORK_DIR}/regular_expressions.h
	${CLOCKWORK_DIR}/Logger.h
	${CLOCKWORK_DIR}/anet.h
	${CLOCKWORK_DIR}/clock.h
	${CLOCKWORK_DIR}/cJSON.h
	${CLOCKWORK_DIR}/DebugExtra.h
	${CLOCKWORK_DIR}/value.h
	${CLOCKWORK_DIR}/set_default_value_mixin.h
	${CLOCKWORK_DIR}/symboltable.h
	${CLOCKWORK_DIR}/rate.h
	${CLOCKWORK_DIR}/helpers.h
	${CLOCKWORK_DIR}/watchdog.h
	${CLOCKWORK_DIR}/Message.h
	${CLOCKWORK_DIR}/Receiver.h
    ${CLOCKWORK_DIR}/json_expr_parser.h
    ${CLOCKWORK_DIR}/json_expression.h
    ${CLOCKWORK_DIR}/process.h
    ${CLOCKWORK_DIR}/ThreadSafeQueue.h
    ${CLOCKWORK_DIR}/ThreadSafeList.h
    )

add_library (cw_client
	${CLOCKWORK_DIR}/MessagingInterface.cpp
	${CLOCKWORK_DIR}/MessageLog.cpp
	${CLOCKWORK_DIR}/MessageEncoding.cpp
	${CLOCKWORK_DIR}/MessageHeader.cpp
	${CLOCKWORK_DIR}/regular_expressions.cpp
	${CLOCKWORK_DIR}/CommandManager.cpp
	${CLOCKWORK_DIR}/SocketMonitor.cpp
	${CLOCKWORK_DIR}/ConnectionManager.cpp
	${CLOCKWORK_DIR}/Logger.cpp
	${CLOCKWORK_DIR}/anet.c
	${CLOCKWORK_DIR}/clock.cpp
	${CLOCKWORK_DIR}/cJSON.c
	${CLOCKWORK_DIR}/DebugExtra.cpp
	${CLOCKWORK_DIR}/value.cpp
	${CLOCKWORK_DIR}/symboltable.cpp
	${CLOCKWORK_DIR}/rate.cpp
	${CLOCKWORK_DIR}/helpers.cpp
	${CLOCKWORK_DIR}/Message.cpp
	${CLOCKWORK_DIR}/watchdog.cpp
    ${CLOCKWORK_DIR}/json_expr_parser.cpp
    ${CLOCKWORK_DIR}/json_expression.cpp
    ${CLOCKWORK_DIR}/process.cpp
	${HEADER_FILES}
)


include_directories("/usr/local/include")
if (EXISTS "/opt/local/include")
include_directories("/opt/local/include")
endif()
include_directories("${PROJECT_SOURCE_DIR}/include")
include_directories("${PROJECT_BINARY_DIR}")
include_directories("${CLOCKWORK_DIR}")

LINK_DIRECTORIES("/usr/local/lib")
if (EXISTS "/opt/local/lib")
LINK_DIRECTORIES("/opt/local/lib")
endif()

# yacc instructions from Andy Cedilnik 2002.

if ("${FLEX_EXECUTABLE}" STREQUAL "")
set (FLEX_EXECUTABLE flex)
endif()

if ("${BISON_EXECUTABLE}" STREQUAL "")
set (BISON_EXECUTABLE bison)
endif()

set(cw_lex_output ${CMAKE_BINARY_DIR}/cwlang.yy.cpp)
set(cw_yacc_output ${CMAKE_BINARY_DIR}/cwlang.tab.cpp)
# Create custom command for flex/lex (note the outputs)
 ADD_CUSTOM_COMMAND(
   DEPENDS ${CLOCKWORK_DIR}/cwlang.lpp
   COMMAND ${FLEX_EXECUTABLE}
   ARGS -o${CMAKE_BINARY_DIR}/cwlang.yy.cpp
		${CLOCKWORK_DIR}/cwlang.lpp
		#   TARGET ClockworkParser
   OUTPUT ${cw_lex_output})

# Create custom command for bison/yacc (note the DEPENDS)
 ADD_CUSTOM_COMMAND(
   DEPENDS ${CLOCKWORK_DIR}/cwlang.ypp
   COMMAND ${BISON_EXECUTABLE}
   ARGS -y
        -o ${CMAKE_BINARY_DIR}/cwlang.tab.cpp
		-g -v -d ${CLOCKWORK_DIR}/cwlang.ypp
		#TARGET ClockworkParser
   DEPENDS ${CLOCKWORK_DIR}/cwlang.lpp
   OUTPUT ${cw_yacc_output})

# Add generated source to the list of sources
 SET(Clockwork_SRCS ${Clockwork_SRCS} ${cw_yacc_output} ${cw_lex_output})

# Since the parser source does not exists yet when cmake is run, mark
# it as generated
SET_SOURCE_FILES_PROPERTIES(${cw_lex_output} GENERATED)
SET_SOURCE_FILES_PROPERTIES(${cw_yacc_output} GENERATED)

# Include binary directory to include lexer.c in parser.c
INCLUDE_DIRECTORIES(${PROJECT_BINARY_DIR}/src)

set (Clockwork_Headers
    src/AbortAction.h
    src/Action.h
    src/ActionList.h
    src/anet.h
    src/arraystr.h
    src/AutoStats.h
    src/bit_ops.h
    src/buffering.h
    src/CallMethodAction.h
    src/Channel.h
    src/ClearListAction.h
    src/ClientInterface.h
    src/clock_sync.h
    src/clockwork.h
    src/cmdline.h
    src/CommandManager.h
    src/Configuration.h
    src/ConditionHandler.h
    src/ConnectionManager.h
    src/ControlSystemMachine.h
    src/copy_environment.h
    src/CopyPropertiesAction.h
    src/CounterRateFilterSettings.h
    src/CounterRateInstance.h
    src/cw_client.h
    src/cwlang.h
    src/debug_malloc.h
    src/DebugExtra.h
    src/DisableAction.h
    src/Dispatcher.h
    src/domain.h
    src/dynamic_value.h
    src/ecat_thread.h
    src/ECInterface.h
    src/EnableAction.h
    src/error_state.h
    src/EtherCATSetup.h
    src/exec_command.h
    src/ExecuteMessageAction.h
    src/ExportState.h
    src/Expression.h
    src/ExpressionAction.h
    src/filtering.h
    src/FireTriggerAction.h
    src/HandleMessageAction.h
    src/HandleRequestAction.h
    src/hw_config.h
    src/IfCommandAction.h
    src/IncludeAction.h
    src/IOComponent.h
    src/IODCommand.h
    src/IODCommands.h
    src/json_expr_parser.h
    src/json_expression.h
    src/LoadPropertiesAction.h
    src/LockAction.h
    src/LogAction.h
    src/Logger.h
    src/MachineClass.h
    src/MachineCommandAction.h
    src/MachineCommandList.h
    src/MachineDetails.h
    src/MachineInstance.h
    src/MachineInterface.h
    src/MachineShadowInstance.h
    src/Message.h
    src/MessageEncoding.h
    src/MessageLog.h
    src/MessagingInterface.h
    src/ModbusInterface.h
    src/MQTTDCommand.h
    src/MQTTDCommands.h
    src/MQTTInterface.h
    src/options.h
    src/Parameter.h
    src/parameter_array.h
    src/PatternAction.h
    src/PersistentStore.h
    src/Plugin.h
    src/PolledMessageHandler.h
    src/PopListAction.h
    src/PredicateAction.h
    src/process.h
    src/process_data.h
    src/ProcessingThread.h
    src/rate.h
    src/RateEstimatorInstance.h
    src/read_file.h
    src/regular_expressions.h
    src/ResetAction.h
    src/ResumeAction.h
    src/Scheduler.h
    src/SDOEntry.h
    src/SendMessageAction.h
    src/SetListEntriesAction.h
    src/SetOperationAction.h
    src/SetStateAction.h
    src/SharedWorkSet.h
    src/SharedQueueManager.h
    src/ShutdownAction.h
    src/SimulatedRawInput.h
    src/SimulatedRawOutput.h
    src/SocketMonitor.h
    src/SortListAction.h
    src/split_string.h
    src/StableState.h
    src/State.h
    src/Statistic.h
    src/Statistics.h
    src/StatisticList.h
    src/StatisticHelper.h
    src/symboltable.h
    src/SyncRemoteStatesAction.h
    src/Transition.h
    src/Trigger.h
    src/UnlockAction.h
    src/urlencode.h
    src/value.h
    src/WaitAction.h
)
if (USE_ETHERCAT)
	LIST (APPEND Clockwork_Headers "src/EtherCATthread.h")
endif()

set (Clockwork_SRCS
	${Clockwork_Headers}
	${cw_yacc_output} ${cw_lex_output}
    src/AbortAction.cpp
    src/Action.cpp
    src/ActionList.cpp
    src/AutoStats.cpp
    src/bit_ops.cpp
    src/CallMethodAction.cpp
    src/Channel.cpp
    src/ClearListAction.cpp
    src/ClientInterface.cpp
    src/clock_sync.cpp
    src/clockwork.cpp
    src/cmdline.lpp
    src/cmdline.ypp
    src/ConditionHandler.cpp
    src/Configuration.cpp
    src/ControlSystemMachine.cpp
    src/copy_environment.c
    src/CopyPropertiesAction.cpp
    src/CounterRateInstance.cpp
    src/cwlang.lpp
    src/cwlang.ypp
    src/debug_malloc.c
    src/DisableAction.cpp
    src/Dispatcher.cpp
    src/dynamic_value.cpp
    src/ECInterface.cpp
    src/EnableAction.cpp
    src/error_state.cpp
    src/exec_command.c
    src/ExecuteMessageAction.cpp
    src/ExportState.cpp
    src/Expression.cpp
    src/ExpressionAction.cpp
    src/filtering.cpp
    src/FireTriggerAction.cpp
    src/handle_command.cpp
    src/handle_hardware.cpp
    src/handle_machines.cpp
    src/handle_scheduler.cpp
    src/HandleMessageAction.cpp
    src/HandleRequestAction.cpp
    src/IfCommandAction.cpp
    src/IncludeAction.cpp
    src/IOComponent.cpp
    src/IODCommands.cpp
    src/json_expr_parser.cpp
    src/json_expression.cpp
    src/LoadPropertiesAction.cpp
    src/LockAction.cpp
    src/LogAction.cpp
    src/MachineClass.cpp
    src/MachineCommandAction.cpp
    src/MachineCommandList.cpp
    src/MachineDetails.cpp
    src/MachineInstance.cpp
    src/MachineInterface.cpp
    src/MachineShadowInstance.cpp
    src/ModbusInterface.cpp
    src/MQTTInterface.cpp
    src/options.cpp
    src/Parameter.cpp
    src/parameter_array.cpp
    src/PatternAction.cpp
    src/PersistentStore.cpp
    src/Plugin.cpp
    src/PolledMessageHandler.cpp
    src/PopListAction.cpp
    src/PredicateAction.cpp
    src/process.cpp
    src/process_data.cpp
    src/ProcessingThread.cpp
    src/RateEstimatorInstance.cpp
    src/read_file.c
    src/ResetAction.cpp
    src/ResumeAction.cpp
    src/Scheduler.cpp
    src/SendMessageAction.cpp
    src/SetListEntriesAction.cpp
    src/SetOperationAction.cpp
    src/SetStateAction.cpp
    src/SharedWorkSet.cpp
    src/ShutdownAction.cpp
    src/SortListAction.cpp
    src/split_string.c
    src/StableState.cpp
    src/State.cpp
    src/StatisticHelper.cpp
    src/SyncRemoteStatesAction.cpp
    src/Transition.cpp
    src/Trigger.cpp
    src/UnlockAction.cpp
    src/urlencode.cpp
    src/wait_for_work.cpp
    src/WaitAction.cpp
	)
if (USE_ETHERCAT)
if (USE_SOEM)
	include_directories("${PROJECT_SOURCE_DIR}/ext/SOEM/soem")
	include_directories("${PROJECT_SOURCE_DIR}/ext/SOEM/osal")
	include_directories("${PROJECT_SOURCE_DIR}/ext/SOEM/oshw")
	if("${CMAKE_SYSTEM}" MATCHES "Linux")
		include_directories("${PROJECT_SOURCE_DIR}/ext/SOEM/osal/linux")
		include_directories("${PROJECT_SOURCE_DIR}/ext/SOEM/oshw/linux")
	else()
		include_directories("${PROJECT_SOURCE_DIR}/ext/SOEM/osal/macosx")
		include_directories("${PROJECT_SOURCE_DIR}/ext/SOEM/oshw/macosx")
	endif()

	LIST (APPEND Clockwork_SRCS "src/EtherCATthread.cpp")
endif()
endif()
add_library(cw_interpreter ${Clockwork_SRCS})
set_target_properties (cw_interpreter PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )

add_executable(cw src/cw.cpp) # ${Clockwork_SRCS})
set_target_properties (cw PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
set_target_properties(cw PROPERTIES LINK_FLAGS "-rdynamic")
#target_link_libraries(cw cw_client "dl" ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES})
target_link_libraries(cw cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} dl pthread)

if (USE_ETHERCAT)
# Experimental cw with SOEM EtherCAT library support
if(USE_SOEM)
add_executable(cw_ecat src/cw.cpp) # ${Clockwork_SRCS})
set_target_properties (cw_ecat PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR;SOEM_ETHERCAT" )
set_target_properties(cw_ecat PROPERTIES LINK_FLAGS "-rdynamic")
target_link_directories(cw_ecat PRIVATE "${CW_ETHERCAT_DIR}")
target_link_libraries(cw_ecat cw_interpreter cw_client dl ${CW_ETHERCAT_LIB} ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} "pthread")
endif()

if(USE_ETHERCAT_SOURCE)
if (NOT EXISTS "${PROJECT_SOURCE_DIR}/../ethercat/")
	message("Cannot find EtherLab source")
endif()
set (ETHERCAT_DIR ${PROJECT_SOURCE_DIR}/../ethercat)
set (ectool_extras "")
if (EXISTS "${ETHERCAT_DIR}/tool/CommandDiag.cpp")
  LIST (APPEND ectool_extras "${ETHERCAT_DIR}/tool/CommandDiag.cpp")
endif()
if (SUPPORT_EOE)
  if (EXISTS "${ETHERCAT_DIR}/tool/CommandEoeAddIf.cpp")
    LIST (APPEND ectool_extras "${ETHERCAT_DIR}/tool/CommandEoeAddIf.cpp")
  endif()
  if (EXISTS "${ETHERCAT_DIR}/tool/CommandEoeDelIf.cpp")
    LIST (APPEND ectool_extras "${ETHERCAT_DIR}/tool/CommandEoeDelIf.cpp")
  endif()
  if (EXISTS "${ETHERCAT_DIR}/tool/CommandEoe.cpp")
    LIST (APPEND ectool_extras "${ETHERCAT_DIR}/tool/CommandEoe.cpp")
  endif()
  if (EXISTS "${ETHERCAT_DIR}/tool/CommandIp.cpp")
    LIST (APPEND ectool_extras "${ETHERCAT_DIR}/tool/CommandIp.cpp")
  endif()
endif()
if (EXISTS "${ETHERCAT_DIR}/tool/CommandPcap.cpp")
  LIST (APPEND ectool_extras "${ETHERCAT_DIR}/tool/CommandPcap.cpp")
endif()
if (EXISTS "${ETHERCAT_DIR}/tool/CommandRegReadWrite.cpp")
  LIST (APPEND ectool_extras "${ETHERCAT_DIR}/tool/CommandRegReadWrite.cpp")
endif()
if (EXISTS "${ETHERCAT_DIR}/tool/CommandReboot.cpp")
  LIST (APPEND ectool_extras "${ETHERCAT_DIR}/tool/CommandReboot.cpp")
endif()
if (EXISTS "${ETHERCAT_DIR}/tool/CommandCrc.cpp")
  LIST (APPEND ectool_extras "${ETHERCAT_DIR}/tool/CommandCrc.cpp")
endif()
add_library (ec_tool
${ETHERCAT_DIR}/tool/CommandAlias.cpp
${ETHERCAT_DIR}/tool/CommandConfig.cpp
${ETHERCAT_DIR}/tool/Command.cpp
${ETHERCAT_DIR}/tool/CommandCStruct.cpp
${ETHERCAT_DIR}/tool/CommandData.cpp
${ETHERCAT_DIR}/tool/CommandDebug.cpp
${ETHERCAT_DIR}/tool/CommandDomains.cpp
${ETHERCAT_DIR}/tool/CommandDownload.cpp
${ETHERCAT_DIR}/tool/CommandFoeRead.cpp
${ETHERCAT_DIR}/tool/CommandFoeWrite.cpp
${ETHERCAT_DIR}/tool/CommandGraph.cpp
${ectool_extras}
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
${ExtraECToolFiles}
../ec_tool/SoeCommand.cpp
../ec_tool/tool_main.cpp
)
INCLUDE_DIRECTORIES(${ETHERCAT_DIR})
INCLUDE_DIRECTORIES(${ETHERCAT_DIR}/tool)
INCLUDE_DIRECTORIES(${ETHERCAT_DIR}/include)
INCLUDE_DIRECTORIES(${ETHERCAT_DIR}/master)
LINK_DIRECTORIES(${ETHERCAT_DIR}/lib/.libs)
endif()

INCLUDE_DIRECTORIES(${LIBXML2_INCLUDE_DIR})
LINK_DIRECTORIES("LIBXML2_LIBRARIES")

add_executable(iod src/iod.cpp src/ecat_thread.cpp src/EtherCATSetup.cpp
	src/ethercat_xml_parser.cpp ${Clockwork_SRCS}
)

set_target_properties (iod PROPERTIES COMPILE_DEFINITIONS "USE_ETHERCAT;KEEP_STATS;USE_CHRONO" )
set_target_properties(iod PROPERTIES LINK_FLAGS "-rdynamic")
if(USE_ETHERCAT_SOURCE)
target_link_libraries(iod cw_client ec_tool ethercat dl ${ZeroMQ_LIBRARY} xml2 ${Boost_LIBRARIES} "mosquitto" "pthread" rt)
else()
target_link_libraries(iod cw_client ethercat dl ${ZeroMQ_LIBRARY} xml2 ${Boost_LIBRARIES} "mosquitto" "pthread" rt)
endif()
install(TARGETS iod RUNTIME DESTINATION ${PROJECT_SOURCE_DIR})

add_executable(iod_sdo src/iod.cpp src/ecat_thread.cpp src/EtherCATSetup.cpp
	src/ethercat_xml_parser.cpp ${Clockwork_SRCS}
)
#set_target_properties (iod_sdo PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_compile_definitions(iod_sdo PRIVATE USE_ETHERCAT=1 USE_SDO=1 USE_CHRONO=1 )
set_target_properties(iod_sdo PROPERTIES LINK_FLAGS "-rdynamic")
if(USE_ETHERCAT_SOURCE)
target_link_libraries(iod_sdo cw_client ec_tool ethercat dl ${ZeroMQ_LIBRARY} xml2 ${Boost_LIBRARIES} "mosquitto" "pthread" rt)
else()
target_link_libraries(iod_sdo cw_client ethercat dl ${ZeroMQ_LIBRARY} xml2 ${Boost_LIBRARIES} "mosquitto" "pthread" rt)
endif()
install(TARGETS iod_sdo RUNTIME DESTINATION ${PROJECT_SOURCE_DIR})
endif()

set(iosh_lex_output ${CMAKE_BINARY_DIR}/cmdline.yy.cpp)
set(iosh_yacc_output ${CMAKE_BINARY_DIR}/cmdline.tab.cpp)
# Create custom command for flex/lex (note the outputs)
 ADD_CUSTOM_COMMAND(
   DEPENDS ${CLOCKWORK_DIR}/cmdline.lpp
   COMMAND ${FLEX_EXECUTABLE}
   ARGS -o${CMAKE_BINARY_DIR}/cmdline.yy.cpp
		${CLOCKWORK_DIR}/cmdline.lpp
		#TARGET ClockworkParser
   OUTPUT ${iosh_lex_output})

# Create custom command for bison/yacc (note the DEPENDS)
 ADD_CUSTOM_COMMAND(
   DEPENDS ${CLOCKWORK_DIR}/cmdline.ypp
   COMMAND ${BISON_EXECUTABLE}
   ARGS -y
        -o ${CMAKE_BINARY_DIR}/cmdline.tab.cpp
		-g -v -d ${CLOCKWORK_DIR}/cmdline.ypp
		#TARGET ClockworkParser
   DEPENDS ${CLOCKWORK_DIR}/cmdline.lpp
   OUTPUT ${iosh_yacc_output})

# Since the parser source does not exists yet when cmake is run, mark
# it as generated
SET_SOURCE_FILES_PROPERTIES(${iosh_lex_output} GENERATED)
SET_SOURCE_FILES_PROPERTIES(${iosh_yacc_output} GENERATED)

add_executable(iosh src/iosh.cpp ${iosh_yacc_output} ${iosh_lex_output})
target_link_libraries(iosh cw_client "readline" ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} "pthread")

add_executable(iocmd src/iocmd.cpp)
target_link_libraries(iocmd cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} "pthread")

add_executable(device_connector src/device_connector.cpp src/options.cpp )
target_link_libraries(device_connector cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} "pthread")

add_executable(persistd src/persistd.cpp src/PersistentStore.cpp )
target_link_libraries(persistd cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} "pthread")

add_executable(modbusd src/modbusd.cpp )
target_link_libraries(modbusd cw_client ${MODBUS_LIBRARIES} ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} "pthread")

add_executable(dbd src/dbd.cpp )
target_link_libraries(dbd cw_client ${MODBUS_LIBRARIES} ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} "pthread")

if (RUN_TESTS)
enable_testing()
add_subdirectory(tests)
endif()

install(TARGETS cw dbd iosh iocmd device_connector modbusd persistd RUNTIME DESTINATION ${PROJECT_SOURCE_DIR})
if(USE_SOEM)
	install(TARGETS cw_ecat RUNTIME DESTINATION ${PROJECT_SOURCE_DIR})
endif()
install(TARGETS cw_client ARCHIVE DESTINATION ${PROJECT_SOURCE_DIR}/stage/lib)
install(TARGETS cw_interpreter ARCHIVE DESTINATION ${PROJECT_SOURCE_DIR}/stage/lib)
#install(DIRECTORY src/ DESTINATION include/clockwork FILES_MATCHING PATTERN "*.h")
