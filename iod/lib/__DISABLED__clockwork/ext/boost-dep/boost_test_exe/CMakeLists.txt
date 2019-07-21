cmake_minimum_required(VERSION 3.0)

set(exe_name "boost_test_exe")


##################################
#        Extra definitions       #
##################################


add_definitions(-std=c++11)


##################################
#       Gather Dependencies      #
##################################


unset(LIBRARY)
unset(INCLUDE)

# append_target_include_dirs(INCLUDE Boost-Dep)

set(LIBRARY
    Boost-Dep
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
add_test(${exe_name} ${exe_name})

target_link_libraries(${exe_name}
    ${LIBRARY}
)
target_include_directories(${exe_name}
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${INCLUDE}
)

##################################
#       sort out resources       #
##################################
