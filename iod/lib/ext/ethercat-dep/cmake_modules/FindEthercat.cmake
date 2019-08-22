
find_path(ethercat_INCLUDE_DIR
    NAMES
        "ecrt.h"
    PATHS
        "/usr"
        "/opt"
        "/opt/etherlab"
        "~/homebrew"
    PATH_SUFFIXES
        "local"
        "include"
        "external"
        "local/include"
    NO_DEFAULT_PATH
)

find_library(ethercat_LIBRARY
    NAMES
        "ethercat"
    PATHS
        "/usr"
        "/opt"
        "/opt/etherlab"
        "~/homebrew"
    PATH_SUFFIXES
        "local"
        "lib"
        "local/lib"
        "lib/x86_64-linux-gnu"
    NO_DEFAULT_PATH
)
set(ethercat_INCLUDE_DIRS ${ethercat_INCLUDE_DIR})
set(ethercat_LIBRARIES ${ethercat_LIBRARY})

find_package_handle_standard_args(ethercat REQUIRED_VARS ethercat_INCLUDE_DIRS ethercat_LIBRARIES)
