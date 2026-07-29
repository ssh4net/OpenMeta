cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED OPENMETA_TESTS_BIN OR OPENMETA_TESTS_BIN STREQUAL "")
  message(FATAL_ERROR "OPENMETA_TESTS_BIN is required")
endif()
if(NOT EXISTS "${OPENMETA_TESTS_BIN}")
  message(FATAL_ERROR "openmeta_tests binary not found: ${OPENMETA_TESTS_BIN}")
endif()

if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_read_release_gate")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_gtest_filter
  "BmffDerivedFieldsDecode.*:"
  "C2paContainers.*:"
  "CcmQuery.*:"
  "ContainerPayload.*:"
  "ContainerScan.*:"
  "CrwCiffDecode.*:"
  "DjiApp4Decode.*:"
  "ExifTagNames.*:"
  "ExifTiffDecode.*:"
  "ExrDecode.*:"
  "Flir.*:"
  "GeoTiff.*:"
  "IccDecodeTest.*:"
  "IccInterpret.*:"
  "InteropExport.*:"
  "IptcIimDecodeTest.*:"
  "JumbfDecode.*:"
  "LibRawAdapter.*:"
  "MetaStoreTest.*:"
  "MpfDecode.*:"
  "OcioAdapter.*:"
  "PhotoshopIrbDecodeTest.*:"
  "PreviewExtract.*:"
  "PrintImDecode.*:"
  "ReadLaneCoverage.*:"
  "ResourcePolicy.*:"
  "SimpleMetaRead.*:"
  "ValidateFile.*:"
  "XmpDecodeTest.*")

execute_process(
  COMMAND "${OPENMETA_TESTS_BIN}" "--gtest_filter=${_gtest_filter}"
  WORKING_DIRECTORY "${WORK_DIR}"
  RESULT_VARIABLE _rv_tests
  OUTPUT_VARIABLE _out_tests
  ERROR_VARIABLE _err_tests
)
if(NOT _rv_tests EQUAL 0)
  message(FATAL_ERROR
    "read release gate gtests failed (${_rv_tests})\n"
    "stdout:\n${_out_tests}\n"
    "stderr:\n${_err_tests}")
endif()
