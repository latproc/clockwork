
find_path(modbus_INCLUDE_DIR
    NAMES
        "modbus/modbus.h"
    PATHS
        "/usr"
        "/opt"
        "~/homebrew"
    PATH_SUFFIXES
        "local"
        "include"
        "external"
        "local/include"
    NO_DEFAULT_PATH
)

find_library(modbus_LIBRARY
    NAMES
        "modbus"
    PATHS
        "/usr"
        "/opt"
        "~/homebrew"
    PATH_SUFFIXES
        "local"
        "lib"
        "local/lib"
        "lib/x86_64-linux-gnu"
    NO_DEFAULT_PATH
)
set(modbus_INCLUDE_DIRS ${modbus_INCLUDE_DIR})
set(modbus_LIBRARIES ${modbus_LIBRARY})

find_package_handle_standard_args(modbus REQUIRED_VARS modbus_INCLUDE_DIRS modbus_LIBRARIES)
