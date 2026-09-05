# Fail if product tests reference the site/SVN application tree.
set(_root "${CMAKE_CURRENT_LIST_DIR}")
set(_roots "${_root}" "${_root}/../../tests")
set(_hit "")
foreach(_dir IN LISTS _roots)
  if(NOT EXISTS "${_dir}")
    continue()
  endif()
  file(GLOB_RECURSE _files
    "${_dir}/*.cpp" "${_dir}/*.h" "${_dir}/*.c" "${_dir}/*.lpc"
    "${_dir}/*.cw" "${_dir}/*.cmake" "${_dir}/CMakeLists.txt")
  foreach(_f IN LISTS _files)
    get_filename_component(_bn "${_f}" NAME)
    if(_bn STREQUAL "check_no_site_paths.cmake")
      continue()
    endif()
    file(READ "${_f}" _txt)
    if(_txt MATCHES "/opt/latproc/code" OR
       _txt MATCHES "GrabPlanner\\.lpc" OR
       _txt MATCHES "CoreSelection\\.lpc")
      set(_hit "${_hit}\n  ${_f}")
    endif()
  endforeach()
endforeach()
if(_hit)
  message(FATAL_ERROR
    "Product tests must not reference the site application tree:${_hit}\n"
    "Use in-tree fixtures only. See AGENTS.md.")
endif()
