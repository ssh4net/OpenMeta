if(DEFINED OPENMETA_PYTHON_EXECUTABLE
   AND NOT OPENMETA_PYTHON_EXECUTABLE STREQUAL "")
  if(NOT EXISTS "${OPENMETA_PYTHON_EXECUTABLE}")
    message(FATAL_ERROR
      "Python executable not found: ${OPENMETA_PYTHON_EXECUTABLE}")
  endif()
  set(_openmeta_test_python "${OPENMETA_PYTHON_EXECUTABLE}")
else()
  find_program(_openmeta_test_python NAMES python3 python REQUIRED)
endif()
