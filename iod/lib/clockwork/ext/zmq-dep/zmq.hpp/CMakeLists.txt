
set(exe_name "ZMQ_HPP-Dep")


##################################
#       Gather Dependencies      #
##################################

get_property(_ZMQ-Dep_INCLUDE_DIRS GLOBAL PROPERTY _ZMQ-Dep_INCLUDE_DIRS)

set(LIBRARY
    _ZMQ-Dep
)
set(INCLUDE
    ${_ZMQ-Dep_INCLUDE_DIRS}
)


##################################
#          Build Binary          #
##################################

FILE(GLOB_RECURSE source_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*
)
FILE(GLOB_RECURSE header_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h*
)

add_library(${exe_name}
    STATIC
    ${source_files}
    ${header_files}
)
target_link_libraries(${exe_name}
    ${LIBRARY}
)
target_include_directories(${exe_name}
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${INCLUDE}
)
