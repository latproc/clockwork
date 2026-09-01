# 3.5: still present on some older Ubuntu HMI panels (e.g. 16.04 / cmake 3.5.1).
# Prefer 3.10+ where available; avoid features that require newer CMake unless gated.
cmake_minimum_required (VERSION 3.5)
project (Latproc)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set (Latproc_VERSION_MAJOR 0)
set (Latproc_VERSION_MINOR 9.7)
option(USE_ASAN "Enable AddressSanitizer + UndefinedBehaviorSanitizer instrumentation" OFF)

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
endif()

# This branch (feature/iod-elc-kernel-transport) builds plant iod as iod-elc only.
# Legacy IgH ecrt iod/iod_sdo live on prod-experimental-mqtt-fix (line A).
option(BUILD_IOD_ELC "Build iod-elc (libelcethercat kernel transport)" ON)

if(BUILD_IOD_ELC)
  set(USE_ETHERCAT true)
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

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

function(target_link_directories_compat target dir)
       if(CMAKE_VERSION VERSION_LESS "3.13")
               target_link_libraries(${target} "-L${dir}")
       else()
               target_link_directories(${target} PRIVATE "${dir}")
       endif()
endfunction()

#set (CMAKE_VERBOSE_MAKEFILE false)
set (CMAKE_EXPORT_COMPILE_COMMANDS true)

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Wall ")
set(CLOCKWORK_SANITIZER_FLAGS "")
if (USE_ASAN)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set(CLOCKWORK_SANITIZER_FLAGS "-fsanitize=address,undefined -fno-omit-frame-pointer")
    else()
        message(FATAL_ERROR "USE_ASAN requires a Clang or GCC toolchain")
    endif()
endif()
if (CLOCKWORK_SANITIZER_FLAGS)
    # add_compile_definitions needs CMake >= 3.12
    if (CMAKE_VERSION VERSION_LESS "3.12")
        add_definitions(-DBOOST_USE_ASAN)
    else()
        add_compile_definitions(BOOST_USE_ASAN)
    endif()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CLOCKWORK_SANITIZER_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CLOCKWORK_SANITIZER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${CLOCKWORK_SANITIZER_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${CLOCKWORK_SANITIZER_FLAGS}")
    message(STATUS "Sanitizers enabled: ${CLOCKWORK_SANITIZER_FLAGS}")
endif()
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-variable")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-function")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-value")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-literal-operator")
endif()
IF("${CMAKE_SYSTEM}" MATCHES "Linux")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -rdynamic")
endif()
set (CMAKE_CXX_FLAGS_RELEASE "-O2 -g")

set (CLOCKWORK_DIR ${PROJECT_SOURCE_DIR}/src/)
set (BUILD_DIR ${PROJECT_SOURCE_DIR}/build)

configure_file (
  "${PROJECT_SOURCE_DIR}/LatprocConfig.h.in"
  "${PROJECT_BINARY_DIR}/LatprocConfig.h"
)

set (CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake/Modules/")
# Prefer context when present (full iod process/scheduler). Not required for
# cw_client / humid: json path evaluation no longer uses boost::context::fiber.
FIND_PACKAGE(Boost COMPONENTS thread program_options filesystem chrono date_time context)
if (NOT Boost_FOUND)
  FIND_PACKAGE(Boost COMPONENTS thread program_options filesystem chrono date_time REQUIRED)
endif()
FIND_PACKAGE(Mosquitto)
FIND_PACKAGE(MODBUS)
FIND_PACKAGE(LibXml2)
# CMake 3.5 FindLibXml2 caches LIBXML2_LIBRARIES; 3.12+ caches LIBXML2_LIBRARY
# and sets LIBRARIES as a non-cache alias. Normalize so iod-elc can use the
# same variables on Linux panels and macOS.
if(LIBXML2_FOUND)
  if(NOT LIBXML2_LIBRARIES AND LIBXML2_LIBRARY)
    set(LIBXML2_LIBRARIES ${LIBXML2_LIBRARY})
  endif()
  if(NOT LIBXML2_INCLUDE_DIRS AND LIBXML2_INCLUDE_DIR)
    set(LIBXML2_INCLUDE_DIRS ${LIBXML2_INCLUDE_DIR})
  endif()
endif()
FIND_PACKAGE(ZeroMQ REQUIRED)
FIND_PACKAGE(OpenSSL REQUIRED)

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
    ${CLOCKWORK_DIR}/urlencode.h
	${CLOCKWORK_DIR}/symboltable.h
	${CLOCKWORK_DIR}/rate.h
	${CLOCKWORK_DIR}/watchdog.h
	${CLOCKWORK_DIR}/Message.h
	${CLOCKWORK_DIR}/Parameter.h
	${CLOCKWORK_DIR}/Receiver.h
    ${CLOCKWORK_DIR}/json_expr_parser.h
    ${CLOCKWORK_DIR}/json_expression.h
    ${CLOCKWORK_DIR}/ThreadSafeQueue.h
    ${CLOCKWORK_DIR}/ThreadSafeList.h
    )

# process.cpp uses boost::context::fiber (scheduler helpers) — keep it out of
# cw_client so humid panels on Bionic Boost 1.65 can build the client.
add_library (cw_client
	${CLOCKWORK_DIR}/cw_client_api.cpp
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
    ${CLOCKWORK_DIR}/urlencode.cpp
	${CLOCKWORK_DIR}/symboltable.cpp
	${CLOCKWORK_DIR}/rate.cpp
	${CLOCKWORK_DIR}/Message.cpp
	${CLOCKWORK_DIR}/Parameter.cpp
	${CLOCKWORK_DIR}/watchdog.cpp
    ${CLOCKWORK_DIR}/json_expr_parser.cpp
    ${CLOCKWORK_DIR}/json_expression.cpp
	${HEADER_FILES}
)
target_include_directories(cw_client PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(cw_client PUBLIC ${ZeroMQ_LIBRARY})

if (EXISTS "/usr/local/include")
include_directories("/usr/local/include")
endif()
if (EXISTS "/opt/local/include")
include_directories("/opt/local/include")
endif()
include_directories("${PROJECT_SOURCE_DIR}/include")
include_directories("${PROJECT_BINARY_DIR}")
include_directories("${CLOCKWORK_DIR}")

if (EXISTS "/usr/local/lib")
LINK_DIRECTORIES("/usr/local/lib")
endif()
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
    src/buffering.h
    src/CallMethodAction.h
    src/Channel.h
    src/cJSON.h
    src/ClearListAction.h
    src/ClientInterface.h
    src/clockwork.h
    src/cmdline.h
    src/CommandManager.h
    src/ConditionHandler.h
    src/Configuration.h
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
    src/EtherCATSetup.h
    src/exec_command.h
    src/exec_digest.h
    src/exec_web_request.h
    src/ExecuteMessageAction.h
    src/ExportState.h
    src/Expression.h
    src/ExpressionPcode.h
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
    src/PatternAction.h
    src/PersistentStore.h
    src/Plugin.h
    src/PopListAction.h
    src/PredicateAction.h
    src/process_data.h
    src/process.h
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
    src/symboltable.h
    src/SyncRemoteStatesAction.h
    src/Transition.h
    src/Trigger.h
    src/UnlockAction.h
	src/UpdateListItemsAction.cpp
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
    src/CallMethodAction.cpp
    src/Channel.cpp
    src/ClearListAction.cpp
    src/ClientInterface.cpp
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
    src/exec_command.c
    src/exec_digest.c
    src/exec_web_request.c
    src/ExecuteMessageAction.cpp
    src/ExportState.cpp
    src/Expression.cpp
    src/ExpressionPcode.cpp
    src/ExpressionAction.cpp
    src/filtering.cpp
    src/FireTriggerAction.cpp
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
    src/PatternAction.cpp
    src/PersistentStore.cpp
    src/Plugin.cpp
    src/PopListAction.cpp
    src/PredicateAction.cpp
    src/process_data.cpp
    src/process.cpp
    src/ProcessingThread.cpp
    src/StallTrace.cpp
    src/RateEstimatorInstance.cpp
    src/RecordApply.cpp
    src/RecordClass.cpp
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
    src/SyncRemoteStatesAction.cpp
    src/Transition.cpp
    src/Trigger.cpp
    src/TryAction.cpp
    src/UnlockAction.cpp
    src/UpdateListItemsAction.cpp
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
target_link_libraries(cw_interpreter PUBLIC OpenSSL::Crypto)

add_executable(cw src/cw.cpp) # ${Clockwork_SRCS})
set_target_properties (cw PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
#target_link_libraries(cw cw_client "dl" ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES})
target_link_libraries(cw cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} dl pthread)

add_executable(cw-scaffold src/cw_scaffold.cpp)
set_target_properties(cw-scaffold PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR")
target_link_libraries(cw-scaffold cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} dl pthread)

enable_testing()
set(_record_parse ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cw_parse.cmake)
set(_record_fix ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/record)
add_test(NAME record_parse_customer COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/customer.cw -DEXPECT_RC=0
  -P ${_record_parse})
add_test(NAME record_parse_view COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/customer_with_address_view.cw -DEXPECT_RC=0
  -P ${_record_parse})
add_test(NAME record_parse_table COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/customer_table.cw -DEXPECT_RC=0
  -P ${_record_parse})
add_test(NAME record_parse_reject_when COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/reject_when.cw -DEXPECT_RC=2
  -P ${_record_parse})
add_test(NAME record_parse_reject_command COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/reject_command.cw -DEXPECT_RC=2
  -P ${_record_parse})
add_test(NAME record_parse_machine_key COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/machine_key.cw -DEXPECT_RC=2
  -DMUST_CONTAIN=RECORD
  -P ${_record_parse})
add_test(NAME record_parse_no_key COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/no_key.cw -DEXPECT_RC=2
  -DMUST_CONTAIN=KEY
  -P ${_record_parse})
add_test(NAME record_parse_copy_from_class COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/record_list.cw -DEXPECT_RC=0
  -P ${_record_parse})
add_test(NAME record_parse_copy_from_view COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/customer_view_list.cw -DEXPECT_RC=0
  -P ${_record_parse})
add_test(NAME record_parse_copy_where COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/record_list_where.cw -DEXPECT_RC=0
  -P ${_record_parse})
add_test(NAME record_parse_query_into COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/query_into.cw -DEXPECT_RC=0
  -P ${_record_parse})
add_test(NAME record_parse_query_into_json COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/query_into_json.cw -DEXPECT_RC=0
  -P ${_record_parse})
add_test(NAME record_parse_private COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/record_private.cw -DEXPECT_RC=0
  -P ${_record_parse})
add_test(NAME record_parse_machine_private COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/machine_private.cw -DEXPECT_RC=2
  -DMUST_CONTAIN=RECORD
  -P ${_record_parse})
add_test(NAME record_parse_as_list COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/as_list.cw -DEXPECT_RC=0
  -P ${_record_parse})
add_test(NAME record_parse_machine_table COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/machine_table.cw -DEXPECT_RC=0
  -P ${_record_parse})
add_test(NAME record_parse_export_valid COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/export_valid.cw -DEXPECT_RC=0
  -P ${_record_parse})
add_test(NAME record_parse_export_unknown_option COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/export_unknown_option.cw -DEXPECT_RC=2
  -DMUST_CONTAIN=EXPORT
  -P ${_record_parse})
add_test(NAME record_parse_export_local_option COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_record_fix}/export_local_option.cw -DEXPECT_RC=0
  -P ${_record_parse})

# Runtime tests for the exception machinery (ABORT/RETURN/THROW/CATCH). These
# programs do not SHUTDOWN, so pass/fail is checked via log markers under a
# bounded run (see tests/run_cw_runtime.sh, which SIGTERMs cw so its log flushes).
set(_runtime ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cw_runtime.sh)
set(_runtime_tests ${CMAKE_CURRENT_SOURCE_DIR}/../tests)
add_test(NAME runtime_abort COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/abort.cw
  --must-not-contain "check should have aborted but continued")
add_test(NAME runtime_exceptions COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/exceptions.cw
  --must-contain "Overrun detected"
  --must-not-contain "should not be displayed")

# Part B: WAITFOR / CALL ON TIMEOUT ABORT|RETURN|THROW <msg> with TIMEOUT <ms>.
add_test(NAME parse_waitfor_timeout COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_runtime_tests}/waitfor_timeout.cw -DEXPECT_RC=0
  -P ${_record_parse})
add_test(NAME runtime_waitfor_timeout COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/waitfor_timeout_runtime.cw
  --must-contain "waitfor timed out")

# Handler-level deadline: <handler> { } WITH TIMEOUT <ms> [ON TIMEOUT { }] / AFTER <ms> { }.
add_test(NAME parse_handler_timeout COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_runtime_tests}/handler_timeout.cw -DEXPECT_RC=0
  -P ${_record_parse})
add_test(NAME runtime_handler_timeout COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/handler_timeout_runtime.cw
  --must-contain "init timed out")
# TIMEOUT is a read-only contextual value (ms) inside ON TIMEOUT.
add_test(NAME runtime_timeout_value COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/timeout_value_runtime.cw
  --must-contain "timed out after 500ms")
# TIMEOUT reflects an inner statement's deadline when a handler catches it.
add_test(NAME runtime_handler_timeout_value COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/handler_timeout_value_runtime.cw
  --must-contain "run timed out after 2000ms")
# Nested deadlines: outer cancels a blocking inner recovery.
add_test(NAME runtime_nested_timeout COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/nested_timeout_runtime.cw
  --must-contain "outer timeout"
  --must-not-contain "inner recovery")
# Late-reply correlation: a timed-out CALL's late _done reply must not complete
# a later CALL to the same command (the second CALL waits for its own reply).
add_test(NAME runtime_call_reply_correlation COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/late_reply_correlation.cw
  --must-contain "second call done completions=2"
  --must-not-contain "second call done completions=1")

# Part B/4: TRY { } WHEN TIMER >= timeout { } — parse + runtime.
add_test(NAME parse_try_catch COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_runtime_tests}/try_catch.cw -DEXPECT_RC=0
  -P ${_record_parse})

# Blocking body + timeout -> THROW (caught by CATCH).
add_test(NAME runtime_try_throw COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/try_catch.cw
  --must-contain "try timed out")
# Blocking body + timeout -> RETURN (statement after TRY runs).
add_test(NAME runtime_try_return COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/try_return.cw
  --must-contain "returned normally")
# Blocking body + timeout -> ABORT (statement after TRY must NOT run).
add_test(NAME runtime_try_abort COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/try_abort.cw
  --must-not-contain "should not run")
# Body completes before timeout (handler must NOT run).
add_test(NAME runtime_try_body_completes COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/try_body_completes.cw
  --must-contain "body done"
  --must-not-contain "timed out")
# Synchronous body (handler must NOT run).
add_test(NAME runtime_try_sync COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/try_sync.cw
  --must-contain "body ran"
  --must-not-contain "timed out")
# Timeout predicate already true (fires immediately).
add_test(NAME runtime_try_immediate COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/try_immediate.cw
  --must-contain "immediate timeout")
# Empty handler: TRY completes on timeout, statement after TRY runs.
add_test(NAME runtime_try_empty_handler COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/try_empty_handler.cw
  --must-contain "completed via empty handler")
# ON ERROR { }: body failure runs ON ERROR (not ON TIMEOUT).
add_test(NAME runtime_try_on_error COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/try_on_error.cw
  --must-contain "error handled"
  --must-not-contain "should not timeout")
# ON ERROR does not run on a timeout (distinct outcomes).
add_test(NAME runtime_try_on_error_timeout COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/try_on_error_timeout.cw
  --must-contain "timed out"
  --must-not-contain "should not error")
# ABORT in ON TIMEOUT cascades to ON ERROR.
add_test(NAME runtime_try_on_error_cascade COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/try_on_error_cascade.cw
  --must-contain "abort cascaded to error")
# Handler-level ON ERROR (no WITH TIMEOUT) catches a body failure.
add_test(NAME runtime_handler_on_error COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/handler_on_error.cw
  --must-contain "handler error caught")
# WAIT ... WITH TIMEOUT (statement form).
add_test(NAME runtime_wait_timeout COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/wait_timeout_runtime.cw
  --must-contain "wait timed out")
# RECEIVE handler runtime deadline.
add_test(NAME runtime_receive_timeout COMMAND sh ${_runtime}
  $<TARGET_FILE:cw> ${_runtime_tests}/receive_timeout_runtime.cw
  --must-contain "receive timed out")
# SET ... WITH TIMEOUT (parse-only; a synchronous SET has no observable deadline).
add_test(NAME parse_set_timeout COMMAND ${CMAKE_COMMAND}
  -DCW=$<TARGET_FILE:cw> -DFILE=${_runtime_tests}/set_timeout.cw -DEXPECT_RC=0
  -P ${_record_parse})

add_executable(test_record_apply tests/test_record_apply.cpp src/DbNotify.cpp)
set_target_properties(test_record_apply PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR")
target_include_directories(test_record_apply PRIVATE src tests)
target_link_libraries(test_record_apply cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} dl pthread)
add_test(NAME test_record_apply COMMAND test_record_apply)
add_executable(test_copy_from_record tests/test_copy_from_record.cpp)
set_target_properties(test_copy_from_record PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR")
target_include_directories(test_copy_from_record PRIVATE src tests)
target_link_libraries(test_copy_from_record cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} dl pthread)
add_test(NAME test_copy_from_record COMMAND test_copy_from_record)
add_executable(test_abort_action tests/test_abort_action.cpp)
set_target_properties(test_abort_action PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR")
target_include_directories(test_abort_action PRIVATE src tests)
target_link_libraries(test_abort_action cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} dl pthread)
add_test(NAME test_abort_action COMMAND test_abort_action)
add_executable(test_deadline_race tests/test_deadline_race.cpp)
set_target_properties(test_deadline_race PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR")
target_include_directories(test_deadline_race PRIVATE src tests)
target_link_libraries(test_deadline_race cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} dl pthread)
add_test(NAME test_deadline_race COMMAND test_deadline_race)
add_executable(test_deadline_req tests/test_deadline_req.cpp src/DeadlineReq.cpp)
set_target_properties(test_deadline_req PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR")
target_include_directories(test_deadline_req PRIVATE src)
target_link_libraries(test_deadline_req cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} pthread)
add_test(NAME test_deadline_req COMMAND test_deadline_req)
add_executable(test_db_notify tests/test_db_notify.cpp src/DbNotify.cpp)
set_target_properties(test_db_notify PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR")
target_include_directories(test_db_notify PRIVATE src)
target_link_libraries(test_db_notify cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} pthread)
add_test(NAME test_db_notify COMMAND test_db_notify)
add_executable(test_two_process_apply tests/test_two_process_apply.cpp src/DbNotify.cpp)
set_target_properties(test_two_process_apply PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR")
target_include_directories(test_two_process_apply PRIVATE src tests)
target_link_libraries(test_two_process_apply cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} dl pthread)
add_test(NAME test_two_process_apply COMMAND test_two_process_apply)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../datastore/build-wal/dbsvr")
  set_tests_properties(test_two_process_apply PROPERTIES
    ENVIRONMENT "DBSVR=${CMAKE_CURRENT_SOURCE_DIR}/../../datastore/build-wal/dbsvr")
endif()
add_executable(test_two_dbd tests/test_two_dbd.cpp)
set_target_properties(test_two_dbd PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR")
target_include_directories(test_two_dbd PRIVATE src)
target_link_libraries(test_two_dbd ${ZeroMQ_LIBRARY} pthread)
add_test(NAME test_two_dbd COMMAND test_two_dbd)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../datastore/build-wal/dbsvr")
  set_tests_properties(test_two_dbd PROPERTIES
    ENVIRONMENT "DBD=$<TARGET_FILE:dbd>;DBSVR=${CMAKE_CURRENT_SOURCE_DIR}/../../datastore/build-wal/dbsvr"
    TIMEOUT 30)
else()
  set_tests_properties(test_two_dbd PROPERTIES
    ENVIRONMENT "DBD=$<TARGET_FILE:dbd>"
    TIMEOUT 20)
endif()
add_executable(test_cw_system tests/test_cw_system.cpp)
set_target_properties(test_cw_system PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR")
target_include_directories(test_cw_system PRIVATE src)
target_link_libraries(test_cw_system cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} pthread)
add_test(NAME test_cw_system COMMAND test_cw_system)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../datastore/build-wal/dbsvr")
  set_tests_properties(test_cw_system PROPERTIES
    ENVIRONMENT "CW=$<TARGET_FILE:cw>;DBD=$<TARGET_FILE:dbd>;DBSVR=${CMAKE_CURRENT_SOURCE_DIR}/../../datastore/build-wal/dbsvr"
    TIMEOUT 45)
else()
  set_tests_properties(test_cw_system PROPERTIES
    ENVIRONMENT "CW=$<TARGET_FILE:cw>;DBD=$<TARGET_FILE:dbd>"
    TIMEOUT 20)
endif()
add_test(NAME record_scaffold_customer COMMAND ${CMAKE_COMMAND}
  -DCW_SCAFFOLD=$<TARGET_FILE:cw-scaffold>
  -DCW=$<TARGET_FILE:cw>
  -DFROM=${_record_fix}/customer.cw
  -DGOLDEN=${_record_fix}/expected_CustomerINTERFACE.lpc
  -DGOLDEN_SQL=${_record_fix}/expected_Customer.sql
  -DOUT_NAME=CustomerINTERFACE.lpc
  -DSQL_NAME=Customer.sql
  -DCHANNEL=${CMAKE_CURRENT_SOURCE_DIR}/../tests/db-channel.cw
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cw_scaffold.cmake)
add_test(NAME record_scaffold_view COMMAND ${CMAKE_COMMAND}
  -DCW_SCAFFOLD=$<TARGET_FILE:cw-scaffold>
  -DCW=$<TARGET_FILE:cw>
  -DFROM=${_record_fix}/customer_with_address_view.cw
  -DGOLDEN=${_record_fix}/expected_CustomerWithAddressINTERFACE.lpc
  -DGOLDEN_SQL=${_record_fix}/expected_CustomerWithAddress.sql
  -DOUT_NAME=CustomerWithAddressINTERFACE.lpc
  -DSQL_NAME=CustomerWithAddress.sql
  -DCHANNEL=${CMAKE_CURRENT_SOURCE_DIR}/../tests/db-channel.cw
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cw_scaffold.cmake)

if (USE_ETHERCAT)
# Experimental cw with SOEM EtherCAT library support
if(USE_SOEM)
add_executable(cw_ecat src/cw.cpp) # ${Clockwork_SRCS})
set_target_properties (cw_ecat PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR;SOEM_ETHERCAT" )
target_link_directories_compat(cw_ecat "${CW_ETHERCAT_DIR}")
target_link_libraries(cw_ecat cw_interpreter cw_client dl ${CW_ETHERCAT_LIB} ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} "pthread")
endif()

# Kernel transport controller (only plant iod on this branch).
if(BUILD_IOD_ELC)
  # Prefer make install-lib → /usr/local; fall back to /opt/elc.
  list(APPEND CMAKE_PREFIX_PATH "/usr/local" "/opt/elc")
  if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
    set(ENV{PKG_CONFIG_PATH} "/usr/local/lib/pkgconfig:/opt/elc/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
  else()
    set(ENV{PKG_CONFIG_PATH} "/usr/local/lib/pkgconfig:/opt/elc/lib/pkgconfig")
  endif()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(ELC elcethercat)
  if(NOT ELC_FOUND)
    message(WARNING
      "elcethercat not found; skipping iod-elc. "
      "Install libelcethercat (pkg-config elcethercat) to build the plant controller. "
      "cw and the other RELEASE_TARGETS still build.")
  elseif(NOT LIBXML2_FOUND)
    message(WARNING
      "LibXml2 not found; skipping iod-elc. "
      "Install libxml2 (headers + library) to build the plant controller. "
      "cw and the other RELEASE_TARGETS still build.")
  endif()
endif()
if(BUILD_IOD_ELC AND ELC_FOUND AND LIBXML2_FOUND)
  include_directories(${ELC_INCLUDE_DIRS})

  add_executable(iod-elc
    src/iod.cpp
    src/ecat_thread.cpp
    src/EtherCATSetup.cpp
    src/EtherCATEntrySelector.cpp
    src/ethercat_xml_parser.cpp
    src/KernelEthercatBus.cpp
    src/ElcConfigFile.cpp
    src/ElcSetupRecipe.cpp
    ${Clockwork_SRCS}
  )
  target_include_directories(iod-elc PRIVATE ${LIBXML2_INCLUDE_DIRS})
  # Plant iod: kernel elc transport only.
  target_compile_definitions(iod-elc PRIVATE
    USE_ETHERCAT=1
    KEEP_STATS
    USE_DC
    USE_KERNEL_ETHERCAT=1
    USE_SDO=1
  )
  foreach(_elc_libdir ${ELC_LIBRARY_DIRS} /usr/local/lib /opt/elc/lib)
    if(_elc_libdir)
      target_link_directories_compat(iod-elc "${_elc_libdir}")
    endif()
  endforeach()
  set(_iod_elc_extra_libs)
  if(NOT APPLE)
    list(APPEND _iod_elc_extra_libs rt)
  endif()
  target_link_libraries(iod-elc
    cw_client
    ${ELC_LIBRARIES}
    dl ${ZeroMQ_LIBRARY} ${LIBXML2_LIBRARIES} ${Boost_LIBRARIES} "mosquitto" "pthread" ${_iod_elc_extra_libs} curl OpenSSL::Crypto
  )
  install(TARGETS iod-elc RUNTIME DESTINATION ${PROJECT_SOURCE_DIR})
  message(STATUS "Building iod-elc with libelcethercat (${ELC_VERSION}) include=${ELC_INCLUDE_DIRS}")
elseif(NOT BUILD_IOD_ELC)
  message(WARNING "BUILD_IOD_ELC is OFF; no plant iod binary will be built on this branch")
endif()

endif() # USE_ETHERCAT

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

# Build the generated cmdline parser once and link it into both tools.
add_library(cmdline_parser STATIC ${iosh_yacc_output} ${iosh_lex_output})
set(USE_READLINE on)
if (USE_READLINE)
target_compile_definitions(cmdline_parser PRIVATE USE_READLINE )
target_link_libraries(cmdline_parser "readline")
endif()

add_executable(iosh src/iosh.cpp)
# macOS Homebrew readline/zmq path; use compat helper so CMake 3.10 panels still configure.
if (EXISTS "/opt/homebrew/lib")
  target_link_directories_compat(iosh "/opt/homebrew/lib")
endif()
target_link_libraries(iosh cmdline_parser cw_client "readline" ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} "pthread")
target_compile_definitions(iosh PRIVATE USE_READLINE )

add_executable(iocmd src/iocmd.cpp)
target_link_libraries(iocmd cmdline_parser cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} "pthread")

add_executable(device_connector src/device_connector.cpp src/options.cpp )
target_link_libraries(device_connector cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} "pthread")

add_executable(persistd src/persistd.cpp src/PersistentStore.cpp )
target_link_libraries(persistd cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} "pthread")

# modbusd needs libmodbus. dbd talks ZMQ only.
set(CLOCKWORK_RUNTIME_TARGETS cw cw-scaffold iosh iocmd device_connector persistd)
add_executable(dbd src/dbd.cpp src/DeadlineReq.cpp src/DbNotify.cpp)
target_link_libraries(dbd cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} "pthread")
list(APPEND CLOCKWORK_RUNTIME_TARGETS dbd)
add_dependencies(test_two_dbd dbd)
add_dependencies(test_cw_system cw dbd)
if (MODBUS_FOUND)
  add_executable(modbusd src/modbusd.cpp )
  target_link_libraries(modbusd cw_client ${MODBUS_LIBRARIES} ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} "pthread")
  list(APPEND CLOCKWORK_RUNTIME_TARGETS modbusd)
else()
  message(STATUS "libmodbus not found: skipping modbusd (cw_client and dbd still build)")
endif()

if (RUN_TESTS)
enable_testing()
add_subdirectory(tests)
endif()

install(TARGETS ${CLOCKWORK_RUNTIME_TARGETS} RUNTIME DESTINATION ${PROJECT_SOURCE_DIR})
if(USE_SOEM)
	install(TARGETS cw_ecat RUNTIME DESTINATION ${PROJECT_SOURCE_DIR})
endif()

install(TARGETS cw_client EXPORT cw_clientTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT libs)
install(DIRECTORY include/cw_client DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT cw_clientTargets NAMESPACE cw_client::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cw_client)
configure_package_config_file(cmake/cw_clientConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/cw_clientConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cw_client)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/cw_clientConfigVersion.cmake
  VERSION ${Latproc_VERSION_MAJOR}.${Latproc_VERSION_MINOR} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cw_clientConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cw_clientConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cw_client)
install(TARGETS cw_interpreter ARCHIVE DESTINATION ${PROJECT_SOURCE_DIR}/stage/lib COMPONENT libs)

add_custom_target(libs
  DEPENDS cw_client cw_interpreter
  COMMENT "Build only the Clockwork libraries (cw_client, cw_interpreter)"
)

if (CMAKE_VERSION VERSION_LESS "3.15")
    add_custom_target(install_libs
      COMMAND "${CMAKE_COMMAND}" -DCMAKE_INSTALL_CONFIG_NAME=$<CONFIG> -DCOMPONENT=libs
              -P "${CMAKE_BINARY_DIR}/cmake_install.cmake"
      DEPENDS cw_client cw_interpreter
      COMMENT "Install only the Clockwork libraries (component: libs)"
    )
    
    add_custom_target(install_client
      COMMAND "${CMAKE_COMMAND}" -DCMAKE_INSTALL_CONFIG_NAME=$<CONFIG> -DCOMPONENT=clientlib
              -P "${CMAKE_BINARY_DIR}/cmake_install.cmake"
      DEPENDS cw_client
      COMMENT "Install only the cw_client library (component: clientlib)"
    )
else()
    add_custom_target(install_libs
      COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --component libs
      DEPENDS cw_client cw_interpreter
      COMMENT "Install only the Clockwork libraries (component: libs)"
    )
    
    add_custom_target(install_client
    	COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --component clientlib
    	DEPENDS cw_client
    	COMMENT "Install only the cw_client library (component: clientlib)"
    )
endif()
#install(DIRECTORY src/ DESTINATION include/clockwork FILES_MATCHING PATTERN "*.h")
