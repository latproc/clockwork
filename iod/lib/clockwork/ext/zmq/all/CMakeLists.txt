
set(exe_name "ZMQ-Dummy")


get_property(ZMQ_HPP-Dep_INCLUDE_DIRS GLOBAL PROPERTY ZMQ_HPP-Dep_INCLUDE_DIRS)
get_property(ZMQ-Dep_INCLUDE_DIRS GLOBAL PROPERTY ZMQ-Dep_INCLUDE_DIRS)

set(INCLUDE
    ${ZMQ_HPP-Dep_INCLUDE_DIRS}
    ${ZMQ-Dep_INCLUDE_DIRS}
)
set(LIBRARY
    #cZMQ-Dep
    ZMQ_HPP-Dep
    ZMQ-Dep
)

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
