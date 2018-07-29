cmake_minimum_required (VERSION 2.6)
project (libclockwork)

set (libclockwork_VERSION_MAJOR 0)
set (libclockwork_VERSION_MINOR 9)

set (CMAKE_CXX_FLAGS "-g -Wall")

set (CLOCKWORK_DIR ${PROJECT_SOURCE_DIR}/../iod/)

configure_file (
  "${PROJECT_SOURCE_DIR}/ClockworkConfig.h.in"
  "${PROJECT_BINARY_DIR}/ClockworkConfig.h"
)

add_definitions(-DEC_SIMULATOR=1)
add_definitions(-DDEBUG)

FIND_PACKAGE(Boost COMPONENTS system program_options filesystem REQUIRED )

add_library (clockwork 
    ${PROJECT_SOURCE_DIR}/globals.cpp 
    ${CLOCKWORK_DIR}/anet.c
    ${CLOCKWORK_DIR}/cJSON.c
    ${CLOCKWORK_DIR}/CallMethodAction.cpp
    ${CLOCKWORK_DIR}/Channel.cpp
    ${CLOCKWORK_DIR}/ClearListAction.cpp
    ${CLOCKWORK_DIR}/ClientInterface.cpp
    ${CLOCKWORK_DIR}/CopyPropertiesAction.cpp
    ${CLOCKWORK_DIR}/DebugExtra.cpp
    ${CLOCKWORK_DIR}/DisableAction.cpp
    ${CLOCKWORK_DIR}/Dispatcher.cpp
    ${CLOCKWORK_DIR}/ECInterface.cpp
    ${CLOCKWORK_DIR}/EnableAction.cpp
    ${CLOCKWORK_DIR}/ExecuteMessageAction.cpp
    ${CLOCKWORK_DIR}/Expression.cpp
    ${CLOCKWORK_DIR}/ExpressionAction.cpp
    ${CLOCKWORK_DIR}/FireTriggerAction.cpp
    ${CLOCKWORK_DIR}/HandleMessageAction.cpp
    ${CLOCKWORK_DIR}/IOComponent.cpp
    ${CLOCKWORK_DIR}/IODCommands.cpp
    ${CLOCKWORK_DIR}/IfCommandAction.cpp
    ${CLOCKWORK_DIR}/IncludeAction.cpp
    ${CLOCKWORK_DIR}/LockAction.cpp
    ${CLOCKWORK_DIR}/LogAction.cpp
    ${CLOCKWORK_DIR}/Logger.cpp
    ${CLOCKWORK_DIR}/MachineCommandAction.cpp
    ${CLOCKWORK_DIR}/MachineInstance.cpp
    ${CLOCKWORK_DIR}/Message.cpp
    ${CLOCKWORK_DIR}/MessageEncoding.cpp
    ${CLOCKWORK_DIR}/MessageLog.cpp
    ${CLOCKWORK_DIR}/MessagingInterface.cpp
    ${CLOCKWORK_DIR}/ModbusInterface.cpp
    ${CLOCKWORK_DIR}/MQTTInterface.cpp
    ${CLOCKWORK_DIR}/PatternAction.cpp
    ${CLOCKWORK_DIR}/PersistentStore.cpp
    ${CLOCKWORK_DIR}/Plugin.cpp
    ${CLOCKWORK_DIR}/PopListAction.cpp
    ${CLOCKWORK_DIR}/PredicateAction.cpp
    ${CLOCKWORK_DIR}/ResumeAction.cpp
    ${CLOCKWORK_DIR}/Scheduler.cpp
    ${CLOCKWORK_DIR}/SendMessageAction.cpp
    ${CLOCKWORK_DIR}/SetListEntriesAction.cpp
    ${CLOCKWORK_DIR}/SetOperationAction.cpp
    ${CLOCKWORK_DIR}/SetStateAction.cpp
    ${CLOCKWORK_DIR}/ShutdownAction.cpp
    ${CLOCKWORK_DIR}/SortListAction.cpp
    ${CLOCKWORK_DIR}/State.cpp
    ${CLOCKWORK_DIR}/UnlockAction.cpp
    ${CLOCKWORK_DIR}/WaitAction.cpp
    ${CLOCKWORK_DIR}/dynamic_value.cpp
    ${CLOCKWORK_DIR}/filtering.cpp
    ${CLOCKWORK_DIR}/options.cpp
    ${CLOCKWORK_DIR}/regular_expressions.cpp
    ${CLOCKWORK_DIR}/symboltable.cpp
    ${CLOCKWORK_DIR}/value.cpp
    )

include_directories("/usr/local/include")
include_directories("${PROJECT_SOURCE_DIR}/")
include_directories("${PROJECT_BINARY_DIR}")
include_directories("${CLOCKWORK_DIR}")

find_library(ZeroMQ_LIBRARY NAMES zmq libzmq HINTS "/usr/local/lib/")
find_library(MQTT_LIBRARY NAMES mosquitto libmosquitto HINTS "/usr/local/lib")
set(ZeroMQ_LIBRARIES ${ZeroMQ_LIBRARY})
set(MQTT_LIBRARIES ${MQTT_LIBRARY})

add_executable (UniquePort uniqueport.cpp)
target_link_libraries(UniquePort clockwork ${ZeroMQ_LIBRARIES} ${MQTT_LIBRARIES} ${Boost_LIBRARIES})

