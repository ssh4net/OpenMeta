cmake_minimum_required(VERSION 3.20)
include("${CMAKE_CURRENT_LIST_DIR}/test_python_interpreter.cmake")

if(NOT DEFINED METAREAD_BIN OR METAREAD_BIN STREQUAL "")
  message(FATAL_ERROR "METAREAD_BIN is required")
endif()
if(NOT EXISTS "${METAREAD_BIN}")
  message(FATAL_ERROR "metaread binary not found: ${METAREAD_BIN}")
endif()

if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_cli_metaread_photoshop_irb_smoke")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_jpg "${WORK_DIR}/photoshop_irb.jpg")

execute_process(
  COMMAND "${_openmeta_test_python}" -c
    "from pathlib import Path; u16=lambda v: bytes([(v>>8)&255,v&255]); u32=lambda v: bytes([(v>>24)&255,(v>>16)&255,(v>>8)&255,v&255]); data=u16(3)+u16(200)+u16(300)+u16(8)+u16(3); app=b'Photoshop 3.0'+bytes([0])+b'8BIM'+u16(0x03E8)+bytes([0,0])+u32(len(data))+data; Path(r'''${_jpg}''').write_bytes(bytes([255,216,255,237])+u16(len(app)+2)+app+bytes([255,217]))"
  RESULT_VARIABLE _rv_write
  OUTPUT_VARIABLE _out_write
  ERROR_VARIABLE _err_write
)
if(NOT _rv_write EQUAL 0)
  message(FATAL_ERROR
    "failed to write Photoshop IRB JPEG fixture (${_rv_write})\nstdout:\n${_out_write}\nstderr:\n${_err_write}")
endif()

execute_process(
  COMMAND "${METAREAD_BIN}" --no-build-info "${_jpg}"
  RESULT_VARIABLE _rv
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
)

if(NOT _rv EQUAL 0)
  message(FATAL_ERROR
    "metaread Photoshop IRB smoke failed (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
endif()

if(NOT _out MATCHES "0x03E8:Photoshop2ChannelCount")
  message(FATAL_ERROR
    "metaread output missing Photoshop2ChannelCount\nstdout:\n${_out}\nstderr:\n${_err}")
endif()

if(NOT _out MATCHES "0x03E8:Photoshop2Columns")
  message(FATAL_ERROR
    "metaread output missing Photoshop2Columns\nstdout:\n${_out}\nstderr:\n${_err}")
endif()

message(STATUS "metaread Photoshop IRB smoke gate passed")
