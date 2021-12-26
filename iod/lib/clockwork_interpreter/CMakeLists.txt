cmake_minimum_required(VERSION 2.80)

set(exe_name "clockwork_interpreter")

set(Latproc_VERSION_MAJOR 0)
set(Latproc_VERSION_MINOR 9.6)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LocalCMakeLists.txt")
    include("${CMAKE_CURRENT_SOURCE_DIR}/LocalCMakeLists.txt")
endif()


#################################
##        find library         ##
#################################

set(INCLUDE
    ${CMAKE_CURRENT_BINARY_DIR}/generated_includes
    ${CMAKE_CURRENT_SOURCE_DIR}/../clockwork_client.git/clockwork/src/lib_clockwork_client
)
set(LIBRARY
    clockwork_client
)

if (MINGW_BUILD)
    list(APPEND INCLUDE
        "/usr/x86_64-w64-mingw32/include/mosquito/"
    )
endif()

##################################
#       generate config file     #
##################################

set(latproc_config_output "${CMAKE_CURRENT_BINARY_DIR}/generated_includes/LatprocConfig.h")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/LatprocConfig.h.in"
    ${latproc_config_output}
)
SET_SOURCE_FILES_PROPERTIES(${latproc_config_output} GENERATED)

##################################
#  generate yass and bison files #
##################################

# set(FLEX_EXECUTABLE lex)
find_package(FLEX)
if (NOT FLEX_EXECUTABLE)
  set (FLEXX_EXECUTABLE flex)
endif()

# set(BISON_EXECUTABLE bison)
find_package(BISON)
if (NOT BISON_EXECUTABLE)
  set (BISON_EXECUTABLE bison)
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(cw_lex_input ${CMAKE_CURRENT_SOURCE_DIR}/src_generators/cwlang.lpp)
set(cw_yac_input ${CMAKE_CURRENT_SOURCE_DIR}/src_generators/cwlang.ypp)

set(cw_lex_output ${CMAKE_CURRENT_BINARY_DIR}/generated/cwlang.yy.cpp)
set(cw_yacc_output ${CMAKE_CURRENT_BINARY_DIR}/generated/cwlang.tab.cpp)
# Create custom command for flex/lex (note the outputs)
ADD_CUSTOM_COMMAND(
    COMMAND ${FLEX_EXECUTABLE}
    ARGS -o ${cw_lex_output}
    	${cw_lex_input}
    OUTPUT ${cw_lex_output}
)

# Create custom command for bison/yacc (note the DEPENDS)
ADD_CUSTOM_COMMAND(
    COMMAND ${BISON_EXECUTABLE}
    ARGS -y
        -o ${cw_yacc_output}
    	-g -v -d ${cw_yac_input}
    DEPENDS ${cw_lex_input}
    OUTPUT ${cw_yacc_output}
)

# Since the parser source does not exists yet when cmake is run, mark
# it as generated
SET_SOURCE_FILES_PROPERTIES(${cw_lex_output} GENERATED)
SET_SOURCE_FILES_PROPERTIES(${cw_yacc_output} GENERATED)


# message(FATAL_ERROR "XX--- ${cw_lex_output} ${cw_yacc_output}")
##################################
#          Build Binary          #
##################################

FILE(GLOB_RECURSE source_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*
)
FILE(GLOB_RECURSE header_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h*
    ${latproc_config_output}
)
list(APPEND source_files
    ${cw_yacc_output}
    ${cw_lex_output}
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

add_library(${exe_name}
    STATIC
    ${source_files}
    ${header_files}
)
set_target_properties(${exe_name} PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR;USE_EXPERIMENTAL_IDLE_LOOP")

# add_executable(${exe_name} ${source_files} ${header_files})
# add_test(${exe_name} ${exe_name})

target_link_libraries(${exe_name}
    ${LIBRARY}
)
target_include_directories(${exe_name}
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${INCLUDE}
)
#set_target_properties (${exe_name} PROPERTIES COMPILE_DEFINITIONS )

if (MINGW_BUILD)
else()
    install(
        TARGETS
        ${exe_name}
        LIBRARY DESTINATION
        ${PROJECT_SOURCE_DIR}
        ARCHIVE DESTINATION
        ${PROJECT_SOURCE_DIR}
    )
endif()
