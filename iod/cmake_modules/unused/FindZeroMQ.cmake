# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#.rst:
# FindZeroMQ
# --------
#
# Find ZeroMQ
#
# Find the native ZeroMQ includes and library This module defines
#
# ::
#
#   ZeroMQ_INCLUDE_DIR, where to find jpeglib.h, etc.
#   ZeroMQ_LIBRARIES, the libraries needed to use ZeroMQ.
#   ZeroMQ_FOUND, If false, do not try to use ZeroMQ.
#
# also defined, but not for general use are
#
# ::
#
#   ZeroMQ_LIBRARY, where to find the ZeroMQ library.

find_path(ZeroMQ_INCLUDE_DIR zmq.h)

set(ZeroMQ_NAMES ${ZeroMQ_NAMES} zmq libzmq)
find_library(ZeroMQ_LIBRARY NAMES ${ZeroMQ_NAMES} )

#include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(ZeroMQ DEFAULT_MSG ZeroMQ_LIBRARY ZeroMQ_INCLUDE_DIR)

if(ZeroMQ_FOUND)
  set(ZeroMQ_LIBRARIES ${ZeroMQ_LIBRARY})
endif()

# Deprecated declarations.
set (NATIVE_ZeroMQ_INCLUDE_PATH ${ZeroMQ_INCLUDE_DIR} )
if(ZeroMQ_LIBRARY)
  get_filename_component (NATIVE_ZeroMQ_LIB_PATH ${ZeroMQ_LIBRARY} PATH)
endif()

mark_as_advanced(ZeroMQ_LIBRARY ZeroMQ_INCLUDE_DIR )
