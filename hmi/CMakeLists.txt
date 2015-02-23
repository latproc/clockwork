cmake_minimum_required (VERSION 2.6)
project (HMI)

set (HMI_VERSION_MAJOR 0)
set (HMI_VERSION_MINOR 8)

set (CMAKE_CXX_FLAGS "-g -Wall --std=gnu++0x")

configure_file (
  "${PROJECT_SOURCE_DIR}/HMIConfig.h.in"
  "${PROJECT_BINARY_DIR}/HMIConfig.h"
)

FIND_PACKAGE(Boost COMPONENTS system thread REQUIRED )
FIND_PACKAGE(FLTK REQUIRED)
FIND_PACKAGE(OpenGL REQUIRED)
FIND_PACKAGE(MODBUS REQUIRED)

include_directories("/usr/local/include")
include_directories("/usr/local/include/modbus")
include_directories("/opt/local/include")
include_directories("${PROJECT_SOURCE_DIR}/")
include_directories("${PROJECT_BINARY_DIR}")

LINK_DIRECTORIES("/usr/local/lib")
LINK_DIRECTORIES("/opt/local/lib")

add_executable (modbus_sample src/modbus_sample.cxx)
target_link_libraries(modbus_sample ${Boost_LIBRARIES})
TARGET_LINK_LIBRARIES(modbus_sample ${FLTK_LIBRARIES})
TARGET_LINK_LIBRARIES(modbus_sample ${OPENGL_LIBRARIES})
TARGET_LINK_LIBRARIES(modbus_sample MODBUS)
