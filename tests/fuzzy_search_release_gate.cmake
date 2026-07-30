if(NOT DEFINED OPENMETA_TESTS_BIN OR OPENMETA_TESTS_BIN STREQUAL "")
  message(FATAL_ERROR "OPENMETA_TESTS_BIN is required")
endif()

execute_process(
  COMMAND "${OPENMETA_TESTS_BIN}"
    "--gtest_filter=MetadataFuzzySearch.*"
    "--gtest_brief=1"
  RESULT_VARIABLE fuzzy_search_result
  OUTPUT_VARIABLE fuzzy_search_stdout
  ERROR_VARIABLE fuzzy_search_stderr
)

if(NOT fuzzy_search_result EQUAL 0)
  message(FATAL_ERROR
    "Fuzzy-search release gate failed (${fuzzy_search_result}).\n"
    "stdout:\n${fuzzy_search_stdout}\n"
    "stderr:\n${fuzzy_search_stderr}")
endif()

if(fuzzy_search_stdout MATCHES "\\[  SKIPPED \\]"
   OR fuzzy_search_stderr MATCHES "\\[  SKIPPED \\]")
  message(FATAL_ERROR
    "Fuzzy-search release gate skipped RapidFuzz-dependent tests.\n"
    "stdout:\n${fuzzy_search_stdout}\n"
    "stderr:\n${fuzzy_search_stderr}")
endif()

message(STATUS "Fuzzy-search release gate passed")
