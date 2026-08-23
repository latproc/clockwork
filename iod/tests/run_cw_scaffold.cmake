# cmake -DCW_SCAFFOLD= -DCW= -DFROM= -DGOLDEN= -DCHANNEL= -DOUT_NAME= -P run_cw_scaffold.cmake
if(NOT CW_SCAFFOLD OR NOT CW OR NOT FROM OR NOT GOLDEN OR NOT CHANNEL OR NOT OUT_NAME)
  message(FATAL_ERROR "CW_SCAFFOLD, CW, FROM, GOLDEN, CHANNEL, OUT_NAME are required")
endif()

set(outdir "${CMAKE_CURRENT_LIST_DIR}/_scaffold_out")
file(REMOVE_RECURSE "${outdir}")
file(MAKE_DIRECTORY "${outdir}")
execute_process(
  COMMAND "${CW_SCAFFOLD}" --from "${FROM}" --out "${outdir}"
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "cw-scaffold rc=${rc}\n${out}\n${err}")
endif()

set(gen "${outdir}/${OUT_NAME}")
file(READ "${gen}" got)
file(READ "${GOLDEN}" want)
if(NOT got STREQUAL want)
  message(FATAL_ERROR "scaffold output does not match golden\n--- got ---\n${got}\n--- want ---\n${want}")
endif()

execute_process(
  COMMAND "${CW}" --parse-only "${FROM}" "${gen}" "${CHANNEL}"
  RESULT_VARIABLE prc
  OUTPUT_VARIABLE pout
  ERROR_VARIABLE perr
)
if(NOT prc EQUAL 0)
  message(FATAL_ERROR "generated LPC failed to parse rc=${prc}\n${pout}\n${perr}")
endif()
