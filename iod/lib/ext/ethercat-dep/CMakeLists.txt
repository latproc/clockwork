
set(exe_name "Ethercat-Dep")

##################################
#       Gather Dependencies      #
##################################

if (MINGW_BUILD)
    set(LIBRARY
        # modbus
    )
else()
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")
    find_package(Ethercat)
    if (ethercat_FOUND)
        set(LIBRARY ${ethercat_LIBRARIES})
        set(INCLUDE ${ethercat_INCLUDE_DIRS})
    else()
        message(FATAL_ERROR "## Not Found - ethercat")
    endif()
endif()

list(APPEND LIBRARY pthread)


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
