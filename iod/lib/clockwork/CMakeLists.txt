cmake_minimum_required(VERSION 2.80)
add_subdirectory(ext)

set(exe_name "Clockwork")
SET(BUILD_SHARED_LIBS ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")

#################################
##        find library         ##
#################################

get_property(ZMQ-Dep_INCLUDE_DIRS GLOBAL PROPERTY ZMQ-Dep_INCLUDE_DIRS)
get_property(Boost-Dep_INCLUDE_DIRS GLOBAL PROPERTY Boost-Dep_INCLUDE_DIRS)
get_property(Curl-Dep_INCLUDE_DIRS GLOBAL PROPERTY Curl-Dep_INCLUDE_DIRS)

set(INCLUDE
    ${ZMQ-Dep_INCLUDE_DIRS}
    ${Boost-Dep_INCLUDE_DIRS}
    ${Curl-Dep_INCLUDE_DIRS}
)
set(LIBRARY
    ZMQ-Dep
    Boost-Dep
    Curl-Dep
)


if (MINGW_BUILD)
    list(APPEND LIBRARY
        ws2_32
    )

else ()

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
set (CMAKE_CXX_FLAGS "-std=c++11 ")


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
