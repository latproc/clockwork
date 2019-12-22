
find_path(MODBUS_INCLUDE_DIR
    NAMES
        "MODBUS/MODBUS.h"
    PATHS
        "/usr"
        "/opt"
        "~/homebrew"
    PATH_SUFFIXES
        "local"
        "include"
        "external"
        "local/include"
				#    NO_DEFAULT_PATH
)

find_library(MODBUS_LIBRARY
    NAMES
        "MODBUS"
    PATHS
        "/usr"
        "/opt"
        "~/homebrew"
    PATH_SUFFIXES
        "local"
        "lib"
        "local/lib"
        "lib/x86_64-linux-gnu"
				#    NO_DEFAULT_PATH
)
set(MODBUS_INCLUDE_DIRS ${MODBUS_INCLUDE_DIR})
set(MODBUS_LIBRARIES ${MODBUS_LIBRARY})

find_package_handle_standard_args(MODBUS REQUIRED_VARS MODBUS_INCLUDE_DIRS MODBUS_LIBRARIES)
