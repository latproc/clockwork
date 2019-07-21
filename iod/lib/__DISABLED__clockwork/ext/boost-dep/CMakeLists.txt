
set(exe_name "Boost-Dep")

##################################
#       Gather Dependencies      #
##################################

if (MINGW_BUILD)
    set(LIBRARY
        boost_chrono
        boost_system
        boost_filesystem
        boost_thread
        boost_program_options
    )
else()
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")
    FIND_PACKAGE(Boost 1.58 COMPONENTS system filesystem thread chrono program_options REQUIRED)
    if (Boost_FOUND)
        set(LIBRARY ${Boost_LIBRARIES})
        set(INCLUDE ${Boost_INCLUDE_DIR})
    else (Boost_Found)
        message(FATAL_ERROR "## Not Found -  BOOST")
    endif (Boost_FOUND)
endif()
# if (APPLE)
#     #
# endif ()
#
if("${CMAKE_SYSTEM}" MATCHES "Linux")
    # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    list(APPEND LIBRARY pthread)
endif ()
#
# if (WINDOWS)
#     #
# endif ()


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
