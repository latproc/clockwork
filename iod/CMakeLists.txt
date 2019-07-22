
cmake_minimum_required (VERSION 2.8)

if (MINGW_BUILD)
    include("lib/clockwork_client.git/cmake_modules/mingw_defines.cmake")
    get_property(CW_MINGW_DEFINITIONS GLOBAL PROPERTY CW_MINGW_DEFINITIONS)
    add_definitions(${CW_MINGW_DEFINITIONS})
endif()

include("cmake_modules/FindPackageHandleStandardArgs.cmake")

add_subdirectory(lib)
add_subdirectory(app)


if (RUN_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# install(TARGETS cw_interpreter iosh device_connector modbusd persistd
#     RUNTIME DESTINATION
#     ${PROJECT_SOURCE_DIR}
#     LIBRARY DESTINATION
#     ${PROJECT_SOURCE_DIR}
#     ARCHIVE DESTINATION
#     ${PROJECT_SOURCE_DIR}
# )
