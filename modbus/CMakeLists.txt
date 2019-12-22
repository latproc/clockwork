cmake_minimum_required (VERSION 2.6)
project (MBM)

set (MBM_VERSION_MAJOR 0)
set (MBM_VERSION_MINOR 1)

if (EXISTS "${PROJECT_SOURCE_DIR}/LocalCMakeLists.txt")
		    include("${PROJECT_SOURCE_DIR}/LocalCMakeLists.txt")
endif()
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake_modules/")

set (CMAKE_CXX_FLAGS "-g -Wall --std=gnu++0x -Wno-deprecated-declarations")
set (CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})

set (CLOCKWORK_DIR ${PROJECT_SOURCE_DIR}/../iod/src)

configure_file (
  "${PROJECT_SOURCE_DIR}/src/MBMConfig.h.in"
  "${PROJECT_BINARY_DIR}/MBMConfig.h"
)


set(HEADER_FILES
	${CLOCKWORK_DIR}/MessagingInterface.h
	${CLOCKWORK_DIR}/SocketMonitor.h
	${CLOCKWORK_DIR}/ConnectionManager.h
	${CLOCKWORK_DIR}/MessageLog.h
	${CLOCKWORK_DIR}/MessageEncoding.h
	${CLOCKWORK_DIR}/MessageHeader.h
	${CLOCKWORK_DIR}/regular_expressions.h
	${CLOCKWORK_DIR}/Logger.h
	${CLOCKWORK_DIR}/anet.h
	${CLOCKWORK_DIR}/cJSON.h
	${CLOCKWORK_DIR}/DebugExtra.h
	${CLOCKWORK_DIR}/value.h
	${CLOCKWORK_DIR}/symboltable.h
	${CLOCKWORK_DIR}/rate.h
	${CLOCKWORK_DIR}/Message.h
)

add_library (Clockwork
    ${CLOCKWORK_DIR}/MessagingInterface.cpp
    ${CLOCKWORK_DIR}/MessageLog.cpp
    ${CLOCKWORK_DIR}/MessageEncoding.cpp
    ${CLOCKWORK_DIR}/MessageHeader.cpp
    ${CLOCKWORK_DIR}/regular_expressions.cpp
    ${CLOCKWORK_DIR}/Logger.cpp
    ${CLOCKWORK_DIR}/anet.c
    ${CLOCKWORK_DIR}/cJSON.c
    ${CLOCKWORK_DIR}/DebugExtra.cpp
    ${CLOCKWORK_DIR}/value.cpp
    ${CLOCKWORK_DIR}/symboltable.cpp
    ${CLOCKWORK_DIR}/options.cpp
	${CLOCKWORK_DIR}/SocketMonitor.cpp
	${CLOCKWORK_DIR}/ConnectionManager.cpp
	${CLOCKWORK_DIR}/rate.cpp
    ${CLOCKWORK_DIR}/Message.cpp )


FIND_PACKAGE(Boost COMPONENTS system thread REQUIRED )
FIND_PACKAGE(MODBUS REQUIRED)
FIND_PACKAGE(ZMQ 4.2 REQUIRED)


include_directories("/usr/local/include")
include_directories("/usr/local/include/modbus")
include_directories("/opt/local/include")
include_directories("${PROJECT_SOURCE_DIR}/")
include_directories("${PROJECT_BINARY_DIR}")
include_directories("${CLOCKWORK_DIR}")
include_directories("${ZMQ_INCLUDE_DIR}")


LINK_DIRECTORIES("/usr/local/lib")
LINK_DIRECTORIES("/opt/local/lib")

add_executable (address src/address.cpp src/plc_interface.cpp)
target_link_libraries(address ${Boost_LIBRARIES})
TARGET_LINK_LIBRARIES(address ${MODBUS_LIBRARIES})
TARGET_LINK_LIBRARIES(address ${CMAKE_DL_LIBS})


add_executable (mbmon src/mbmon.cpp src/plc_interface.cpp src/monitor.cpp)
target_link_libraries(mbmon ${Boost_LIBRARIES})
TARGET_LINK_LIBRARIES(mbmon ${ZMQ_LIBRARY})
TARGET_LINK_LIBRARIES(mbmon Clockwork)
TARGET_LINK_LIBRARIES(mbmon ${MODBUS_LIBRARIES})
TARGET_LINK_LIBRARIES(mbmon ${CMAKE_DL_LIBS})
