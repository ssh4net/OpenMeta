if(NOT DEFINED OPENMETA_BUILD_DIR OR OPENMETA_BUILD_DIR STREQUAL "")
  message(FATAL_ERROR "OPENMETA_BUILD_DIR is required")
endif()
if(NOT DEFINED OPENMETA_SOURCE_DIR OR OPENMETA_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "OPENMETA_SOURCE_DIR is required")
endif()
if(NOT DEFINED OPENMETA_INSTALL_LIBDIR OR OPENMETA_INSTALL_LIBDIR STREQUAL "")
  message(FATAL_ERROR "OPENMETA_INSTALL_LIBDIR is required")
endif()
if(NOT DEFINED OPENMETA_WORK_DIR OR OPENMETA_WORK_DIR STREQUAL "")
  set(OPENMETA_WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/_shared_library_install")
endif()
if(NOT DEFINED OPENMETA_CONFIG OR OPENMETA_CONFIG STREQUAL "")
  set(OPENMETA_CONFIG "Release")
endif()

set(_openmeta_consumer_source
  "${OPENMETA_SOURCE_DIR}/tests/shared_library_consumer")
if(NOT EXISTS "${_openmeta_consumer_source}/CMakeLists.txt")
  message(FATAL_ERROR
    "OpenMeta shared consumer source is missing: ${_openmeta_consumer_source}")
endif()

file(REMOVE_RECURSE "${OPENMETA_WORK_DIR}")
set(_openmeta_install_dir "${OPENMETA_WORK_DIR}/install")
set(_openmeta_consumer_build_dir "${OPENMETA_WORK_DIR}/consumer-build")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${OPENMETA_BUILD_DIR}"
          --prefix "${_openmeta_install_dir}" --config "${OPENMETA_CONFIG}"
  RESULT_VARIABLE _openmeta_install_result
)
if(NOT _openmeta_install_result EQUAL 0)
  message(FATAL_ERROR "OpenMeta shared install failed: ${_openmeta_install_result}")
endif()

set(_openmeta_consumer_configure
  "${CMAKE_COMMAND}"
  -S "${_openmeta_consumer_source}"
  -B "${_openmeta_consumer_build_dir}"
  "-DOpenMeta_DIR=${_openmeta_install_dir}/${OPENMETA_INSTALL_LIBDIR}/cmake/OpenMeta"
)
if(NOT OPENMETA_GENERATOR_IS_MULTI_CONFIG)
  list(APPEND _openmeta_consumer_configure
    "-DCMAKE_BUILD_TYPE=${OPENMETA_CONFIG}")
endif()
if(DEFINED OPENMETA_GENERATOR AND NOT OPENMETA_GENERATOR STREQUAL "")
  list(APPEND _openmeta_consumer_configure -G "${OPENMETA_GENERATOR}")
endif()
if(DEFINED OPENMETA_CXX_COMPILER AND NOT OPENMETA_CXX_COMPILER STREQUAL "")
  list(APPEND _openmeta_consumer_configure
    "-DCMAKE_CXX_COMPILER=${OPENMETA_CXX_COMPILER}")
endif()
if(DEFINED OPENMETA_DEPENDENCY_PREFIX_PATH
   AND NOT OPENMETA_DEPENDENCY_PREFIX_PATH STREQUAL "")
  string(REPLACE ";" "\\;" _openmeta_dependency_prefix_path_arg
    "${OPENMETA_DEPENDENCY_PREFIX_PATH}")
  list(APPEND _openmeta_consumer_configure
    "-DCMAKE_PREFIX_PATH=${_openmeta_dependency_prefix_path_arg}")
endif()
execute_process(
  COMMAND ${_openmeta_consumer_configure}
  RESULT_VARIABLE _openmeta_consumer_configure_result
)
if(NOT _openmeta_consumer_configure_result EQUAL 0)
  message(FATAL_ERROR
    "OpenMeta shared consumer configure failed: ${_openmeta_consumer_configure_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${_openmeta_consumer_build_dir}"
          --config "${OPENMETA_CONFIG}"
  RESULT_VARIABLE _openmeta_consumer_build_result
)
if(NOT _openmeta_consumer_build_result EQUAL 0)
  message(FATAL_ERROR
    "OpenMeta shared consumer build failed: ${_openmeta_consumer_build_result}")
endif()

set(_openmeta_consumer_path_file
  "${_openmeta_consumer_build_dir}/openmeta_shared_consumer_path_${OPENMETA_CONFIG}.txt")
if(NOT EXISTS "${_openmeta_consumer_path_file}")
  message(FATAL_ERROR
    "OpenMeta shared consumer path file is missing: ${_openmeta_consumer_path_file}")
endif()
file(READ "${_openmeta_consumer_path_file}" _openmeta_consumer_executable)
string(STRIP "${_openmeta_consumer_executable}" _openmeta_consumer_executable)
if(NOT EXISTS "${_openmeta_consumer_executable}")
  message(FATAL_ERROR
    "OpenMeta shared consumer executable is missing: ${_openmeta_consumer_executable}")
endif()

if(WIN32)
  set(_openmeta_consumer_runtime_dirs "${_openmeta_install_dir}/bin")
  foreach(_openmeta_dependency_prefix IN LISTS OPENMETA_DEPENDENCY_PREFIX_PATH)
    if(IS_DIRECTORY "${_openmeta_dependency_prefix}/bin")
      list(APPEND _openmeta_consumer_runtime_dirs
        "${_openmeta_dependency_prefix}/bin")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _openmeta_consumer_runtime_dirs)
  list(JOIN _openmeta_consumer_runtime_dirs ";"
    _openmeta_consumer_runtime_path)
  message(STATUS
    "OpenMeta shared consumer runtime path: ${_openmeta_consumer_runtime_path}")
  set(ENV{PATH} "${_openmeta_consumer_runtime_path};$ENV{PATH}")
endif()

execute_process(
  COMMAND "${_openmeta_consumer_executable}"
  RESULT_VARIABLE _openmeta_consumer_run_result
)
if(NOT _openmeta_consumer_run_result EQUAL 0)
  message(FATAL_ERROR
    "OpenMeta shared consumer failed: ${_openmeta_consumer_run_result}")
endif()
