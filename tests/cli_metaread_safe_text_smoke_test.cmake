cmake_minimum_required(VERSION 3.20)

foreach(_var IN ITEMS METAREAD_BIN METADUMP_BIN METATRANSFER_BIN
                      METAVALIDATE_BIN)
  if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
    message(FATAL_ERROR "${_var} is required")
  endif()
  if(NOT EXISTS "${${_var}}")
    message(FATAL_ERROR "${_var} binary not found: ${${_var}}")
  endif()
endforeach()

if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_cli_metaread_safe_text_smoke")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_jpg "${WORK_DIR}/unsafe_text.jpg")

# JPEG with APP1 Exif containing IFD0:Make (0x010F, ASCII) = "A\\x01\\0".
# This should be rendered as a safe corrupted-text placeholder in metaread.
execute_process(
  COMMAND python3 -c
    "from pathlib import Path; Path(r'''${_jpg}''').write_bytes(bytes([255,216,255,225,0,34,69,120,105,102,0,0,73,73,42,0,8,0,0,0,1,0,15,1,2,0,3,0,0,0,65,1,0,0,0,0,0,0,255,217]))"
  RESULT_VARIABLE _rv_write
  OUTPUT_VARIABLE _out_write
  ERROR_VARIABLE _err_write
)
if(NOT _rv_write EQUAL 0)
  message(FATAL_ERROR
    "failed to write unsafe-text JPEG fixture (${_rv_write})\nstdout:\n${_out_write}\nstderr:\n${_err_write}")
endif()

execute_process(
  COMMAND "${METAREAD_BIN}" --no-build-info "${_jpg}"
  RESULT_VARIABLE _rv
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
)

if(NOT _rv EQUAL 0)
  message(FATAL_ERROR
    "metaread safe-text smoke failed (${_rv})\nstdout:\n${_out}\nstderr:\n${_err}")
endif()

if(NOT _out MATCHES "CORRUPTED_TEXT")
  message(FATAL_ERROR
    "metaread output missing CORRUPTED_TEXT placeholder\nstdout:\n${_out}\nstderr:\n${_err}")
endif()

if(NOT WIN32)
  execute_process(
    COMMAND python3 -c
      "import pathlib,subprocess,sys; d=pathlib.Path(r'''${WORK_DIR}'''); suffix=chr(27)+']52;c;Zm9v'+chr(7); p=d/('unsafe_'+suffix+'.jpg'); out=d/('unsafe_out_'+suffix+'.xmp'); p.write_bytes(bytes([255,216,255,217])); specs=[([r'''${METAREAD_BIN}''','--no-build-info',str(p)],0),([r'''${METADUMP_BIN}''','--no-build-info','--force','--out',str(out),str(p)],0),([r'''${METATRANSFER_BIN}''','--no-build-info','--output',str(p),str(p)],1),([r'''${METATRANSFER_BIN}''','--no-build-info','--load-transfer-artifact',str(p)],1),([r'''${METAVALIDATE_BIN}''','--no-build-info',str(p)],0)]; results=[(subprocess.run(cmd,capture_output=True),expected) for cmd,expected in specs]; sys.exit(0 if all(r.returncode==expected and bytes([27]) not in r.stdout+r.stderr and bytes([92,120,49,66]) in r.stdout+r.stderr for r,expected in results) else 1)"
    RESULT_VARIABLE _rv_path
    OUTPUT_VARIABLE _out_path
    ERROR_VARIABLE _err_path
  )
  if(NOT _rv_path EQUAL 0)
    message(FATAL_ERROR
      "CLI terminal-path escaping failed (${_rv_path})\nstdout:\n${_out_path}\nstderr:\n${_err_path}")
  endif()
endif()

message(STATUS "CLI safe-text smoke gate passed")
