
cmake_minimum_required (VERSION 2.8)

add_subdirectory(ext)
add_subdirectory(lib)
add_subdirectory(app)


if (RUN_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS cw_interpreter iosh device_connector modbusd persistd
    RUNTIME DESTINATION
    ${PROJECT_SOURCE_DIR}
    LIBRARY DESTINATION
    ${PROJECT_SOURCE_DIR}
    ARCHIVE DESTINATION
    ${PROJECT_SOURCE_DIR}
)
