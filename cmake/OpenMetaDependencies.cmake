include_guard(GLOBAL)

function(openmeta_expat_library_is_static out_var library_path)
  set(_openmeta_is_static OFF)

  if(NOT library_path
     OR library_path STREQUAL "optimized"
     OR library_path STREQUAL "debug"
     OR library_path STREQUAL "general")
    set(${out_var} "${_openmeta_is_static}" PARENT_SCOPE)
    return()
  endif()

  get_filename_component(_openmeta_library_name "${library_path}" NAME)
  string(TOLOWER "${_openmeta_library_name}" _openmeta_library_name_lower)

  if(_openmeta_library_name_lower MATCHES "\\.a$")
    set(_openmeta_is_static ON)
  elseif(WIN32
         AND _openmeta_library_name_lower MATCHES "^(lib)?expat(w)?d?mt\\.lib$")
    set(_openmeta_is_static ON)
  endif()

  set(${out_var} "${_openmeta_is_static}" PARENT_SCOPE)
endfunction()

function(openmeta_expat_needs_xml_static out_var)
  set(_openmeta_needs_xml_static OFF)

  if(NOT OPENMETA_EXPAT_FOUND)
    set(${out_var} "${_openmeta_needs_xml_static}" PARENT_SCOPE)
    return()
  endif()

  if(TARGET EXPAT::EXPAT)
    get_target_property(_openmeta_expat_defs EXPAT::EXPAT
      INTERFACE_COMPILE_DEFINITIONS)
    if(_openmeta_expat_defs)
      list(FIND _openmeta_expat_defs "XML_STATIC"
        _openmeta_xml_static_index)
      if(NOT _openmeta_xml_static_index EQUAL -1)
        set(_openmeta_needs_xml_static ON)
      endif()
    endif()
  endif()

  if(NOT _openmeta_needs_xml_static)
    set(_openmeta_expat_candidates "")
    foreach(_openmeta_expat_var
        IN ITEMS EXPAT_LIBRARY_DEBUG EXPAT_LIBRARY_RELEASE EXPAT_LIBRARY)
      if(DEFINED ${_openmeta_expat_var} AND NOT "${${_openmeta_expat_var}}" STREQUAL "")
        list(APPEND _openmeta_expat_candidates "${${_openmeta_expat_var}}")
      endif()
    endforeach()
    if(DEFINED EXPAT_LIBRARIES)
      list(APPEND _openmeta_expat_candidates ${EXPAT_LIBRARIES})
    endif()

    foreach(_openmeta_expat_candidate IN LISTS _openmeta_expat_candidates)
      openmeta_expat_library_is_static(_openmeta_expat_candidate_is_static
        "${_openmeta_expat_candidate}")
      if(_openmeta_expat_candidate_is_static)
        set(_openmeta_needs_xml_static ON)
        break()
      endif()
    endforeach()
  endif()

  set(${out_var} "${_openmeta_needs_xml_static}" PARENT_SCOPE)
endfunction()

function(openmeta_apply_expat_dep target_name visibility)
  if(NOT OPENMETA_EXPAT_FOUND)
    return()
  endif()

  if(TARGET EXPAT::EXPAT)
    openmeta_reject_apple_static_shared_dependency(EXPAT::EXPAT)
    target_link_libraries(${target_name} ${visibility} EXPAT::EXPAT)
  else()
    target_include_directories(${target_name} ${visibility}
      "${EXPAT_INCLUDE_DIRS}")
    target_link_libraries(${target_name} ${visibility} "${EXPAT_LIBRARIES}")
  endif()

  target_compile_definitions(${target_name} PRIVATE OPENMETA_HAS_EXPAT=1)

  openmeta_expat_needs_xml_static(_openmeta_needs_xml_static)
  if(_openmeta_needs_xml_static)
    target_compile_definitions(${target_name} PRIVATE XML_STATIC)
  endif()
endfunction()

function(openmeta_add_googletest)
  if(TARGET gtest_main)
    set(OPENMETA_GTEST_PROVIDER "already-present" PARENT_SCOPE)
    return()
  endif()

  find_package(GTest CONFIG QUIET)
  if(GTest_FOUND)
    if(TARGET GTest::gtest_main)
      add_library(gtest_main ALIAS GTest::gtest_main)
      set(OPENMETA_GTEST_PROVIDER "system (config)" PARENT_SCOPE)
      return()
    endif()
    message(FATAL_ERROR "GTest was found, but no `GTest::gtest_main` target is available.")
  endif()

  find_package(GTest QUIET)
  if(GTest_FOUND)
    if(TARGET GTest::gtest_main)
      add_library(gtest_main ALIAS GTest::gtest_main)
      set(OPENMETA_GTEST_PROVIDER "system (module)" PARENT_SCOPE)
      return()
    endif()
    if(TARGET GTest::Main)
      add_library(gtest_main ALIAS GTest::Main)
      set(OPENMETA_GTEST_PROVIDER "system (module)" PARENT_SCOPE)
      return()
    endif()
    message(FATAL_ERROR "GTest was found, but no usable main target is available.")
  endif()

  message(FATAL_ERROR
    "GoogleTest not found. Install a package providing `GTest::gtest_main` (or `GTest::Main`) "
    "or set CMAKE_PREFIX_PATH to a custom install.")
endfunction()

function(openmeta_add_fuzztest)
  if(TARGET fuzztest::fuzztest)
    set(OPENMETA_FUZZTEST_PROVIDER "already-present" PARENT_SCOPE)
    return()
  endif()

  find_package(fuzztest CONFIG QUIET)
  if(fuzztest_FOUND)
    if(NOT TARGET fuzztest::fuzztest)
      message(FATAL_ERROR "fuzztest was found, but no `fuzztest::fuzztest` target is available.")
    endif()
    # Some packaged builds export `fuzztest::gtest_main` instead.
    if(NOT TARGET fuzztest::fuzztest_gtest_main)
      if(TARGET fuzztest::gtest_main)
        add_library(fuzztest::fuzztest_gtest_main ALIAS fuzztest::gtest_main)
      else()
        message(FATAL_ERROR
          "fuzztest was found, but no `fuzztest::fuzztest_gtest_main` (or `fuzztest::gtest_main`) target is available.")
      endif()
    endif()
    set(OPENMETA_FUZZTEST_PROVIDER "system (config)" PARENT_SCOPE)
    return()
  endif()

  # Optional fallback: build FuzzTest from sources using the in-repo wrapper.
  if(OPENMETA_DEPS_REPOS_ROOT)
    set(wrapper_src "${PROJECT_SOURCE_DIR}/cmake/third_party/fuzztest_pkg")
    if(NOT EXISTS "${wrapper_src}/CMakeLists.txt")
      message(FATAL_ERROR "Missing OpenMeta FuzzTest wrapper: ${wrapper_src}")
    endif()

    # Build the wrapper once per build tree.
    if(NOT TARGET openmeta_fuzztest_pkg_build)
      add_custom_target(openmeta_fuzztest_pkg_build)
      add_subdirectory(
        "${wrapper_src}"
        "${CMAKE_BINARY_DIR}/_deps/openmeta_fuzztest_pkg"
        EXCLUDE_FROM_ALL
      )
    endif()

    if(NOT TARGET fuzztest::fuzztest OR NOT TARGET fuzztest::fuzztest_gtest_main)
      message(FATAL_ERROR
        "FuzzTest wrapper build did not provide expected targets. "
        "Check OPENMETA_DEPS_REPOS_ROOT and CMAKE_PREFIX_PATH.")
    endif()

    set(OPENMETA_FUZZTEST_PROVIDER "local (wrapper)" PARENT_SCOPE)
    return()
  endif()

  message(FATAL_ERROR
    "FuzzTest not found. Install a package providing `fuzztest::fuzztest` and "
    "`fuzztest::fuzztest_gtest_main`, set CMAKE_PREFIX_PATH to a custom install, "
    "or set OPENMETA_DEPS_REPOS_ROOT to build FuzzTest from local sources.")
endfunction()

function(openmeta_apply_core_deps target_name)
  if(OPENMETA_ZLIB_FOUND)
    target_link_libraries(${target_name} PRIVATE ZLIB::ZLIB)
    target_compile_definitions(${target_name} PRIVATE OPENMETA_HAS_ZLIB=1)
  endif()

  if(OPENMETA_BROTLI_FOUND)
    target_link_libraries(${target_name} PRIVATE Brotli::decoder)
    target_compile_definitions(${target_name} PRIVATE OPENMETA_HAS_BROTLI=1)
  endif()

  if(OPENMETA_EXPAT_FOUND)
    openmeta_apply_expat_dep(${target_name} PRIVATE)
  endif()

  openmeta_apply_rapidfuzz_dep(${target_name})

  if(OPENMETA_ENABLE_C2PA_VERIFY)
    target_compile_definitions(${target_name} PRIVATE OPENMETA_ENABLE_C2PA_VERIFY=1)
  else()
    target_compile_definitions(${target_name} PRIVATE OPENMETA_ENABLE_C2PA_VERIFY=0)
  endif()

  if(OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE)
    target_compile_definitions(${target_name} PRIVATE OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE=1)
  else()
    target_compile_definitions(${target_name} PRIVATE OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE=0)
  endif()

  if(OPENMETA_OPENSSL_FOUND)
    target_link_libraries(${target_name} PRIVATE OpenSSL::Crypto)
    target_compile_definitions(${target_name} PRIVATE OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE=1)
  else()
    target_compile_definitions(${target_name} PRIVATE OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE=0)
  endif()
endfunction()

function(openmeta_apply_rapidfuzz_dep target_name)
  if(OPENMETA_RAPIDFUZZ_FOUND)
    target_compile_definitions(
      ${target_name}
      PRIVATE
        OPENMETA_HAS_RAPIDFUZZ=1)
    if(TARGET rapidfuzz::rapidfuzz)
      get_target_property(
        _openmeta_rapidfuzz_includes
        rapidfuzz::rapidfuzz
        INTERFACE_INCLUDE_DIRECTORIES)
      if(_openmeta_rapidfuzz_includes)
        target_include_directories(
          ${target_name}
          SYSTEM PRIVATE
            ${_openmeta_rapidfuzz_includes})
      else()
        target_link_libraries(${target_name} PRIVATE rapidfuzz::rapidfuzz)
      endif()
    elseif(OPENMETA_RAPIDFUZZ_INCLUDE_DIR)
      target_include_directories(
        ${target_name}
        SYSTEM PRIVATE
          "${OPENMETA_RAPIDFUZZ_INCLUDE_DIR}")
    endif()
  else()
    target_compile_definitions(
      ${target_name}
      PRIVATE
        OPENMETA_HAS_RAPIDFUZZ=0)
  endif()
endfunction()
