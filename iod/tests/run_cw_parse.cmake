# Run: cmake -DCW=... -DFILE=... -DEXPECT_RC=... [-DMUST_CONTAIN=...] -P run_cw_parse.cmake
if(NOT CW OR NOT FILE)
  message(FATAL_ERROR "CW and FILE are required")
endif()
if(NOT DEFINED EXPECT_RC)
  set(EXPECT_RC 0)
endif()

execute_process(
  COMMAND "${CW}" --parse-only "${FILE}"
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
)
set(combined "${out}\n${err}")
if(NOT rc EQUAL EXPECT_RC)
  message(FATAL_ERROR
    "parse rc=${rc} expected ${EXPECT_RC} file=${FILE}\n${combined}")
endif()
if(MUST_CONTAIN)
  string(FIND "${combined}" "${MUST_CONTAIN}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR
      "output missing '${MUST_CONTAIN}' file=${FILE}\n${combined}")
  endif()
endif()
