cmake_minimum_required(VERSION 3.0)

set(exe_name "iod")


##################################
#        Extra definitions       #
##################################


add_definitions(-std=c++11)


##################################
#       Gather Dependencies      #
##################################


unset(LIBRARY)
unset(INCLUDE)

set(LIBRARY
    clockwork_client
    iod_interpreter
    ec_tool
    "ethercat"
    "dl"
    xml2
    mosquitto
    pthread
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

# add_library(${exe_name}
#     STATIC
#     ${source_files}
#     ${header_files}
# )
add_executable(${exe_name} ${source_files} ${header_files})
# add_test(${exe_name} ${exe_name})
set_target_properties(${exe_name} PROPERTIES COMPILE_DEFINITIONS "USE_ETHERCAT")
# set_target_properties(${exe_name} PROPERTIES COMPILE_DEFINITIONS "USE_ETHERCAT=1")
install(TARGETS ${exe_name} RUNTIME DESTINATION ${PROJECT_SOURCE_DIR})


target_link_libraries(${exe_name}
    ${LIBRARY}
)
target_include_directories(${exe_name}
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${INCLUDE}
)
