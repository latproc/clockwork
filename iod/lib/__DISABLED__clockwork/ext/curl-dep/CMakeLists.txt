
set(exe_name "Curl-Dep")


##################################
#       Gather Dependencies      #
##################################

if (MINGW_BUILD)
    set(LIBRARY
        curl
    )
else()
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")
    FIND_PACKAGE(Curl REQUIRED)
    #   CURL_INCLUDE_DIRS   - where to find curl/curl.h, etc.
    #   CURL_LIBRARIES      - List of libraries when using curl.
    #   CURL_FOUND          - True if curl found.
    #   CURL_VERSION_STRING - the version of curl found (since CMake 2.8.8)

    set(INCLUDE ${CURL_INCLUDE_DIRS})
    set(LIBRARY ${CURL_LIBRARIES})
endif()


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
