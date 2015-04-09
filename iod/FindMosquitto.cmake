find_path(MOSQUITTO_INCLUDE_DIR
          mosquitto.h
          PATHS /usr/local/src/mosquitto/mosquitto-1.4.1/include)

find_library(MOSQUITTO_LIBRARY
             PATHS /usr/local/src/mosquitto/mosquitto-1.4.1/lib
             NAMES libmosquitto mosquitto)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MOSQUITTO DEFAULT_MSG MOSQUITTO_LIBRARY MOSQUITTO_INCLUDE_DIR)

if(MOSQUITTO_FOUND)
  set(MOSQUITTO_LIBRARIES ${MOSQUITTO_LIBRARY})
else(MOSQUITTO_FOUND)
  set(MOSQUITTO_LIBRARIES)
endif(MOSQUITTO_FOUND)

mark_as_advanced(MOSQUITTO_INCLUDE_DIR MOSQUITTO_LIBRARY)

# This file is based on a file in the changeling package https://github.com/JamesHarrison/changeling
#
#Changeling was developed by James Harrison (http://talkunafraid.co.uk/) for Insanity Radio 103.2 FM (http://www.insanityradio.com/).
#
#Copyright (c) 2012, James Harrison.
#
#All rights reserved.
#
#Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met: * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer. * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution. * Neither the name of the Changeling project nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
#
#THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL JAMES HARRISON OR OTHER CHANGELING CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

