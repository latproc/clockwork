
find_path(mosquitto_INCLUDE_DIR
    NAMES
        "mosquitto.h"
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

find_library(mosquitto_LIBRARY
    NAMES
        "mosquitto"
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
set(mosquitto_INCLUDE_DIRS ${mosquitto_INCLUDE_DIR})
set(mosquitto_LIBRARIES ${mosquitto_LIBRARY})

find_package_handle_standard_args(mosquitto REQUIRED_VARS mosquitto_INCLUDE_DIRS mosquitto_LIBRARIES)
