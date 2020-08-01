cmake_minimum_required(VERSION 3.0)

set(exe_name "test_bit_ops")


##################################
#        Extra definitions       #
##################################


add_definitions(-std=c++11)
add_definitions(-DEC_SIMULATOR=1)

##################################
#      generate source files     #
##################################

##################################
#       Gather Dependencies      #
##################################


unset(LIBRARY)
unset(INCLUDE)

set(LIBRARY
    clockwork_client
    clockwork_interpreter
		Mosquitto-Dep
    gtest
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

add_executable(${exe_name} ${source_files} ${header_files})
add_test(${exe_name} ${exe_name})

install(TARGETS ${exe_name} RUNTIME DESTINATION ${PROJECT_SOURCE_DIR}/tests/bin)


target_link_libraries(${exe_name}
    ${LIBRARY}
)
target_include_directories(${exe_name}
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${INCLUDE}
)
