cmake_minimum_required(VERSION 2.80)
add_subdirectory(ext)

set(exe_name "Clockwork")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LocalCMakeLists.txt")
    include("${CMAKE_CURRENT_SOURCE_DIR}/LocalCMakeLists.txt")
endif()

#################################
##        find library         ##
#################################

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

add_library(${exe_name}
    STATIC
    ${source_files}
    ${header_files}
)

target_link_libraries(${exe_name}
    ${LIBRARY}
)
set (CMAKE_CXX_FLAGS "-std=c++11 ")


#################################
##    sort out include files   ##
#################################

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${INCLUDE}
)

install(TARGETS ${exe_name}
    LIBRARY DESTINATION
    ${PROJECT_SOURCE_DIR}
    ARCHIVE DESTINATION
    ${PROJECT_SOURCE_DIR}
)
