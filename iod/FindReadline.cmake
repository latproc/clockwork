find_path(READLINE_INCLUDE_DIR
          readline.h
          PATHS /usr/local/include /usr/local/include/readline)

find_library(READLINE_LIBRARY
             PATHS /usr/local/lib
             NAMES libreadline readline)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(READLINE DEFAULT_MSG READLINE_LIBRARY READLINE_INCLUDE_DIR)

if(READLINE_FOUND)
  set(READLINE_LIBRARIES ${READLINE_LIBRARY})
else(READLINE_FOUND)
  set(READLINE_LIBRARIES)
endif(READLINE_FOUND)

mark_as_advanced(READLINE_INCLUDE_DIR READLINE_LIBRARY)
