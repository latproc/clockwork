if (RUN_TESTS)
# Download and unpack googletest at configure time
configure_file(CMakeLists.txt.in ${CMAKE_BINARY_DIR}/googletest-download/CMakeLists.txt)
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
  RESULT_VARIABLE result
	WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/googletest-download )
	message(${CMAKE_BINARY_DIR}/googletest-download )
if(result)
  message(FATAL_ERROR "CMake step for googletest failed: ${result}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} --build .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/googletest-download )
if(result)
  message(FATAL_ERROR "Build step for googletest failed: ${result}")
endif()

# Prevent overriding the parent project's compiler/linker
# settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Add googletest directly to our build. This defines
# the gtest and gtest_main targets.
add_subdirectory(${CMAKE_BINARY_DIR}/googletest-src
                 ${CMAKE_BINARY_DIR}/googletest-build
                 EXCLUDE_FROM_ALL)

# The gtest/gtest_main targets carry header search path
# dependencies automatically when using CMake 2.8.11 or
# later. Otherwise we have to add them here ourselves.
if (CMAKE_VERSION VERSION_LESS 2.8.11)
  include_directories("${gtest_SOURCE_DIR}/include")
endif()

set(DL_LIBRARY "dl")

add_executable(test_ecat_entry_selector test_ecat_entry_selector.cpp
  ${PROJECT_SOURCE_DIR}/src/EtherCATEntrySelector.cpp)
target_link_libraries(test_ecat_entry_selector gtest_main pthread)
add_test(NAME test_ecat_entry_selector COMMAND test_ecat_entry_selector)

add_executable(test_elc_setup_reapply_policy test_elc_setup_reapply_policy.cpp)
target_include_directories(test_elc_setup_reapply_policy PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_elc_setup_reapply_policy gtest_main pthread)
add_test(NAME test_elc_setup_reapply_policy COMMAND test_elc_setup_reapply_policy)

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(test_value test_value.cpp )
target_link_libraries(test_value gmock_main cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_value COMMAND test_value)

add_executable(test_disable_action iod_mock.h test_disable_action.cpp )
set_target_properties (test_disable_action PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_disable_action gmock_main cw_interpreter cw_client
  ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_disable_action COMMAND test_disable_action)

# LIST drain fixtures: in-tree types only.
add_executable(test_list_walkers test_list_walkers.cpp)
set_target_properties (test_list_walkers PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_compile_definitions(test_list_walkers PRIVATE
  LIST_WALKER_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures")
target_link_libraries(test_list_walkers gmock_main cw_interpreter cw_client
  ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_list_walkers COMMAND test_list_walkers)

add_executable(test_sync_remote_states_action iod_mock.h test_sync_remote_states_action.cpp)
target_link_libraries(test_sync_remote_states_action gmock_main cw_interpreter cw_client
        ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_sync_remote_states_action COMMAND test_sync_remote_states_action)

add_executable(test_buffering test_buffering.cpp )
target_link_libraries(test_buffering gmock_main cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_buffering COMMAND test_buffering)

add_executable(test_bit_ops test_bit_ops.cpp
   ${PROJECT_SOURCE_DIR}/src/bit_ops.cpp
)
set_target_properties (test_bit_ops PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_bit_ops gmock_main cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_bit_ops COMMAND test_bit_ops)

add_executable(test_clock test_clock.cpp )
set_target_properties (test_clock PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_clock gmock_main cw_client cw_interpreter ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_clock COMMAND test_clock)

add_executable(test_msg_encoding test_msg_encoding.cpp)
set_target_properties (test_msg_encoding PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_msg_encoding gmock_main cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_msg_encoding COMMAND test_msg_encoding)

add_executable(test_expressions test_expressions.cpp )
set_target_properties (test_expressions PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_expressions gmock_main cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_expressions COMMAND test_expressions)

add_executable(test_command_guards test_command_guards.cpp )
set_target_properties (test_command_guards PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_command_guards gmock_main cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_command_guards COMMAND test_command_guards)

add_executable(test_message test_message.cpp )
set_target_properties (test_message PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_message gmock_main cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_message COMMAND test_message)

add_executable(test_cs_holder test_cs_holder.cpp )
set_target_properties (test_cs_holder PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_cs_holder gmock_main cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_cs_holder COMMAND test_cs_holder)

add_executable(test_expr_eval expr_eval.cpp )
set_target_properties (test_expr_eval PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_expr_eval gmock_main cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_expr_eval COMMAND test_expr_eval)

add_executable(test_channels test_channels.cpp )
set_target_properties (test_channels PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_channels gmock_main cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_channels COMMAND test_message)

add_executable(test_symboltable test_symboltable.cpp )
target_link_libraries(test_symboltable gmock_main cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_symboltable COMMAND test_symboltable)


# test_ecat_xml previously linked IgH libethercat; on this branch plant iod is
# elc-only. Rebuild against iod-elc / EC_SIMULATOR when the XML parser test is
# next needed (disabled until then).
if (FALSE AND USE_ETHERCAT)
    add_executable(test_ecat_xml test_ecat_xml.cpp ${PROJECT_SOURCE_DIR}/src/ethercat_xml_parser.cpp )
    add_custom_command(
            TARGET test_ecat_xml POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                    ${PROJECT_SOURCE_DIR}/tests/test.xml
                    ${CMAKE_CURRENT_BINARY_DIR}/test.xml)
    target_include_directories(test_ecat_xml PRIVATE ${LIBXML2_INCLUDE_DIRS})
    target_link_libraries(test_ecat_xml gmock_main cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${LIBXML2_LIBRARIES} pthread)
    include_directories("${PROJECT_SOURCE_DIR}/tests")
    add_test(NAME test_ecat_xml COMMAND test_ecat_xml)
endif()

add_executable(test_split_string test_split_string.cpp)
set_target_properties (test_split_string PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_split_string gmock_main cw_interpreter ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} dl pthread)
add_test(NAME test_split_string COMMAND test_split_string)

add_executable(test_exec_command test_exec_command.cpp)
set_target_properties (test_exec_command PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_exec_command gmock_main cw_interpreter cw_client dl ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} pthread)
add_test(NAME test_exec_command COMMAND test_exec_command)

add_executable(test_exec_web_request test_exec_web_request.cpp)
set_target_properties (test_exec_web_request PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_exec_web_request cw_interpreter cw_client curl dl ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} pthread)
include_directories("${PROJECT_SOURCE_DIR}/src")
include_directories("${PROJECT_SOURCE_DIR}/ext/cpp-httplib")
add_test(NAME test_exec_web_request COMMAND test_exec_web_request)

add_executable(test_json_value test_json_value.cpp )
target_link_libraries(test_json_value gmock_main cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} dl pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_json_value COMMAND test_json_value)

add_executable(test_json_ownership test_json_ownership.cpp )
target_link_libraries(test_json_ownership gmock_main cw_interpreter cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_json_ownership COMMAND test_json_ownership)

add_executable(test_scheduler test_scheduler.cpp )
target_link_libraries(test_scheduler gmock_main cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} dl pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
if (USE_ASAN)
    message(STATUS "Skipping test_scheduler under ASan (Boost.Context fcontext reports false positives)")
else()
    add_test(NAME test_scheduler COMMAND test_scheduler)
endif()

add_executable(test_shared_queue test_shared_queue.cpp )
target_link_libraries(test_shared_queue gmock_main cw_client
    ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_shared_queue COMMAND test_shared_queue)

add_executable(test_thread_safe_queue test_thread_safe_queue.cpp )
target_link_libraries(test_thread_safe_queue gmock_main cw_client
    ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_thread_safe_queue COMMAND test_thread_safe_queue)

add_executable(test_regular_expressions test_regular_expressions.cpp )
target_link_libraries(test_regular_expressions gmock_main cw_client cw_interpreter
    ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} ${DL_LIBRARY} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_regular_expressions COMMAND test_regular_expressions)

endif()
