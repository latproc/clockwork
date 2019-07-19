
set(exe_name "_ZMQ-Dep")


##################################
#       Gather Dependencies      #
##################################

if (MINGW_BUILD)
    set(LIBRARY
        zmq
    )
else(MINGW_BUILD)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")
    FIND_PACKAGE(Sodium REQUIRED)
    if(SODIUM_FOUND)
        add_definitions(-DHAVE_LIBSODIUM)
    else()
        message(FATAL_ERROR "no sodium?")
    endif()
    FIND_PACKAGE(ZMQ 4.2 REQUIRED)
    if (ZMQ_FOUND)
        #message("-- Found ZMQ")
        #message("        libs: " ${ZMQ_LIBRARIES})
        #message("        incl: " ${ZMQ_INCLUDE_DIRS})
    else (ZMQ_FOUND)
        message(FATAL_ERROR "## Not Found - ZMQ")
    endif (ZMQ_FOUND)
    set(LIBRARY ${ZMQ_LIBRARIES})
    set(INCLUDE ${ZMQ_INCLUDE_DIRS})
endif ()


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
