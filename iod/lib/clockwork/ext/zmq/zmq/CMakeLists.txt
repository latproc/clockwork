
set(exe_name "ZMQ-Dep")


#################################
##         find  ZMQ           ##
#################################

set(INCLUDE "")
set(LIBRARY "")
if (MINGW_BUILD)
    set(LIBRARY
        zmq
    )
else()
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")
    FIND_PACKAGE(Sodium REQUIRED)
    if(SODIUM_FOUND)
        add_definitions(-DHAVE_LIBSODIUM)
    else()
        message(FATAL_ERROR "no sodium?")
    endif()
    FIND_PACKAGE(ZMQ 4.2 REQUIRED)
    if (ZMQ_FOUND)
        set(LIBRARY ${ZMQ_LIBRARIES})
        set(INCLUDE ${ZMQ_INCLUDE_DIRS})
    else (ZMQ_FOUND)
        message(FATAL_ERROR "## Not Found - ZMQ")
    endif (ZMQ_FOUND)
endif ()


#################################
##      make the library       ##
#################################

FILE(GLOB_RECURSE source_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*
)
FILE(GLOB_RECURSE header_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h*
)

add_library(${exe_name} ${source_files} ${header_files})

target_link_libraries(${exe_name}
    ${LIBRARY}
)

#################################
##    sort out include files   ##
#################################
set(${exe_name}_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${INCLUDE}
)

set_property(GLOBAL PROPERTY ${exe_name}_INCLUDE_DIRS
    ${${exe_name}_INCLUDE_DIRS}
)
include_directories(
    ${${exe_name}_INCLUDE_DIRS}
)
