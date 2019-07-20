find_path(ZMQPP_INCLUDE_DIR zmqpp/zmqpp.hpp)
find_library(ZMQPP_LIBRARY NAMES zmqpp)

set(ZMQPP_LIBRARIES ${ZMQPP_LIBRARY})
set(ZMQPP_INCLUDE_DIRS ${ZMQPP_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(ZMQPP DEFAULT_MSG ZMQPP_LIBRARIES ZMQPP_INCLUDE_DIR)
mark_as_advanced(ZMQPP_INCLUDE_DIR ZMQPP_LIBRARIES)

