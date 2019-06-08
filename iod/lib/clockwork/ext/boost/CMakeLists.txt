
set(exe_name "Boost-Dep")
SET(BUILD_SHARED_LIBS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")

if (MINGW_BUILD)
    set(LIBRARY
        boost_chrono
        boost_system
        boost_filesystem
        boost_thread
        boost_program_options
    )
else()
    FIND_PACKAGE(Boost 1.55 COMPONENTS system filesystem thread chrono program_options REQUIRED)
    if (Boost_FOUND)
        set(LIBRARY ${Boost_LIBRARIES})
        set(INCLUDE ${Boost_INCLUDE_DIR})
    else (Boost_Found)
        message(FATAL_ERROR "## Not Found -  BOOST")
    endif (Boost_FOUND)
endif()




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
##    sort out include files    ##
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
