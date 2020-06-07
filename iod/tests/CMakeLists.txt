if (RUN_TESTS)
# Download and unpack googletest at configure time
# Download and unpack googletest at configure time
configure_file(CMakeLists.txt.in googletest-download/CMakeLists.txt)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download )
if(result)
  message(FATAL_ERROR "CMake step for googletest failed: ${result}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} --build .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download )
if(result)
  message(FATAL_ERROR "Build step for googletest failed: ${result}")
endif()

# Prevent overriding the parent project's compiler/linker
# settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Add googletest directly to our build. This defines
# the gtest and gtest_main targets.
add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/googletest-src
                 ${CMAKE_CURRENT_BINARY_DIR}/googletest-build
                 EXCLUDE_FROM_ALL)

# The gtest/gtest_main targets carry header search path
# dependencies automatically when using CMake 2.8.11 or
# later. Otherwise we have to add them here ourselves.
if (CMAKE_VERSION VERSION_LESS 2.8.11)
  include_directories("${gtest_SOURCE_DIR}/include")
endif()

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(test_value test_value.cpp )
target_link_libraries(test_value gmock_main Clockwork ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_value COMMAND test_value)

add_executable(test_dyn_value test_dyn_value.cpp )
target_link_libraries(test_dyn_value gmock_main Clockwork ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_dyn_value COMMAND test_dyn_value)

#add_executable(test_disable_action test_disable_action.cpp )
#target_link_libraries(test_disable_action gmock_main Clockwork cw_interpreter
#  ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} mosquitto pthread)
#include_directories("${PROJECT_SOURCE_DIR}/tests")
#add_test(NAME test_disable_action COMMAND test_disable_action)

add_executable(test_buffering test_buffering.cpp )
target_link_libraries(test_buffering gmock_main Clockwork ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} mosquitto pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_buffering COMMAND test_buffering)

add_executable(test_bit_ops test_bit_ops.cpp
   ${PROJECT_SOURCE_DIR}/src/bit_ops.cpp
)
set_target_properties (test_bit_ops PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_bit_ops gmock_main Clockwork ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} mosquitto pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_bit_ops COMMAND test_bit_ops)

add_executable(test_alphanum_str test_alphanum_str.cpp
	${PROJECT_SOURCE_DIR}/src/ConnectionManager.cpp
)
set_target_properties (test_alphanum_str PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
target_link_libraries(test_alphanum_str gmock_main Clockwork ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} mosquitto pthread)
include_directories("${PROJECT_SOURCE_DIR}/tests")
add_test(NAME test_alphanum_str COMMAND test_bit_ops)
#add_executable(test_expressions test_expressions.cpp
#   ${PROJECT_SOURCE_DIR}/src/Expression.cpp
#   ${PROJECT_SOURCE_DIR}/src/dynamic_value.cpp
#)
#set_target_properties (test_expressions PROPERTIES COMPILE_DEFINITIONS "EC_SIMULATOR" )
#target_link_libraries(test_expressions gmock_main Clockwork ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} mosquitto pthread)
#include_directories("${PROJECT_SOURCE_DIR}/tests")
#add_test(NAME test_expressions COMMAND test_expressions)

#add_executable(test_ecat_xml test_ecat_xml.cpp ${PROJECT_SOURCE_DIR}/src/ethercat_xml_parser.cpp )
#set_target_properties (test_ecat_xml PROPERTIES COMPILE_DEFINITIONS EC_SIMULATOR )
#add_custom_command(
#        TARGET test_ecat_xml POST_BUILD
#        COMMAND ${CMAKE_COMMAND} -E copy
#                ${PROJECT_SOURCE_DIR}/tests/test.xml
#                ${CMAKE_CURRENT_BINARY_DIR}/test.xml)
#
#target_link_libraries(test_ecat_xml gmock_main Clockwork ${ZeroMQ_LIBRARY} ${Boost_LIBRARIES} xml2 pthread)
#include_directories("${PROJECT_SOURCE_DIR}/tests")
#add_test(NAME test_ecat_xml COMMAND test_ecat_xml)

endif()
