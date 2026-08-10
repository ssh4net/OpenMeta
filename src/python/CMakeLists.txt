include_guard(GLOBAL)

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module OPTIONAL_COMPONENTS Development.SABIModule)
find_package(nanobind CONFIG QUIET)
if(NOT nanobind_FOUND)
  # Nanobind installs its CMake package config under different layouts depending
  # on how it was built. One common variant is:
  #   <prefix>/nanobind/cmake/nanobind-config.cmake
  #
  # If the user points CMAKE_PREFIX_PATH at <prefix>, teach CMake where to find
  # nanobind without requiring an explicit `-Dnanobind_DIR=...`.
  foreach(_openmeta_prefix IN LISTS CMAKE_PREFIX_PATH)
    if(EXISTS "${_openmeta_prefix}/nanobind/cmake/nanobind-config.cmake"
       OR EXISTS "${_openmeta_prefix}/nanobind/cmake/nanobindConfig.cmake")
      set(nanobind_DIR "${_openmeta_prefix}/nanobind/cmake")
      break()
    endif()
  endforeach()
  if(nanobind_DIR)
    find_package(nanobind CONFIG QUIET)
  endif()

  set(_openmeta_nanobind_cmake_dir "")

  execute_process(
    COMMAND "${Python_EXECUTABLE}" -c
            "import nanobind, sys; p=getattr(nanobind,'cmake_dir',None); print(p() if callable(p) else '')"
    RESULT_VARIABLE _openmeta_nanobind_py_res
    OUTPUT_VARIABLE _openmeta_nanobind_py_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(_openmeta_nanobind_py_res EQUAL 0 AND _openmeta_nanobind_py_dir)
    set(_openmeta_nanobind_cmake_dir "${_openmeta_nanobind_py_dir}")
  else()
    execute_process(
      COMMAND "${Python_EXECUTABLE}" -m nanobind --cmakedir
      RESULT_VARIABLE _openmeta_nanobind_mod_res
      OUTPUT_VARIABLE _openmeta_nanobind_mod_dir
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(_openmeta_nanobind_mod_res EQUAL 0 AND _openmeta_nanobind_mod_dir)
      set(_openmeta_nanobind_cmake_dir "${_openmeta_nanobind_mod_dir}")
    endif()
  endif()

  if(_openmeta_nanobind_cmake_dir AND
     (EXISTS "${_openmeta_nanobind_cmake_dir}/nanobindConfig.cmake"
      OR EXISTS "${_openmeta_nanobind_cmake_dir}/nanobind-config.cmake"))
    set(nanobind_DIR "${_openmeta_nanobind_cmake_dir}")
    find_package(nanobind CONFIG REQUIRED)
  else()
    message(FATAL_ERROR
      "Could not find nanobind. Set CMAKE_PREFIX_PATH or nanobind_DIR to a prefix containing "
      "nanobindConfig.cmake (or nanobind-config.cmake), or install nanobind into "
      "${Python_EXECUTABLE} (pip install nanobind)."
    )
  endif()
endif()

set(OPENMETA_PYTHON_VERSION "${Python_VERSION}" PARENT_SCOPE)
set(OPENMETA_NANOBIND_FOUND 1 PARENT_SCOPE)

nanobind_add_module(openmeta_python
  src/openmeta_module.cc
)

set_target_properties(openmeta_python PROPERTIES
  OUTPUT_NAME "_openmeta"
  # nanobind's suffix already contains the selected Python ABI marker, such as
  # `_d.cp313-win_amd64.pyd`. A generic CMAKE_DEBUG_POSTFIX would duplicate it.
  DEBUG_POSTFIX ""
)

if(WIN32 AND NB_SUFFIX MATCHES "^_d\\.")
  # nanobind normally hides _DEBUG while including Python.h so a Debug module
  # can use a release Python installation. The selected interpreter itself is
  # a Py_DEBUG build here, so preserve that ABI for the module and nanobind's
  # separately compiled runtime library.
  target_compile_definitions(openmeta_python PRIVATE Py_DEBUG)
  if(TARGET nanobind-static)
    target_compile_definitions(nanobind-static PRIVATE Py_DEBUG)
  endif()
endif()

target_compile_definitions(openmeta_python PRIVATE OPENMETA_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(openmeta_python PRIVATE ${OPENMETA_LIB_TARGET})
openmeta_apply_core_deps(openmeta_python)

target_include_directories(
  openmeta_python
  PRIVATE
    "${PROJECT_SOURCE_DIR}/src/include"
)

# Make the build tree importable via: PYTHONPATH=<build>/python
set(_openmeta_py_build_dir "${CMAKE_BINARY_DIR}/python/openmeta")
file(MAKE_DIRECTORY "${_openmeta_py_build_dir}")
file(MAKE_DIRECTORY "${_openmeta_py_build_dir}/python")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/openmeta/__init__.py"
  "${CMAKE_BINARY_DIR}/python/openmeta/__init__.py"
  COPYONLY
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/openmeta/python/__init__.py"
  "${CMAKE_BINARY_DIR}/python/openmeta/python/__init__.py"
  COPYONLY
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/openmeta/python/metaread.py"
  "${CMAKE_BINARY_DIR}/python/openmeta/python/metaread.py"
  COPYONLY
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/openmeta/python/metadump.py"
  "${CMAKE_BINARY_DIR}/python/openmeta/python/metadump.py"
  COPYONLY
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/openmeta/python/metavalidate.py"
  "${CMAKE_BINARY_DIR}/python/openmeta/python/metavalidate.py"
  COPYONLY
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/openmeta/python/metatransfer.py"
  "${CMAKE_BINARY_DIR}/python/openmeta/python/metatransfer.py"
  COPYONLY
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/openmeta/python/openmeta_stats.py"
  "${CMAKE_BINARY_DIR}/python/openmeta/python/openmeta_stats.py"
  COPYONLY
)

set_target_properties(openmeta_python PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${_openmeta_py_build_dir}"
  RUNTIME_OUTPUT_DIRECTORY "${_openmeta_py_build_dir}"
)

# Install into the wheel/site-packages root.
set(_openmeta_py_install_dir "openmeta")
if(DEFINED SKBUILD_PLATLIB_DIR)
  set(_openmeta_py_install_dir "${SKBUILD_PLATLIB_DIR}/openmeta")
endif()

set(_openmeta_install_python_module ON)
# When building a wheel (via scikit-build-core), we must install the module
# into the wheel staging area (SKBUILD_PLATLIB_DIR).
#
# But for a normal CMake install with OPENMETA_BUILD_WHEEL enabled, the desired
# output is the wheel file itself (installed by the top-level install script),
# not a loose extension module in the install prefix.
if(OPENMETA_BUILD_WHEEL AND NOT DEFINED SKBUILD_PLATLIB_DIR)
  set(_openmeta_install_python_module OFF)
endif()

if(_openmeta_install_python_module)
  install(
    TARGETS openmeta_python
    LIBRARY DESTINATION "${_openmeta_py_install_dir}"
    RUNTIME DESTINATION "${_openmeta_py_install_dir}"
  )
  install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/openmeta/__init__.py"
    DESTINATION "${_openmeta_py_install_dir}"
  )

  install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/openmeta/python"
    DESTINATION "${_openmeta_py_install_dir}"
    FILES_MATCHING PATTERN "*.py"
  )
endif()
