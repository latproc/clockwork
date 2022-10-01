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

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(test_value test_value.cpp )
target_link_libraries(test_value gmock_main cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_value COMMAND test_value)

add_executable(test_disable_action iod_mock.h test_disable_action.cpp )
target_link_libraries(test_disable_action gmock_main cw_client cw_interpreter
  ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_disable_action COMMAND test_disable_action)

add_executable(test_buffering test_buffering.cpp )
target_link_libraries(test_buffering gmock_main cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_buffering COMMAND test_buffering)

add_executable(test_bit_ops test_bit_ops.cpp
   ${PROJECT_SOURCE_DIR}/src/bit_ops.cpp
)
set_target_properties (test_bit_ops PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_bit_ops gmock_main cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_bit_ops COMMAND test_bit_ops)

add_executable(test_clock test_clock.cpp )
set_target_properties (test_clock PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_clock gmock_main cw_client cw_interpreter ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_clock COMMAND test_clock)

add_executable(test_msg_encoding test_msg_encoding.cpp)
set_target_properties (test_msg_encoding PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_msg_encoding gmock_main cw_client cw_interpreter ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_msg_encoding COMMAND test_msg_encoding)

add_executable(test_expressions test_expressions.cpp )
set_target_properties (test_expressions PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_expressions gmock_main cw_client cw_interpreter ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_expressions COMMAND test_expressions)

add_executable(test_message test_message.cpp )
set_target_properties (test_message PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_message gmock_main cw_client cw_interpreter ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_message COMMAND test_message)

add_executable(test_cs_holder test_cs_holder.cpp )
set_target_properties (test_cs_holder PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_cs_holder gmock_main cw_client cw_interpreter ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_cs_holder COMMAND test_cs_holder)

add_executable(test_expr_eval expr_eval.cpp )
set_target_properties (test_expr_eval PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_expr_eval gmock_main cw_client cw_interpreter ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_expr_eval COMMAND test_expr_eval)

add_executable(test_channels test_channels.cpp )
set_target_properties (test_channels PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_channels gmock_main cw_client cw_interpreter ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} ${Mosquitto_LIBRARIES} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_channels COMMAND test_message)

#add_executable(test_ecat_xml test_ecat_xml.cpp ${PROJECT_SOURCE_DIR}/src/ethercat_xml_parser.cpp )
#set_target_properties (test_ecat_xml PROPERTIES COMPILE_DEFINITIONS EC_SIMULATOR )
#add_custom_command(
#        TARGET test_ecat_xml POST_BUILD
#        COMMAND ${CMAKE_COMMAND} -E copy
#                ${PROJECT_SOURCE_DIR}/tests/test.xml
#                ${CMAKE_CURRENT_BINARY_DIR}/test.xml)
#
#target_link_libraries(test_ecat_xml gmock_main cw_client ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} xml2 pthread)
#include_directories("${PROJECT_SOURCE_DIR}/tests")
#add_test(NAME test_ecat_xml COMMAND test_ecat_xml)

endif()
