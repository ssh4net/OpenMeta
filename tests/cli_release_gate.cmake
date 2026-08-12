cmake_minimum_required(VERSION 3.20)

foreach(_var IN ITEMS METAREAD_BIN METADUMP_BIN THUMDUMP_BIN
                      METAVALIDATE_BIN METATRANSFER_BIN)
  if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
    message(FATAL_ERROR "${_var} is required")
  endif()
  if(NOT EXISTS "${${_var}}")
    message(FATAL_ERROR "${_var} binary not found: ${${_var}}")
  endif()
endforeach()

if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_cli_release_gate")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

execute_process(
  COMMAND ${CMAKE_COMMAND}
    "-DMETAREAD_BIN=${METAREAD_BIN}"
    "-DMETADUMP_BIN=${METADUMP_BIN}"
    "-DMETATRANSFER_BIN=${METATRANSFER_BIN}"
    "-DMETAVALIDATE_BIN=${METAVALIDATE_BIN}"
    "-DWORK_DIR=${WORK_DIR}/cli_metaread_safe_text_smoke"
    -P "${CMAKE_CURRENT_LIST_DIR}/cli_metaread_safe_text_smoke_test.cmake"
  RESULT_VARIABLE _rv_metaread
  OUTPUT_VARIABLE _out_metaread
  ERROR_VARIABLE _err_metaread
)
if(NOT _rv_metaread EQUAL 0)
  message(FATAL_ERROR
    "CLI release gate metaread smoke failed (${_rv_metaread})\n"
    "stdout:\n${_out_metaread}\n"
    "stderr:\n${_err_metaread}")
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND}
    "-DMETAREAD_BIN=${METAREAD_BIN}"
    "-DWORK_DIR=${WORK_DIR}/cli_metaread_photoshop_irb_smoke"
    -P "${CMAKE_CURRENT_LIST_DIR}/cli_metaread_photoshop_irb_smoke_test.cmake"
  RESULT_VARIABLE _rv_metaread_irb
  OUTPUT_VARIABLE _out_metaread_irb
  ERROR_VARIABLE _err_metaread_irb
)
if(NOT _rv_metaread_irb EQUAL 0)
  message(FATAL_ERROR
    "CLI release gate metaread Photoshop IRB smoke failed (${_rv_metaread_irb})\n"
    "stdout:\n${_out_metaread_irb}\n"
    "stderr:\n${_err_metaread_irb}")
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND}
    "-DMETAVALIDATE_BIN=${METAVALIDATE_BIN}"
    "-DWORK_DIR=${WORK_DIR}/cli_metavalidate_smoke"
    -P "${CMAKE_CURRENT_LIST_DIR}/cli_metavalidate_smoke_test.cmake"
  RESULT_VARIABLE _rv_metavalidate
  OUTPUT_VARIABLE _out_metavalidate
  ERROR_VARIABLE _err_metavalidate
)
if(NOT _rv_metavalidate EQUAL 0)
  message(FATAL_ERROR
    "CLI release gate metavalidate smoke failed (${_rv_metavalidate})\n"
    "stdout:\n${_out_metavalidate}\n"
    "stderr:\n${_err_metavalidate}")
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND}
    "-DMETAREAD_BIN=${METAREAD_BIN}"
    "-DMETADUMP_BIN=${METADUMP_BIN}"
    "-DTHUMDUMP_BIN=${THUMDUMP_BIN}"
    "-DMETAVALIDATE_BIN=${METAVALIDATE_BIN}"
    "-DMETATRANSFER_BIN=${METATRANSFER_BIN}"
    "-DWORK_DIR=${WORK_DIR}/cli_numeric_parse_smoke"
    -P "${CMAKE_CURRENT_LIST_DIR}/cli_numeric_parse_smoke_test.cmake"
  RESULT_VARIABLE _rv_numeric
  OUTPUT_VARIABLE _out_numeric
  ERROR_VARIABLE _err_numeric
)
if(NOT _rv_numeric EQUAL 0)
  message(FATAL_ERROR
    "CLI release gate numeric-parse smoke failed (${_rv_numeric})\n"
    "stdout:\n${_out_numeric}\n"
    "stderr:\n${_err_numeric}")
endif()
