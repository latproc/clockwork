cmake_minimum_required(VERSION 3.0)

set(exe_name "iosh")


##################################
#        Extra definitions       #
##################################


add_definitions(-std=c++11)

##################################
#      generate source files     #
##################################

# set(FLEX_EXECUTABLE lex)
find_package(FLEX)
if (NOT FLEX_EXECUTABLE)
     message(FATAL_ERROR "flex was not found.")
endif()

# set(BISON_EXECUTABLE bison)
find_package(BISON)
if (NOT BISON_EXECUTABLE)
    message(FATAL_ERROR "bison not found.")
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(ios_lex_input ${CMAKE_CURRENT_SOURCE_DIR}/src_generators/cmdline.lpp)
set(ios_yac_input ${CMAKE_CURRENT_SOURCE_DIR}/src_generators/cmdline.ypp)

set(iosh_lex_output ${CMAKE_CURRENT_BINARY_DIR}/generated/cmdline.yy.cpp)
set(iosh_yacc_output ${CMAKE_CURRENT_BINARY_DIR}/generated/cmdline.tab.cpp)

# Create custom command for flex/lex (note the outputs)
ADD_CUSTOM_COMMAND(
   COMMAND ${FLEX_EXECUTABLE}
   ARGS -o ${iosh_lex_output}
		${ios_lex_input}
   OUTPUT ${iosh_lex_output})

# Create custom command for bison/yacc (note the DEPENDS)
ADD_CUSTOM_COMMAND(
   COMMAND ${BISON_EXECUTABLE}
   ARGS -y
        -o ${iosh_yacc_output}
		-g -v -d ${ios_yac_input}
   DEPENDS ${ios_lex_input}
   OUTPUT ${iosh_yacc_output}
)

# Since the parser source does not exists yet when cmake is run, mark
# it as generated
SET_SOURCE_FILES_PROPERTIES(${iosh_lex_output} GENERATED)
SET_SOURCE_FILES_PROPERTIES(${iosh_yacc_output} GENERATED)


##################################
#       Gather Dependencies      #
##################################


unset(LIBRARY)
unset(INCLUDE)

set(LIBRARY
    clockwork_client
    "readline"
    # "pthread"
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

list(APPEND source_files
    ${iosh_yacc_output}
    ${iosh_lex_output}
)

# add_library(${exe_name}
#     STATIC
#     ${source_files}
#     ${header_files}
# )
add_executable(${exe_name} ${source_files} ${header_files})
# add_test(${exe_name} ${exe_name})
# set_target_properties(${exe_name} PROPERTIES COMPILE_DEFINITIONS "USE_ETHERCAT")
set_target_properties(${exe_name} PROPERTIES COMPILE_DEFINITIONS "USE_ETHERCAT=1")
install(TARGETS ${exe_name} RUNTIME DESTINATION ${PROJECT_SOURCE_DIR})


target_link_libraries(${exe_name}
    ${LIBRARY}
)
target_include_directories(${exe_name}
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${INCLUDE}
)
