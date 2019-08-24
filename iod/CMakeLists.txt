
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

# install(TARGETS clockwork_interpreter iosh device_connector modbusd persistd
#     RUNTIME DESTINATION
#     ${PROJECT_SOURCE_DIR}
#     LIBRARY DESTINATION
#     ${PROJECT_SOURCE_DIR}
#     ARCHIVE DESTINATION
#     ${PROJECT_SOURCE_DIR}
# )

if(EXISTS "${PROJECT_SOURCE_DIR}/../ethercat/")
FIND_PACKAGE(LibXml2)

set (ETHERCAT_DIR ${PROJECT_SOURCE_DIR}/../ethercat)
add_library (ec_tool
${ETHERCAT_DIR}/tool/CommandAlias.cpp
${ETHERCAT_DIR}/tool/CommandConfig.cpp
${ETHERCAT_DIR}/tool/Command.cpp
${ETHERCAT_DIR}/tool/CommandCStruct.cpp
${ETHERCAT_DIR}/tool/CommandData.cpp
${ETHERCAT_DIR}/tool/CommandDebug.cpp
${ETHERCAT_DIR}/tool/CommandDomains.cpp
${ETHERCAT_DIR}/tool/CommandDownload.cpp
${ETHERCAT_DIR}/tool/CommandEoe.cpp
${ETHERCAT_DIR}/tool/CommandFoeRead.cpp
${ETHERCAT_DIR}/tool/CommandFoeWrite.cpp
${ETHERCAT_DIR}/tool/CommandGraph.cpp
${ETHERCAT_DIR}/tool/CommandCrc.cpp
${ETHERCAT_DIR}/tool/CommandMaster.cpp
${ETHERCAT_DIR}/tool/CommandPdos.cpp
${ETHERCAT_DIR}/tool/CommandRegRead.cpp
${ETHERCAT_DIR}/tool/CommandRegWrite.cpp
${ETHERCAT_DIR}/tool/CommandRescan.cpp
${ETHERCAT_DIR}/tool/CommandSdos.cpp
${ETHERCAT_DIR}/tool/CommandSiiRead.cpp
${ETHERCAT_DIR}/tool/CommandSiiWrite.cpp
${ETHERCAT_DIR}/tool/CommandSlaves.cpp
${ETHERCAT_DIR}/tool/CommandSoeRead.cpp
${ETHERCAT_DIR}/tool/CommandSoeWrite.cpp
${ETHERCAT_DIR}/tool/CommandStates.cpp
${ETHERCAT_DIR}/tool/CommandUpload.cpp
${ETHERCAT_DIR}/tool/CommandVersion.cpp
${ETHERCAT_DIR}/tool/CommandXml.cpp
${ETHERCAT_DIR}/tool/DataTypeHandler.cpp
${ETHERCAT_DIR}/tool/FoeCommand.cpp
${ETHERCAT_DIR}/tool/MasterDevice.cpp
${ETHERCAT_DIR}/tool/NumberListParser.cpp
${ETHERCAT_DIR}/tool/SdoCommand.cpp
${ETHERCAT_DIR}/tool/sii_crc.cpp
../ec_tool/SoeCommand.cpp
../ec_tool/tool_main.cpp
)
INCLUDE_DIRECTORIES(${ETHERCAT_DIR})
INCLUDE_DIRECTORIES(${ETHERCAT_DIR}/tool)
INCLUDE_DIRECTORIES(${ETHERCAT_DIR}/include)
INCLUDE_DIRECTORIES(${ETHERCAT_DIR}/master)
INCLUDE_DIRECTORIES(${LIBXML2_INCLUDE_DIR})
LINK_DIRECTORIES(${LIBXML2_LIBRARIES})
LINK_DIRECTORIES(${ETHERCAT_DIR}/lib/.libs)


endif()

