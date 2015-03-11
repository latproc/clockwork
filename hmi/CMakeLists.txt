cmake_minimum_required (VERSION 2.6)
project (HMI)

set (HMI_VERSION_MAJOR 0)
set (HMI_VERSION_MINOR 8)

set (CMAKE_CXX_FLAGS "-g -Wall --std=gnu++0x")

set (CLOCKWORK_DIR ${PROJECT_SOURCE_DIR}/../iod/)

configure_file (
  "${PROJECT_SOURCE_DIR}/HMIConfig.h.in"
  "${PROJECT_BINARY_DIR}/HMIConfig.h"
)

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
    ${CLOCKWORK_DIR}/options.cpp
    ${CLOCKWORK_DIR}/Message.cpp )


FIND_PACKAGE(Boost COMPONENTS system thread REQUIRED )
FIND_PACKAGE(FLTK REQUIRED)
FIND_PACKAGE(OpenGL REQUIRED)
FIND_PACKAGE(MODBUS REQUIRED)

include_directories("/usr/local/include")
include_directories("/usr/local/include/modbus")
include_directories("/opt/local/include")
include_directories("${PROJECT_SOURCE_DIR}/")
include_directories("${PROJECT_BINARY_DIR}")
include_directories("${CLOCKWORK_DIR}")


LINK_DIRECTORIES("/usr/local/lib")
LINK_DIRECTORIES("/opt/local/lib")

add_executable (modbus_sample src/modbus_sample.cxx)
target_link_libraries(modbus_sample ${Boost_LIBRARIES})
TARGET_LINK_LIBRARIES(modbus_sample ${FLTK_LIBRARIES})
TARGET_LINK_LIBRARIES(modbus_sample ${OPENGL_LIBRARIES})
TARGET_LINK_LIBRARIES(modbus_sample ${MODBUS_LIBRARIES})
TARGET_LINK_LIBRARIES(modbus_sample ${CMAKE_DL_LIBS})

add_executable (grab_machine src/grab_machine.cxx src/grab_console.cxx)
target_link_libraries(grab_machine Clockwork "zmq" ${Boost_LIBRARIES})
TARGET_LINK_LIBRARIES(grab_machine ${FLTK_LIBRARIES})
TARGET_LINK_LIBRARIES(grab_machine ${OPENGL_LIBRARIES})
TARGET_LINK_LIBRARIES(grab_machine ${MODBUS_LIBRARIES})
TARGET_LINK_LIBRARIES(grab_machine ${CMAKE_DL_LIBS})
