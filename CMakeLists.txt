cmake_minimum_required(VERSION 3.20)

# Single source of truth for the project version (also used by the Python wheel).
set(_openmeta_version_file "${CMAKE_CURRENT_LIST_DIR}/VERSION")
if(NOT EXISTS "${_openmeta_version_file}")
  message(FATAL_ERROR "Missing version file: ${_openmeta_version_file}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_openmeta_version_file}")
file(READ "${_openmeta_version_file}" OPENMETA_VERSION)
string(STRIP "${OPENMETA_VERSION}" OPENMETA_VERSION)
if(NOT OPENMETA_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR
    "Invalid OpenMeta version in ${_openmeta_version_file}: '${OPENMETA_VERSION}' (expected MAJOR.MINOR.PATCH)")
endif()
set(OPENMETA_VERSION "${OPENMETA_VERSION}" CACHE STRING "OpenMeta project version" FORCE)

# Preserve CMAKE_PREFIX_PATH in the cache so GUI tools don't lose it between runs.
if(NOT CMAKE_PREFIX_PATH AND DEFINED ENV{CMAKE_PREFIX_PATH} AND NOT "$ENV{CMAKE_PREFIX_PATH}" STREQUAL "")
  set(_openmeta_env_prefix_path "$ENV{CMAKE_PREFIX_PATH}")
  if(NOT _openmeta_env_prefix_path MATCHES ";")
    string(REPLACE ":" ";" _openmeta_env_prefix_path "${_openmeta_env_prefix_path}")
  endif()
  set(CMAKE_PREFIX_PATH "${_openmeta_env_prefix_path}" CACHE PATH "CMake prefix search path" FORCE)
endif()

project(OpenMeta VERSION "${OPENMETA_VERSION}" LANGUAGES CXX)

option(OPENMETA_BUILD_STATIC "Build libopenmeta.a" ON)
option(OPENMETA_BUILD_SHARED "Build libopenmeta.so/.dylib/.dll" ON)
set(OPENMETA_ABI_VERSION "2" CACHE STRING
  "OpenMeta shared-library ABI version; increase only for ABI breaks")
if(NOT OPENMETA_ABI_VERSION MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR
    "OPENMETA_ABI_VERSION must be a positive integer: '${OPENMETA_ABI_VERSION}'")
endif()
option(OPENMETA_BUILD_TOOLS "Build OpenMeta CLI tools" ON)
option(OPENMETA_BUILD_TESTS "Build OpenMeta unit tests" OFF)
option(OPENMETA_BUILD_BENCHMARKS "Build OpenMeta benchmark executables" OFF)
option(OPENMETA_BUILD_FUZZERS "Build OpenMeta libFuzzer targets" OFF)
option(OPENMETA_BUILD_FUZZTEST "Build OpenMeta fuzz tests (FuzzTest)" OFF)
option(OPENMETA_FUZZTEST_FUZZING_MODE "Enable FuzzTest fuzzing mode flags" OFF)
option(OPENMETA_BUILD_DOCS "Build and install API docs (Doxygen)" OFF)
option(OPENMETA_BUILD_SPHINX_DOCS "Build and install HTML docs via Sphinx (Doxygen XML + Breathe)" OFF)
option(OPENMETA_BUILD_PYTHON "Build Python bindings (nanobind)" OFF)
option(OPENMETA_BUILD_WHEEL "Add an `openmeta_wheel` target (Python wheel build)" OFF)
option(OPENMETA_WHEEL_NO_BUILD_ISOLATION "Build wheel without isolation (requires scikit-build-core installed)" OFF)
option(OPENMETA_WITH_ZLIB "Enable zlib/deflate payload decompression" ON)
option(OPENMETA_WITH_BROTLI "Enable brotli payload decompression" ON)
option(OPENMETA_WITH_EXPAT "Enable XMP packet decode via Expat" ON)
option(OPENMETA_ENABLE_RAPIDFUZZ
  "Enable RapidFuzz-backed metadata fuzzy search" OFF)
option(OPENMETA_WITH_DNG_SDK_ADAPTER
  "Enable the optional Adobe DNG SDK adapter bridge" OFF)
option(OPENMETA_ENABLE_C2PA_VERIFY "Enable draft C2PA verification scaffold fields" OFF)
set(OPENMETA_C2PA_VERIFY_BACKEND "auto" CACHE STRING
  "Draft C2PA verify backend preference: auto/native/openssl/none")
set_property(CACHE OPENMETA_C2PA_VERIFY_BACKEND PROPERTY STRINGS auto native openssl none)
option(OPENMETA_USE_LIBCXX "Use libc++ as the C++ standard library (Clang)" OFF)

include(GNUInstallDirs)

set(OPENMETA_BUILD_TIMESTAMP "" CACHE STRING "Build timestamp in UTC (ISO-8601). Leave empty to set on first configure")

# Python interpreter override.
#
# CMake caches the discovered interpreter in the build directory, and CMake GUI
# often runs outside of an activated environment (uv/venv/conda). To ensure docs
# and wheel builds use the intended Python, provide an explicit interpreter here.
set(OPENMETA_PYTHON_EXECUTABLE "" CACHE FILEPATH "Override Python interpreter used by OpenMeta (docs/wheel/bindings/tests)")
if(OPENMETA_PYTHON_EXECUTABLE)
  if(NOT EXISTS "${OPENMETA_PYTHON_EXECUTABLE}")
    message(FATAL_ERROR "OPENMETA_PYTHON_EXECUTABLE does not exist: ${OPENMETA_PYTHON_EXECUTABLE}")
  endif()
  set(Python_EXECUTABLE "${OPENMETA_PYTHON_EXECUTABLE}" CACHE FILEPATH "Python interpreter" FORCE)
endif()

set(OPENMETA_OIIOTOOL_EXECUTABLE "" CACHE FILEPATH "Optional oiiotool executable for external image usability checks")
if(NOT OPENMETA_OIIOTOOL_EXECUTABLE)
  find_program(_openmeta_oiiotool_executable
    NAMES oiiotool
    HINTS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES bin)
  if(_openmeta_oiiotool_executable)
    set(OPENMETA_OIIOTOOL_EXECUTABLE "${_openmeta_oiiotool_executable}"
      CACHE FILEPATH "Optional oiiotool executable for external image usability checks" FORCE)
  endif()
endif()

set(OPENMETA_EXIFTOOL_EXECUTABLE "" CACHE FILEPATH "Optional exiftool executable for external metadata checks")
if(NOT OPENMETA_EXIFTOOL_EXECUTABLE)
  find_program(_openmeta_exiftool_executable NAMES exiftool)
  if(_openmeta_exiftool_executable)
    set(OPENMETA_EXIFTOOL_EXECUTABLE "${_openmeta_exiftool_executable}"
      CACHE FILEPATH "Optional exiftool executable for external metadata checks" FORCE)
  endif()
endif()

set(OPENMETA_FFMPEG_EXECUTABLE "" CACHE FILEPATH "Optional ffmpeg executable for external image decode checks")
if(NOT OPENMETA_FFMPEG_EXECUTABLE)
  find_program(_openmeta_ffmpeg_executable
    NAMES ffmpeg
    HINTS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES bin)
  if(_openmeta_ffmpeg_executable)
    set(OPENMETA_FFMPEG_EXECUTABLE "${_openmeta_ffmpeg_executable}"
      CACHE FILEPATH "Optional ffmpeg executable for external image decode checks" FORCE)
  endif()
endif()

set(OPENMETA_TEST_RUNTIME_LIBRARY_PATH "" CACHE STRING
  "Optional runtime library path prepended to CTest tests that launch external tools")

set(OPENMETA_BMFF_HEIF_TEST_TARGET "" CACHE FILEPATH "Optional existing HEIF/HEIC target image for external transfer usability checks")
set(OPENMETA_BMFF_AVIF_TEST_TARGET "" CACHE FILEPATH "Optional existing AVIF target image for external transfer usability checks")
set(OPENMETA_BMFF_CR3_TEST_TARGET "" CACHE FILEPATH "Optional existing CR3 target image for external transfer usability checks")
set(_openmeta_bmff_test_target_args
  "-DBMFF_HEIF_TEST_TARGET=${OPENMETA_BMFF_HEIF_TEST_TARGET}"
  "-DBMFF_AVIF_TEST_TARGET=${OPENMETA_BMFF_AVIF_TEST_TARGET}"
  "-DBMFF_CR3_TEST_TARGET=${OPENMETA_BMFF_CR3_TEST_TARGET}")

function(openmeta_apply_test_runtime_environment test_name)
  if(NOT OPENMETA_TEST_RUNTIME_LIBRARY_PATH)
    return()
  endif()

  if(WIN32)
    set(_openmeta_runtime_var "PATH")
    set(_openmeta_runtime_sep ";")
    set(_openmeta_runtime_existing "$ENV{PATH}")
  elseif(APPLE)
    set(_openmeta_runtime_var "DYLD_LIBRARY_PATH")
    set(_openmeta_runtime_sep ":")
    set(_openmeta_runtime_existing "$ENV{DYLD_LIBRARY_PATH}")
  else()
    set(_openmeta_runtime_var "LD_LIBRARY_PATH")
    set(_openmeta_runtime_sep ":")
    set(_openmeta_runtime_existing "$ENV{LD_LIBRARY_PATH}")
  endif()

  set(_openmeta_runtime_value "${OPENMETA_TEST_RUNTIME_LIBRARY_PATH}")
  if(_openmeta_runtime_existing)
    set(_openmeta_runtime_value
      "${_openmeta_runtime_value}${_openmeta_runtime_sep}${_openmeta_runtime_existing}")
  endif()
  set_tests_properties(${test_name} PROPERTIES
    ENVIRONMENT "${_openmeta_runtime_var}=${_openmeta_runtime_value}")
endfunction()

# Optional path to local dependency repos (used for the FuzzTest wrapper).
# Expected layout:
#   <root>/fuzztest
#   <root>/re2
#   <root>/antlr4
set(OPENMETA_DEPS_REPOS_ROOT "" CACHE PATH "Path to local dependency repos for optional source builds")
set(OPENMETA_MULTI_PREVIEW_SAMPLE "" CACHE FILEPATH
  "Optional sample file with 2+ embedded previews for CLI index-suffix integration test")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    CACHE STRING
    "MSVC runtime library used by OpenMeta and its CMake consumers")
endif()

if(OPENMETA_USE_LIBCXX)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "OPENMETA_USE_LIBCXX requires Clang.")
  endif()
  add_compile_options(-stdlib=libc++)
  add_link_options(-stdlib=libc++)
endif()

function(openmeta_apply_msvc_runtime_contract target_name)
  if(NOT MSVC)
    return()
  endif()

  set(_openmeta_runtime_contract "${CMAKE_MSVC_RUNTIME_LIBRARY}")
  if(_openmeta_runtime_contract STREQUAL "MultiThreaded")
    target_compile_options(${target_name} INTERFACE /MT)
  elseif(_openmeta_runtime_contract STREQUAL "MultiThreadedDebug")
    target_compile_options(${target_name} INTERFACE /MTd)
  elseif(_openmeta_runtime_contract STREQUAL "MultiThreadedDLL")
    target_compile_options(${target_name} INTERFACE /MD)
  elseif(_openmeta_runtime_contract STREQUAL "MultiThreadedDebugDLL")
    target_compile_options(${target_name} INTERFACE /MDd)
  elseif(_openmeta_runtime_contract
      STREQUAL "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(${target_name} INTERFACE
      "$<$<CONFIG:Debug>:/MTd>"
      "$<$<NOT:$<CONFIG:Debug>>:/MT>")
  elseif(_openmeta_runtime_contract
      STREQUAL "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    target_compile_options(${target_name} INTERFACE
      "$<$<CONFIG:Debug>:/MDd>"
      "$<$<NOT:$<CONFIG:Debug>>:/MD>")
  else()
    message(FATAL_ERROR
      "Unsupported CMAKE_MSVC_RUNTIME_LIBRARY for OpenMeta: "
      "${_openmeta_runtime_contract}")
  endif()
endfunction()

if(NOT OPENMETA_BUILD_STATIC AND NOT OPENMETA_BUILD_SHARED)
  message(FATAL_ERROR "Enable at least one of OPENMETA_BUILD_STATIC/OPENMETA_BUILD_SHARED.")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(OpenMetaDependencies)
include(OpenMetaTargetSettings)
include(OpenMetaReport)

if(OPENMETA_BUILD_DOCS OR OPENMETA_BUILD_SPHINX_DOCS)
  find_package(Doxygen REQUIRED)
endif()

set(OPENMETA_ZLIB_FOUND OFF)
if(OPENMETA_WITH_ZLIB)
  find_package(ZLIB QUIET)
  if(ZLIB_FOUND)
    set(OPENMETA_ZLIB_FOUND ON)
  endif()
endif()

set(OPENMETA_BROTLI_FOUND OFF)
if(OPENMETA_WITH_BROTLI)
  find_package(Brotli QUIET)
  if(Brotli_FOUND)
    set(OPENMETA_BROTLI_FOUND ON)
  endif()
endif()

set(OPENMETA_EXPAT_FOUND OFF)
if(OPENMETA_WITH_EXPAT)
  find_package(EXPAT QUIET)
  if(EXPAT_FOUND)
    set(OPENMETA_EXPAT_FOUND ON)
  endif()
endif()

set(OPENMETA_RAPIDFUZZ_FOUND OFF)
set(OPENMETA_RAPIDFUZZ_PROVIDER "")
if(OPENMETA_ENABLE_RAPIDFUZZ)
  find_package(rapidfuzz CONFIG QUIET)
  if(TARGET rapidfuzz::rapidfuzz)
    set(OPENMETA_RAPIDFUZZ_FOUND ON)
    set(OPENMETA_RAPIDFUZZ_PROVIDER "rapidfuzz package")
  else()
    find_path(OPENMETA_RAPIDFUZZ_INCLUDE_DIR NAMES rapidfuzz/fuzz.hpp)
    if(OPENMETA_RAPIDFUZZ_INCLUDE_DIR)
      set(OPENMETA_RAPIDFUZZ_FOUND ON)
      set(OPENMETA_RAPIDFUZZ_PROVIDER "include path")
    else()
      message(FATAL_ERROR
        "OPENMETA_ENABLE_RAPIDFUZZ=ON but RapidFuzz C++ was not found. "
        "Install rapidfuzz-cpp so CMake can find `rapidfuzz::rapidfuzz`, "
        "or set OPENMETA_RAPIDFUZZ_INCLUDE_DIR to the include directory "
        "containing rapidfuzz/fuzz.hpp.")
    endif()
  endif()
endif()

set(OPENMETA_DNG_SDK_FOUND OFF)
if(OPENMETA_WITH_DNG_SDK_ADAPTER)
  find_package(dng_sdk QUIET CONFIG)
  if(TARGET dng_sdk::dng_sdk)
    set(OPENMETA_DNG_SDK_FOUND ON)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT OPENMETA_USE_LIBCXX)
      message(WARNING
        "The detected dng_sdk package does not advertise its C++ standard "
        "library choice through CMake. If that package was built against "
        "libc++, configure OpenMeta with OPENMETA_USE_LIBCXX=ON to avoid "
        "link-time ABI failures.")
    endif()
  else()
    message(WARNING
      "OPENMETA_WITH_DNG_SDK_ADAPTER=ON but no dng_sdk package was found. "
      "The DNG SDK adapter will be compiled in unsupported-stub mode.")
  endif()
endif()

string(TOLOWER "${OPENMETA_C2PA_VERIFY_BACKEND}" OPENMETA_C2PA_VERIFY_BACKEND_LOWER)
if(NOT OPENMETA_C2PA_VERIFY_BACKEND_LOWER STREQUAL "auto"
   AND NOT OPENMETA_C2PA_VERIFY_BACKEND_LOWER STREQUAL "native"
   AND NOT OPENMETA_C2PA_VERIFY_BACKEND_LOWER STREQUAL "openssl"
   AND NOT OPENMETA_C2PA_VERIFY_BACKEND_LOWER STREQUAL "none")
  message(FATAL_ERROR
    "OPENMETA_C2PA_VERIFY_BACKEND must be one of: auto, native, openssl, none")
endif()
set(OPENMETA_C2PA_VERIFY_BACKEND "${OPENMETA_C2PA_VERIFY_BACKEND_LOWER}" CACHE STRING
  "Draft C2PA verify backend preference: auto/native/openssl/none" FORCE)

set(OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE OFF)
if(WIN32 OR APPLE)
  set(OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE ON)
endif()

set(OPENMETA_OPENSSL_FOUND OFF)
if(OPENMETA_ENABLE_C2PA_VERIFY
   AND (OPENMETA_C2PA_VERIFY_BACKEND_LOWER STREQUAL "openssl"
        OR OPENMETA_C2PA_VERIFY_BACKEND_LOWER STREQUAL "auto"))
  find_package(OpenSSL QUIET)
  if(OPENSSL_FOUND)
    set(OPENMETA_OPENSSL_FOUND ON)
  endif()
endif()

set(OPENMETA_CONFIG_HAS_ZLIB 0)
if(OPENMETA_ZLIB_FOUND)
  set(OPENMETA_CONFIG_HAS_ZLIB 1)
endif()

set(OPENMETA_CONFIG_HAS_BROTLI 0)
if(OPENMETA_BROTLI_FOUND)
  set(OPENMETA_CONFIG_HAS_BROTLI 1)
endif()

set(OPENMETA_CONFIG_HAS_EXPAT 0)
if(OPENMETA_EXPAT_FOUND)
  set(OPENMETA_CONFIG_HAS_EXPAT 1)
endif()

set(OPENMETA_CONFIG_HAS_OPENSSL 0)
if(OPENMETA_OPENSSL_FOUND)
  set(OPENMETA_CONFIG_HAS_OPENSSL 1)
endif()

set(OPENMETA_CONFIG_HAS_DNG_SDK 0)
if(OPENMETA_DNG_SDK_FOUND)
  set(OPENMETA_CONFIG_HAS_DNG_SDK 1)
endif()

if(OPENMETA_BUILD_TIMESTAMP STREQUAL "")
  string(TIMESTAMP _openmeta_ts "%Y-%m-%dT%H:%M:%SZ" UTC)
  set(OPENMETA_BUILD_TIMESTAMP "${_openmeta_ts}" CACHE STRING "Build timestamp in UTC (ISO-8601). Leave empty to set on first configure" FORCE)
endif()

set(OPENMETA_BUILDINFO_BUILD_TYPE "unknown")
if(CMAKE_CONFIGURATION_TYPES)
  set(OPENMETA_BUILDINFO_BUILD_TYPE "multi-config")
elseif(CMAKE_BUILD_TYPE)
  set(OPENMETA_BUILDINFO_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
endif()

set(OPENMETA_BUILDINFO_TIMESTAMP_UTC "${OPENMETA_BUILD_TIMESTAMP}")
set(OPENMETA_BUILDINFO_WITH_ZLIB "${OPENMETA_WITH_ZLIB}")
set(OPENMETA_BUILDINFO_WITH_BROTLI "${OPENMETA_WITH_BROTLI}")
set(OPENMETA_BUILDINFO_WITH_EXPAT "${OPENMETA_WITH_EXPAT}")
set(OPENMETA_BUILDINFO_ENABLE_RAPIDFUZZ "${OPENMETA_ENABLE_RAPIDFUZZ}")

set(_openmeta_generated_include_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
file(MAKE_DIRECTORY "${_openmeta_generated_include_dir}/openmeta")
configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/OpenMetaBuildInfoGenerated.h.in"
  "${_openmeta_generated_include_dir}/openmeta/build_info_generated.h"
  @ONLY
)

set(OPENMETA_SOURCES
  src/openmeta/build_info.cc
  src/openmeta/byte_arena.cc
  src/openmeta/bmff_fields_decode.cc
  src/openmeta/ccm_query.cc
  src/openmeta/compatibility_dump.cc
  src/openmeta/console_format.cc
  src/openmeta/container_payload.cc
  src/openmeta/container_scan.cc
  src/openmeta/crw_ciff_decode.cc
  src/openmeta/dng_sdk_adapter.cc
  src/openmeta/mapped_file.cc
  src/openmeta/exif_makernote_canon.cc
  src/openmeta/exif_makernote_casio.cc
  src/openmeta/exif_makernote_flir.cc
  src/openmeta/exif_makernote_fujifilm.cc
  src/openmeta/exif_makernote_hp.cc
  src/openmeta/exif_makernote_nintendo.cc
  src/openmeta/exif_makernote_nikon.cc
  src/openmeta/exif_makernote_olympus.cc
  src/openmeta/exif_makernote_panasonic.cc
  src/openmeta/exif_makernote_kodak.cc
  src/openmeta/exif_makernote_reconyx.cc
  src/openmeta/exif_makernote_ricoh.cc
  src/openmeta/exif_makernote_pentax.cc
  src/openmeta/exif_makernote_minolta.cc
  src/openmeta/exif_makernote_samsung.cc
  src/openmeta/exif_makernote_sony.cc
  src/openmeta/exif_makernote_tag_names.cc
  src/openmeta/exif_value_names.cc
  src/openmeta/exr_adapter.cc
  src/openmeta/exr_decode.cc
  src/openmeta/exif_tag_names.cc
  src/openmeta/exif_tiff_decode.cc
  src/openmeta/geotiff_decode.cc
  src/openmeta/geotiff_key_names.cc
  src/openmeta/host_adoption.cc
  src/openmeta/icc_decode.cc
  src/openmeta/icc_interpret.cc
  src/openmeta/interop_export.cc
  src/openmeta/interop_import.cc
  src/openmeta/interop_safety_internal.cc
  src/openmeta/interop_value_format_internal.cc
  src/openmeta/iptc_iim_decode.cc
  src/openmeta/jumbf_decode.cc
  src/openmeta/libraw_adapter.cc
  src/openmeta/meta_key.cc
  src/openmeta/metadata_capabilities.cc
  src/openmeta/metadata_concepts.cc
  src/openmeta/metadata_creation.cc
  src/openmeta/metadata_editing.cc
  src/openmeta/metadata_fuzzy_search.cc
  src/openmeta/metadata_interpretation.cc
  src/openmeta/metadata_query.cc
  src/openmeta/metadata_snapshot_io.cc
  src/openmeta/metadata_transfer.cc
  src/openmeta/metadata_logical_field_internal.cc
  src/openmeta/meta_store.cc
  src/openmeta/meta_edit.cc
  src/openmeta/meta_value.cc
  src/openmeta/ocio_adapter.cc
  src/openmeta/orientation.cc
  src/openmeta/phaseone_geometry.cc
  src/openmeta/photoshop_irb_decode.cc
  src/openmeta/preview_extract.cc
  src/openmeta/printim_decode.cc
  src/openmeta/random_access_source.cc
  src/openmeta/raf_decode.cc
  src/openmeta/simple_meta.cc
  src/openmeta/validate.cc
  src/openmeta/vendor_raw_processing.cc
  src/openmeta/xmp_decode.cc
  src/openmeta/xmp_dump.cc
  src/openmeta/x3f_decode.cc
)

if(OPENMETA_DNG_SDK_FOUND)
  # The Adobe DNG SDK throws dng_exception; keep that containment at the
  # optional adapter boundary instead of enabling exceptions project-wide.
  if(MSVC)
    set_source_files_properties(
      src/openmeta/dng_sdk_adapter.cc
      PROPERTIES COMPILE_OPTIONS "/EHsc")
  else()
    set_source_files_properties(
      src/openmeta/dng_sdk_adapter.cc
      PROPERTIES COMPILE_OPTIONS "-fexceptions")
  endif()
endif()

if(OPENMETA_BUILD_STATIC)
  add_library(openmeta_static STATIC ${OPENMETA_SOURCES})
  openmeta_apply_target_settings(openmeta_static)
  set_target_properties(openmeta_static PROPERTIES OUTPUT_NAME openmeta EXPORT_NAME openmeta_static)
  set_target_properties(openmeta_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(openmeta_static PRIVATE OPENMETA_BUILD_LINKAGE_STATIC=1)
  target_include_directories(openmeta_static PRIVATE "${_openmeta_generated_include_dir}")
endif()

if(OPENMETA_BUILD_SHARED)
  add_library(openmeta_shared SHARED ${OPENMETA_SOURCES})
  openmeta_apply_target_settings(openmeta_shared)
  set_target_properties(openmeta_shared PROPERTIES
    OUTPUT_NAME openmeta
    EXPORT_NAME openmeta_shared
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${OPENMETA_ABI_VERSION}"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
  )
  target_compile_definitions(openmeta_shared PRIVATE
    OPENMETA_BUILD_LINKAGE_SHARED=1
    OPENMETA_BUILDING_SHARED=1
  )
  target_compile_definitions(openmeta_shared INTERFACE OPENMETA_USING_SHARED=1)
  target_include_directories(openmeta_shared PRIVATE "${_openmeta_generated_include_dir}")

  if(UNIX AND NOT APPLE)
    # A custom prefix can supply static zlib/Brotli/Expat archives. Keep their
    # implementation symbols out of the ELF dynamic symbol table so they never
    # become part of OpenMeta's public ABI.
    target_link_options(openmeta_shared PRIVATE "-Wl,--exclude-libs,ALL")
  endif()
endif()

if(WIN32)
  # A DLL import library and a static archive otherwise both install as
  # `openmeta.lib`. Keep the shared runtime name stable and make archives
  # unambiguous for direct consumers as well as CMake package targets.
  if(TARGET openmeta_static)
    set_target_properties(openmeta_static PROPERTIES
      ARCHIVE_OUTPUT_NAME openmeta_static)
  endif()
  if(TARGET openmeta_shared)
    set_target_properties(openmeta_shared PROPERTIES
      ARCHIVE_OUTPUT_NAME openmeta_shared
      WINDOWS_EXPORT_ALL_SYMBOLS YES)
  endif()
endif()

function(openmeta_reject_apple_static_shared_dependency dependency_target)
  if(NOT APPLE OR NOT OPENMETA_BUILD_SHARED OR NOT TARGET ${dependency_target})
    return()
  endif()

  get_target_property(_openmeta_dependency_type ${dependency_target} TYPE)
  if(_openmeta_dependency_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR
      "Shared OpenMeta builds on macOS require dynamic ${dependency_target}; "
      "a static dependency would expose its symbols through the dylib.")
  endif()

  get_target_property(_openmeta_dependency_configs ${dependency_target}
    IMPORTED_CONFIGURATIONS)
  set(_openmeta_dependency_location_properties IMPORTED_LOCATION)
  foreach(_openmeta_dependency_config IN LISTS _openmeta_dependency_configs)
    list(APPEND _openmeta_dependency_location_properties
      "IMPORTED_LOCATION_${_openmeta_dependency_config}")
  endforeach()

  foreach(_openmeta_dependency_location_property
      IN LISTS _openmeta_dependency_location_properties)
    get_target_property(_openmeta_dependency_location ${dependency_target}
      ${_openmeta_dependency_location_property})
    if(_openmeta_dependency_location MATCHES "\\.a$")
      message(FATAL_ERROR
        "Shared OpenMeta builds on macOS require dynamic ${dependency_target}; "
        "found static archive: ${_openmeta_dependency_location}")
    endif()
  endforeach()
endfunction()

foreach(_tgt IN ITEMS openmeta_static openmeta_shared)
  if(TARGET ${_tgt})
    set(_openmeta_dependency_visibility PRIVATE)
    if(_tgt STREQUAL "openmeta_static")
      # A static archive does not carry its dependency closure. Export it to
      # consumers, while the shared target keeps implementation dependencies
      # private and records them in its own runtime link closure.
      set(_openmeta_dependency_visibility PUBLIC)
    endif()
    if(OPENMETA_ZLIB_FOUND)
      openmeta_reject_apple_static_shared_dependency(ZLIB::ZLIB)
      target_link_libraries(${_tgt} ${_openmeta_dependency_visibility} ZLIB::ZLIB)
      target_compile_definitions(${_tgt} PRIVATE OPENMETA_HAS_ZLIB=1)
    endif()
    if(OPENMETA_BROTLI_FOUND)
      openmeta_reject_apple_static_shared_dependency(Brotli::decoder)
      target_link_libraries(${_tgt} ${_openmeta_dependency_visibility} Brotli::decoder)
      target_compile_definitions(${_tgt} PRIVATE OPENMETA_HAS_BROTLI=1)
    endif()
    if(OPENMETA_EXPAT_FOUND)
      openmeta_apply_expat_dep(${_tgt} ${_openmeta_dependency_visibility})
    endif()
    openmeta_apply_rapidfuzz_dep(${_tgt})
    if(OPENMETA_DNG_SDK_FOUND)
      openmeta_reject_apple_static_shared_dependency(dng_sdk::dng_sdk)
      target_link_libraries(${_tgt} ${_openmeta_dependency_visibility} dng_sdk::dng_sdk)
      target_compile_definitions(${_tgt} PRIVATE OPENMETA_HAS_DNG_SDK=1)
    else()
      target_compile_definitions(${_tgt} PRIVATE OPENMETA_HAS_DNG_SDK=0)
    endif()
    if(OPENMETA_ENABLE_C2PA_VERIFY)
      target_compile_definitions(${_tgt} PRIVATE OPENMETA_ENABLE_C2PA_VERIFY=1)
    else()
      target_compile_definitions(${_tgt} PRIVATE OPENMETA_ENABLE_C2PA_VERIFY=0)
    endif()
    if(OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE)
      target_compile_definitions(${_tgt} PRIVATE OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE=1)
    else()
      target_compile_definitions(${_tgt} PRIVATE OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE=0)
    endif()
    if(OPENMETA_OPENSSL_FOUND)
      openmeta_reject_apple_static_shared_dependency(OpenSSL::Crypto)
      target_link_libraries(${_tgt} ${_openmeta_dependency_visibility} OpenSSL::Crypto)
      target_compile_definitions(${_tgt} PRIVATE OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE=1)
    else()
      target_compile_definitions(${_tgt} PRIVATE OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE=0)
    endif()
    if(OPENMETA_USE_LIBCXX)
      target_compile_options(${_tgt} INTERFACE -stdlib=libc++)
      target_link_options(${_tgt} INTERFACE -stdlib=libc++)
    endif()
    openmeta_apply_msvc_runtime_contract(${_tgt})
  endif()
endforeach()

if(OPENMETA_BUILD_STATIC)
  set(OPENMETA_LIB_TARGET openmeta_static)
else()
  set(OPENMETA_LIB_TARGET openmeta_shared)
endif()

if(TARGET openmeta_shared)
  add_custom_target(openmeta_gate_shared_install
    DEPENDS openmeta_shared
    COMMAND ${CMAKE_COMMAND}
      "-DOPENMETA_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
      "-DOPENMETA_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
      "-DOPENMETA_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_shared_library_install"
      "-DOPENMETA_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
      "-DOPENMETA_CONFIG=$<CONFIG>"
      "-DOPENMETA_GENERATOR=${CMAKE_GENERATOR}"
      "-DOPENMETA_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DOPENMETA_DEPENDENCY_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_library_install_test.cmake"
    COMMENT "Running installed shared-library consumer gate"
    VERBATIM
  )
endif()

if(OPENMETA_BUILD_BENCHMARKS)
  if(NOT OPENMETA_RAPIDFUZZ_FOUND)
    message(FATAL_ERROR
      "OPENMETA_BUILD_BENCHMARKS requires OPENMETA_ENABLE_RAPIDFUZZ=ON")
  endif()
  add_executable(openmeta_benchmark_fuzzy_search
    tests/benchmarks/metadata_fuzzy_search_benchmark.cc)
  openmeta_apply_target_settings(openmeta_benchmark_fuzzy_search)
  target_link_libraries(openmeta_benchmark_fuzzy_search
    PRIVATE ${OPENMETA_LIB_TARGET})
endif()

if(OPENMETA_BUILD_TOOLS)
  add_executable(metaread src/tools/metaread.cc)
  openmeta_apply_target_settings(metaread)
  target_link_libraries(metaread PRIVATE ${OPENMETA_LIB_TARGET})

  add_executable(metavalidate src/tools/metavalidate.cc)
  openmeta_apply_target_settings(metavalidate)
  target_link_libraries(metavalidate PRIVATE ${OPENMETA_LIB_TARGET})

  add_executable(metadump src/tools/metadump.cc)
  openmeta_apply_target_settings(metadump)
  target_link_libraries(metadump PRIVATE ${OPENMETA_LIB_TARGET})

  add_executable(metatransfer src/tools/metatransfer.cc)
  openmeta_apply_target_settings(metatransfer)
  target_link_libraries(metatransfer PRIVATE ${OPENMETA_LIB_TARGET})

  add_executable(thumdump src/tools/thumdump.cc)
  openmeta_apply_target_settings(thumdump)
  target_link_libraries(thumdump PRIVATE ${OPENMETA_LIB_TARGET})

  if(TARGET metavalidate)
    add_custom_target(openmeta_gate_metavalidate_smoke
      DEPENDS metavalidate
      COMMAND ${CMAKE_COMMAND}
        "-DMETAVALIDATE_BIN=$<TARGET_FILE:metavalidate>"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_cli_metavalidate_smoke"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_metavalidate_smoke_test.cmake"
      COMMENT "Running metavalidate smoke gate"
      VERBATIM
    )
  endif()

  if(TARGET metaread)
    add_custom_target(openmeta_gate_metaread_safe_text_smoke
      DEPENDS metaread metadump metatransfer metavalidate
      COMMAND ${CMAKE_COMMAND}
        "-DMETAREAD_BIN=$<TARGET_FILE:metaread>"
        "-DMETADUMP_BIN=$<TARGET_FILE:metadump>"
        "-DMETATRANSFER_BIN=$<TARGET_FILE:metatransfer>"
        "-DMETAVALIDATE_BIN=$<TARGET_FILE:metavalidate>"
        "-DOPENMETA_PYTHON_EXECUTABLE=${OPENMETA_PYTHON_EXECUTABLE}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_cli_metaread_safe_text_smoke"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_metaread_safe_text_smoke_test.cmake"
      COMMENT "Running CLI safe-text smoke gate"
      VERBATIM
    )

    add_custom_target(openmeta_gate_metaread_photoshop_irb_smoke
      DEPENDS metaread
      COMMAND ${CMAKE_COMMAND}
        "-DMETAREAD_BIN=$<TARGET_FILE:metaread>"
        "-DOPENMETA_PYTHON_EXECUTABLE=${OPENMETA_PYTHON_EXECUTABLE}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_cli_metaread_photoshop_irb_smoke"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_metaread_photoshop_irb_smoke_test.cmake"
      COMMENT "Running metaread Photoshop IRB smoke gate"
      VERBATIM
    )
  endif()

  if(TARGET metatransfer)
    add_custom_target(openmeta_gate_metatransfer_smoke
      DEPENDS metatransfer
      COMMAND ${CMAKE_COMMAND}
        "-DMETATRANSFER_BIN=$<TARGET_FILE:metatransfer>"
        "-DOPENMETA_PYTHON_EXECUTABLE=${OPENMETA_PYTHON_EXECUTABLE}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_cli_metatransfer_smoke"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_metatransfer_smoke_test.cmake"
      COMMENT "Running metatransfer smoke gate"
      VERBATIM
    )

    if(OPENMETA_OIIOTOOL_EXECUTABLE)
      add_custom_target(openmeta_gate_metatransfer_image_usability
        DEPENDS metatransfer
        COMMAND ${CMAKE_COMMAND}
          "-DMETATRANSFER_BIN=$<TARGET_FILE:metatransfer>"
          "-DOPENMETA_PYTHON_EXECUTABLE=${OPENMETA_PYTHON_EXECUTABLE}"
          "-DOIIOTOOL_BIN=${OPENMETA_OIIOTOOL_EXECUTABLE}"
          "-DEXIFTOOL_BIN=${OPENMETA_EXIFTOOL_EXECUTABLE}"
          "-DFFMPEG_BIN=${OPENMETA_FFMPEG_EXECUTABLE}"
          "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_metatransfer_image_usability"
          ${_openmeta_bmff_test_target_args}
          -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/metatransfer_image_usability_test.cmake"
        COMMENT "Running metatransfer external image usability gate"
        VERBATIM
      )
    endif()
  endif()

  if(TARGET metaread AND TARGET metadump AND TARGET thumdump
     AND TARGET metavalidate AND TARGET metatransfer)
    add_custom_target(openmeta_gate_cli_numeric_parse_smoke
      DEPENDS metaread metadump thumdump metavalidate metatransfer
      COMMAND ${CMAKE_COMMAND}
        "-DMETAREAD_BIN=$<TARGET_FILE:metaread>"
        "-DMETADUMP_BIN=$<TARGET_FILE:metadump>"
        "-DTHUMDUMP_BIN=$<TARGET_FILE:thumdump>"
        "-DMETAVALIDATE_BIN=$<TARGET_FILE:metavalidate>"
        "-DMETATRANSFER_BIN=$<TARGET_FILE:metatransfer>"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_cli_numeric_parse_smoke"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_numeric_parse_smoke_test.cmake"
      COMMENT "Running CLI numeric-parse smoke gate"
      VERBATIM
    )
  endif()
endif()

if(OPENMETA_BUILD_PYTHON)
  add_subdirectory(src/python)

  if(TARGET openmeta_python)
    if(NOT DEFINED Python_EXECUTABLE OR Python_EXECUTABLE STREQUAL "")
      find_package(Python REQUIRED COMPONENTS Interpreter)
    endif()

    add_custom_target(openmeta_gate_python_transfer_probe_smoke
      DEPENDS openmeta_python
      COMMAND ${CMAKE_COMMAND}
        "-DOPENMETA_PYTHON_EXECUTABLE=${Python_EXECUTABLE}"
        "-DOPENMETA_PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_python_transfer_probe_smoke"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_transfer_probe_smoke_test.cmake"
      COMMENT "Running python transfer-probe smoke gate"
      VERBATIM
    )

    add_custom_target(openmeta_gate_python_metatransfer_edit_smoke
      DEPENDS openmeta_python
      COMMAND ${CMAKE_COMMAND}
        "-DOPENMETA_PYTHON_EXECUTABLE=${Python_EXECUTABLE}"
        "-DOPENMETA_PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_python_metatransfer_edit_smoke"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_metatransfer_edit_smoke_test.cmake"
      COMMENT "Running python metatransfer edit smoke gate"
      VERBATIM
    )

    add_custom_target(openmeta_gate_python_metadata_creation_smoke
      DEPENDS openmeta_python
      COMMAND ${CMAKE_COMMAND}
        "-DOPENMETA_PYTHON_EXECUTABLE=${Python_EXECUTABLE}"
        "-DOPENMETA_PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_metadata_creation_smoke_test.cmake"
      COMMENT "Running python metadata creation smoke gate"
      VERBATIM
    )

    add_custom_target(openmeta_gate_python_metadata_editing_smoke
      DEPENDS openmeta_python
      COMMAND ${CMAKE_COMMAND}
        "-DOPENMETA_PYTHON_EXECUTABLE=${Python_EXECUTABLE}"
        "-DOPENMETA_PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_metadata_editing_smoke_test.cmake"
      COMMENT "Running python metadata editing smoke gate"
      VERBATIM
    )
  endif()
endif()

if(OPENMETA_BUILD_WHEEL)
  find_package(Python REQUIRED COMPONENTS Interpreter)
  find_program(OPENMETA_UV_EXECUTABLE NAMES uv)
  execute_process(
    COMMAND "${Python_EXECUTABLE}" -c "import pip"
    RESULT_VARIABLE _openmeta_python_has_pip
    OUTPUT_QUIET ERROR_QUIET
  )

  if(OPENMETA_WHEEL_NO_BUILD_ISOLATION)
    execute_process(
      COMMAND "${Python_EXECUTABLE}" -c "import scikit_build_core"
      RESULT_VARIABLE _openmeta_python_has_scikit_build_core
      OUTPUT_QUIET ERROR_QUIET
    )
    if(NOT _openmeta_python_has_scikit_build_core EQUAL 0)
      message(FATAL_ERROR
        "OPENMETA_WHEEL_NO_BUILD_ISOLATION requires Python package 'scikit-build-core' (module 'scikit_build_core') "
        "for: ${Python_EXECUTABLE}. Install it into that interpreter (e.g. pip install scikit-build-core), "
        "or set OPENMETA_WHEEL_NO_BUILD_ISOLATION=OFF.")
    endif()
  endif()

  if(NOT _openmeta_python_has_pip EQUAL 0 AND NOT OPENMETA_UV_EXECUTABLE)
    message(FATAL_ERROR
      "OPENMETA_BUILD_WHEEL: '${Python_EXECUTABLE}' has no pip module, and 'uv' was not found on PATH. "
      "Either install pip into that Python, or install uv.")
  endif()

  set(_openmeta_wheel_out_dir "${CMAKE_BINARY_DIR}/dist")
  set(_openmeta_wheel_config_settings "")

  function(openmeta_append_wheel_config_define openmeta_define_name openmeta_define_value)
    if("${openmeta_define_value}" STREQUAL "")
      return()
    endif()
    set(openmeta_escaped_define_value "${openmeta_define_value}")
    string(REPLACE ";" "\\;" openmeta_escaped_define_value "${openmeta_escaped_define_value}")
    list(APPEND _openmeta_wheel_config_settings
      "-C" "cmake.define.${openmeta_define_name}=${openmeta_escaped_define_value}")
    set(_openmeta_wheel_config_settings "${_openmeta_wheel_config_settings}" PARENT_SCOPE)
  endfunction()

  openmeta_append_wheel_config_define(OPENMETA_BUILD_PYTHON ON)
  openmeta_append_wheel_config_define(OPENMETA_BUILD_WHEEL OFF)
  openmeta_append_wheel_config_define(OPENMETA_BUILD_TOOLS OFF)
  openmeta_append_wheel_config_define(OPENMETA_BUILD_TESTS OFF)
  openmeta_append_wheel_config_define(OPENMETA_BUILD_BENCHMARKS OFF)
  openmeta_append_wheel_config_define(OPENMETA_BUILD_FUZZERS OFF)
  openmeta_append_wheel_config_define(OPENMETA_BUILD_FUZZTEST OFF)
  openmeta_append_wheel_config_define(OPENMETA_BUILD_DOCS OFF)
  openmeta_append_wheel_config_define(OPENMETA_BUILD_SPHINX_DOCS OFF)

  foreach(_openmeta_wheel_define_var IN ITEMS
      BUILD_SHARED_LIBS
      CMAKE_C_STANDARD
      CMAKE_C_FLAGS
      CMAKE_C_FLAGS_DEBUG
      CMAKE_C_FLAGS_RELEASE
      CMAKE_C_FLAGS_RELWITHDEBINFO
      CMAKE_C_FLAGS_MINSIZEREL
      CMAKE_CXX_STANDARD
      CMAKE_CXX_FLAGS
      CMAKE_CXX_FLAGS_DEBUG
      CMAKE_CXX_FLAGS_RELEASE
      CMAKE_CXX_FLAGS_RELWITHDEBINFO
      CMAKE_CXX_FLAGS_MINSIZEREL
      CMAKE_MSVC_RUNTIME_LIBRARY
      CMAKE_POLICY_VERSION_MINIMUM
      CMAKE_EXE_LINKER_FLAGS
      CMAKE_SHARED_LINKER_FLAGS
      CMAKE_MODULE_LINKER_FLAGS
      CMAKE_PREFIX_PATH
      OPENMETA_BUILD_STATIC
      OPENMETA_BUILD_SHARED
      OPENMETA_WITH_ZLIB
      OPENMETA_WITH_BROTLI
      OPENMETA_WITH_EXPAT
      OPENMETA_ENABLE_RAPIDFUZZ
      OPENMETA_RAPIDFUZZ_INCLUDE_DIR
      OPENMETA_WITH_DNG_SDK_ADAPTER
      OPENMETA_ENABLE_C2PA_VERIFY
      OPENMETA_C2PA_VERIFY_BACKEND
      OPENMETA_USE_LIBCXX
      OPENMETA_PYTHON_EXECUTABLE
      Python_EXECUTABLE
      Python_ROOT_DIR
      Python_INCLUDE_DIR
      Python_LIBRARY
      Python_LIBRARY_RELEASE
      Python_LIBRARY_DEBUG
      Python_SABI_LIBRARY
      Python_SABI_LIBRARY_RELEASE
      nanobind_DIR
      tsl-robin-map_DIR
      rapidfuzz_DIR
      dng_sdk_DIR
      dng_sdk_ROOT)
    if(DEFINED ${_openmeta_wheel_define_var})
      openmeta_append_wheel_config_define(
        "${_openmeta_wheel_define_var}" "${${_openmeta_wheel_define_var}}")
    endif()
  endforeach()

  set(_openmeta_install_wheel_script "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaInstallWheel.cmake")
  configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/OpenMetaInstallWheel.cmake.in"
    "${_openmeta_install_wheel_script}"
    @ONLY
  )

  add_custom_target(openmeta_wheel
    COMMAND "${CMAKE_COMMAND}"
            -DOPENMETA_WHEEL_INSTALL=OFF
            "-DOPENMETA_WHEEL_CONFIG=$<CONFIG>"
            -P "${_openmeta_install_wheel_script}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Building OpenMeta Python wheel"
    VERBATIM
  )
endif()

if(OPENMETA_BUILD_FUZZTEST)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "OPENMETA_BUILD_FUZZTEST requires Clang.")
  endif()

  openmeta_add_fuzztest()

  add_executable(openmeta_fuzztest_metastore
    ${OPENMETA_SOURCES}
    tests/fuzztest/metastore_fuzztest.cc
  )
  openmeta_apply_target_settings(openmeta_fuzztest_metastore)
  openmeta_apply_core_deps(openmeta_fuzztest_metastore)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(openmeta_fuzztest_metastore PRIVATE -Wno-nullability-extension)
  endif()
  target_link_libraries(openmeta_fuzztest_metastore PRIVATE fuzztest::fuzztest)
  target_link_libraries(openmeta_fuzztest_metastore PRIVATE fuzztest::fuzztest_gtest_main)

  if(OPENMETA_FUZZTEST_FUZZING_MODE)
    # Abseil enables "swisstable generations" automatically in sanitizer builds,
    # which changes internal container layouts. When linking against prebuilt
    # Abseil/FuzzTest without sanitizers, this can cause ODR/ABI mismatches and
    # early process aborts. Disable the generations feature for this target so
    # sanitized OpenMeta code can link against non-sanitized dependencies.
    target_compile_definitions(openmeta_fuzztest_metastore PRIVATE NDEBUG_SANITIZER)
    target_compile_options(
      openmeta_fuzztest_metastore
      PRIVATE
        -g
        -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
        -UNDEBUG
        -fsanitize-coverage=inline-8bit-counters
        -fsanitize-coverage=trace-cmp
        -fsanitize=address
        -DADDRESS_SANITIZER
        -fno-omit-frame-pointer
    )
    target_link_options(
      openmeta_fuzztest_metastore
      PRIVATE
        -fsanitize=address
        -fno-omit-frame-pointer
    )
  endif()
endif()

if(OPENMETA_BUILD_TESTS)
  enable_testing()
  if(TARGET openmeta_shared)
    add_test(
      NAME openmeta_shared_library_install_consumer
      COMMAND ${CMAKE_COMMAND}
        "-DOPENMETA_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        "-DOPENMETA_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DOPENMETA_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_shared_library_install"
        "-DOPENMETA_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
        "-DOPENMETA_CONFIG=$<CONFIG>"
        "-DOPENMETA_GENERATOR=${CMAKE_GENERATOR}"
        "-DOPENMETA_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DOPENMETA_DEPENDENCY_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_library_install_test.cmake"
    )
  endif()
  openmeta_add_googletest()

  add_executable(openmeta_tests
    tests/ccm_query_test.cc
    tests/compatibility_dump_test.cc
    tests/container_payload_test.cc
    tests/container_scan_test.cc
    tests/bmff_fields_decode_test.cc
    tests/c2pa_container_test.cc
    tests/jumbf_decode_test.cc
    tests/libraw_adapter_test.cc
    tests/crw_ciff_decode_test.cc
    tests/dng_sdk_adapter_test.cc
    tests/icc_decode_test.cc
    tests/icc_interpret_test.cc
    tests/exif_tag_names_test.cc
    tests/exr_adapter_test.cc
    tests/exr_decode_test.cc
    tests/exif_tiff_decode_test.cc
    tests/exif_value_names_test.cc
    tests/geotiff_decode_test.cc
    tests/host_adoption_test.cc
    tests/iptc_iim_decode_test.cc
    tests/interop_export_test.cc
    tests/interop_import_test.cc
    tests/dji_app4_decode_test.cc
    tests/makernote_decode_test.cc
    tests/metadata_capabilities_test.cc
    tests/metadata_concepts_test.cc
    tests/metadata_creation_test.cc
    tests/metadata_editing_test.cc
    tests/metadata_editing_transfer_test.cc
    tests/metadata_fuzzy_search_test.cc
    tests/metadata_interpretation_test.cc
    tests/metadata_query_test.cc
    tests/metadata_transfer_api_test.cc
    tests/flir_fff_decode_test.cc
    tests/meta_store_test.cc
    tests/mpf_decode_test.cc
    tests/ocio_adapter_test.cc
    tests/orientation_test.cc
    tests/photoshop_irb_decode_test.cc
    tests/phaseone_geometry_test.cc
    tests/preview_extract_test.cc
    tests/printim_decode_test.cc
    tests/public_metadata_fixtures_test.cc
    tests/random_access_source_test.cc
    tests/read_lane_coverage_test.cc
    tests/resource_policy_test.cc
    tests/simple_meta_bmff_exif_test.cc
    tests/simple_meta_xmp_test.cc
    tests/vendor_raw_processing_test.cc
    tests/validate_test.cc
    tests/xmp_decode_test.cc
    tests/xmp_dump_test.cc
  )
  if(OPENMETA_BUILD_SHARED)
    # This test probes a private formatter. Static linkage can resolve it from
    # the archive, but a shared library correctly keeps the implementation
    # hidden, so compile the test-only copy into the test executable.
    target_sources(openmeta_tests PRIVATE
      src/openmeta/interop_value_format_internal.cc)
  endif()
  target_link_libraries(openmeta_tests PRIVATE ${OPENMETA_LIB_TARGET} gtest_main)
  if(MSVC)
    target_compile_options(openmeta_tests PRIVATE /bigobj /utf-8)
  endif()
  # Tests need to select expectations matching the library configuration, but
  # those implementation flags must not become installed consumer usage
  # requirements of the shared library.
  target_compile_definitions(openmeta_tests PRIVATE
    OPENMETA_HAS_ZLIB=$<BOOL:${OPENMETA_ZLIB_FOUND}>
    OPENMETA_HAS_BROTLI=$<BOOL:${OPENMETA_BROTLI_FOUND}>
    OPENMETA_HAS_EXPAT=$<BOOL:${OPENMETA_EXPAT_FOUND}>
    OPENMETA_HAS_RAPIDFUZZ=$<BOOL:${OPENMETA_RAPIDFUZZ_FOUND}>
    OPENMETA_HAS_DNG_SDK=$<BOOL:${OPENMETA_DNG_SDK_FOUND}>
    OPENMETA_ENABLE_C2PA_VERIFY=$<BOOL:${OPENMETA_ENABLE_C2PA_VERIFY}>
    OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE=$<BOOL:${OPENMETA_C2PA_VERIFY_NATIVE_AVAILABLE}>
    OPENMETA_C2PA_VERIFY_OPENSSL_AVAILABLE=$<BOOL:${OPENMETA_OPENSSL_FOUND}>
  )
  if(OPENMETA_ZLIB_FOUND)
    target_link_libraries(openmeta_tests PRIVATE ZLIB::ZLIB)
  endif()
  if(OPENMETA_BROTLI_FOUND)
    target_link_libraries(openmeta_tests PRIVATE Brotli::decoder)
  endif()
  if(OPENMETA_EXPAT_FOUND)
    if(TARGET EXPAT::EXPAT)
      target_link_libraries(openmeta_tests PRIVATE EXPAT::EXPAT)
    else()
      target_include_directories(openmeta_tests PRIVATE "${EXPAT_INCLUDE_DIRS}")
      target_link_libraries(openmeta_tests PRIVATE "${EXPAT_LIBRARIES}")
    endif()
  endif()
  if(OPENMETA_DNG_SDK_FOUND)
    target_link_libraries(openmeta_tests PRIVATE dng_sdk::dng_sdk)
  endif()
  if(OPENMETA_OPENSSL_FOUND)
    target_link_libraries(openmeta_tests PRIVATE OpenSSL::Crypto)
  endif()
  if(TARGET Brotli::encoder)
    # Used by tests that synthesize brob payloads.
    target_link_libraries(openmeta_tests PRIVATE Brotli::encoder)
    target_compile_definitions(openmeta_tests PRIVATE OPENMETA_HAS_BROTLI_ENCODER=1)
  endif()

  add_test(NAME openmeta_tests COMMAND openmeta_tests)

  if(OPENMETA_RAPIDFUZZ_FOUND)
    add_custom_target(openmeta_gate_fuzzy_search_release
      DEPENDS openmeta_tests
      COMMAND ${CMAKE_COMMAND}
        "-DOPENMETA_TESTS_BIN=$<TARGET_FILE:openmeta_tests>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzzy_search_release_gate.cmake"
      COMMENT "Running fuzzy-search release gate"
      VERBATIM
    )
    add_test(
      NAME openmeta_fuzzy_search_release_gate
      COMMAND ${CMAKE_COMMAND}
        "-DOPENMETA_TESTS_BIN=$<TARGET_FILE:openmeta_tests>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzzy_search_release_gate.cmake"
    )
  endif()

  add_custom_target(openmeta_gate_read_release
    DEPENDS openmeta_tests
    COMMAND ${CMAKE_COMMAND}
      "-DOPENMETA_TESTS_BIN=$<TARGET_FILE:openmeta_tests>"
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_read_release_gate"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/read_release_gate.cmake"
    COMMENT "Running read release gate"
    VERBATIM
  )
  add_test(
    NAME openmeta_read_release_gate
    COMMAND ${CMAKE_COMMAND}
      "-DOPENMETA_TESTS_BIN=$<TARGET_FILE:openmeta_tests>"
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_read_release_gate"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/read_release_gate.cmake"
  )

  if(TARGET metatransfer AND TARGET openmeta_python)
    add_custom_target(openmeta_gate_transfer_release
      DEPENDS openmeta_tests metatransfer openmeta_python
      COMMAND ${CMAKE_COMMAND}
        "-DOPENMETA_TESTS_BIN=$<TARGET_FILE:openmeta_tests>"
        "-DMETATRANSFER_BIN=$<TARGET_FILE:metatransfer>"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_transfer_release_gate"
        "-DOPENMETA_PYTHON_EXECUTABLE=${Python_EXECUTABLE}"
        "-DOPENMETA_PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python"
        "-DOIIOTOOL_BIN=${OPENMETA_OIIOTOOL_EXECUTABLE}"
        "-DEXIFTOOL_BIN=${OPENMETA_EXIFTOOL_EXECUTABLE}"
        "-DFFMPEG_BIN=${OPENMETA_FFMPEG_EXECUTABLE}"
        ${_openmeta_bmff_test_target_args}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/transfer_release_gate.cmake"
      COMMENT "Running transfer release gate"
      VERBATIM
    )
    add_test(
      NAME openmeta_transfer_release_gate
      COMMAND ${CMAKE_COMMAND}
        "-DOPENMETA_TESTS_BIN=$<TARGET_FILE:openmeta_tests>"
        "-DMETATRANSFER_BIN=$<TARGET_FILE:metatransfer>"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_transfer_release_gate"
        "-DOPENMETA_PYTHON_EXECUTABLE=${Python_EXECUTABLE}"
        "-DOPENMETA_PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python"
        "-DOIIOTOOL_BIN=${OPENMETA_OIIOTOOL_EXECUTABLE}"
        "-DEXIFTOOL_BIN=${OPENMETA_EXIFTOOL_EXECUTABLE}"
        "-DFFMPEG_BIN=${OPENMETA_FFMPEG_EXECUTABLE}"
        ${_openmeta_bmff_test_target_args}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/transfer_release_gate.cmake"
    )
  elseif(TARGET metatransfer)
    add_custom_target(openmeta_gate_transfer_release
      DEPENDS openmeta_tests metatransfer
      COMMAND ${CMAKE_COMMAND}
        "-DOPENMETA_TESTS_BIN=$<TARGET_FILE:openmeta_tests>"
        "-DMETATRANSFER_BIN=$<TARGET_FILE:metatransfer>"
        "-DOPENMETA_PYTHON_EXECUTABLE=${OPENMETA_PYTHON_EXECUTABLE}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_transfer_release_gate"
        "-DOIIOTOOL_BIN=${OPENMETA_OIIOTOOL_EXECUTABLE}"
        "-DEXIFTOOL_BIN=${OPENMETA_EXIFTOOL_EXECUTABLE}"
        "-DFFMPEG_BIN=${OPENMETA_FFMPEG_EXECUTABLE}"
        ${_openmeta_bmff_test_target_args}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/transfer_release_gate.cmake"
      COMMENT "Running transfer release gate"
      VERBATIM
    )
    add_test(
      NAME openmeta_transfer_release_gate
      COMMAND ${CMAKE_COMMAND}
        "-DOPENMETA_TESTS_BIN=$<TARGET_FILE:openmeta_tests>"
        "-DMETATRANSFER_BIN=$<TARGET_FILE:metatransfer>"
        "-DOPENMETA_PYTHON_EXECUTABLE=${OPENMETA_PYTHON_EXECUTABLE}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_transfer_release_gate"
        "-DOIIOTOOL_BIN=${OPENMETA_OIIOTOOL_EXECUTABLE}"
        "-DEXIFTOOL_BIN=${OPENMETA_EXIFTOOL_EXECUTABLE}"
        "-DFFMPEG_BIN=${OPENMETA_FFMPEG_EXECUTABLE}"
        ${_openmeta_bmff_test_target_args}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/transfer_release_gate.cmake"
    )
  endif()

  if(TARGET metadump AND TARGET thumdump)
    add_test(
      NAME openmeta_cli_preview_index
      COMMAND ${CMAKE_COMMAND}
        "-DMETADUMP_BIN=$<TARGET_FILE:metadump>"
        "-DTHUMDUMP_BIN=$<TARGET_FILE:thumdump>"
        "-DSAMPLE_FILE=${OPENMETA_MULTI_PREVIEW_SAMPLE}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_cli_preview_index"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_preview_index_test.cmake"
    )
    set_tests_properties(openmeta_cli_preview_index PROPERTIES
      SKIP_REGULAR_EXPRESSION "^SKIP:")
  endif()

  if(TARGET metavalidate)
    add_test(
      NAME openmeta_cli_metavalidate_smoke
      COMMAND ${CMAKE_COMMAND}
        "-DMETAVALIDATE_BIN=$<TARGET_FILE:metavalidate>"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_cli_metavalidate_smoke"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_metavalidate_smoke_test.cmake"
    )
  endif()

  if(TARGET metaread)
    add_test(
      NAME openmeta_cli_safe_text_smoke
      COMMAND ${CMAKE_COMMAND}
        "-DMETAREAD_BIN=$<TARGET_FILE:metaread>"
        "-DMETADUMP_BIN=$<TARGET_FILE:metadump>"
        "-DMETATRANSFER_BIN=$<TARGET_FILE:metatransfer>"
        "-DMETAVALIDATE_BIN=$<TARGET_FILE:metavalidate>"
        "-DOPENMETA_PYTHON_EXECUTABLE=${OPENMETA_PYTHON_EXECUTABLE}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_cli_metaread_safe_text_smoke"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_metaread_safe_text_smoke_test.cmake"
    )

    add_test(
      NAME openmeta_cli_metaread_photoshop_irb_smoke
      COMMAND ${CMAKE_COMMAND}
        "-DMETAREAD_BIN=$<TARGET_FILE:metaread>"
        "-DOPENMETA_PYTHON_EXECUTABLE=${OPENMETA_PYTHON_EXECUTABLE}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_cli_metaread_photoshop_irb_smoke"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_metaread_photoshop_irb_smoke_test.cmake"
    )
  endif()

  if(TARGET metatransfer)
    add_test(
      NAME openmeta_cli_metatransfer_smoke
      COMMAND ${CMAKE_COMMAND}
        "-DMETATRANSFER_BIN=$<TARGET_FILE:metatransfer>"
        "-DOPENMETA_PYTHON_EXECUTABLE=${OPENMETA_PYTHON_EXECUTABLE}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_cli_metatransfer_smoke"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_metatransfer_smoke_test.cmake"
    )

    if(OPENMETA_OIIOTOOL_EXECUTABLE)
      add_test(
        NAME openmeta_cli_metatransfer_image_usability
        COMMAND ${CMAKE_COMMAND}
          "-DMETATRANSFER_BIN=$<TARGET_FILE:metatransfer>"
          "-DOPENMETA_PYTHON_EXECUTABLE=${OPENMETA_PYTHON_EXECUTABLE}"
          "-DOIIOTOOL_BIN=${OPENMETA_OIIOTOOL_EXECUTABLE}"
          "-DEXIFTOOL_BIN=${OPENMETA_EXIFTOOL_EXECUTABLE}"
          "-DFFMPEG_BIN=${OPENMETA_FFMPEG_EXECUTABLE}"
          "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_metatransfer_image_usability"
          ${_openmeta_bmff_test_target_args}
          -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/metatransfer_image_usability_test.cmake"
      )
    endif()
  endif()

  if(TARGET metaread AND TARGET metadump AND TARGET thumdump
     AND TARGET metavalidate AND TARGET metatransfer)
    add_custom_target(openmeta_gate_cli_release
      DEPENDS metaread metadump thumdump metavalidate metatransfer
      COMMAND ${CMAKE_COMMAND}
        "-DMETAREAD_BIN=$<TARGET_FILE:metaread>"
        "-DMETADUMP_BIN=$<TARGET_FILE:metadump>"
        "-DTHUMDUMP_BIN=$<TARGET_FILE:thumdump>"
        "-DMETAVALIDATE_BIN=$<TARGET_FILE:metavalidate>"
        "-DMETATRANSFER_BIN=$<TARGET_FILE:metatransfer>"
        "-DOPENMETA_PYTHON_EXECUTABLE=${OPENMETA_PYTHON_EXECUTABLE}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_cli_release_gate"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_release_gate.cmake"
      COMMENT "Running CLI release gate"
      VERBATIM
    )
    add_test(
      NAME openmeta_cli_release_gate
      COMMAND ${CMAKE_COMMAND}
        "-DMETAREAD_BIN=$<TARGET_FILE:metaread>"
        "-DMETADUMP_BIN=$<TARGET_FILE:metadump>"
        "-DTHUMDUMP_BIN=$<TARGET_FILE:thumdump>"
        "-DMETAVALIDATE_BIN=$<TARGET_FILE:metavalidate>"
        "-DMETATRANSFER_BIN=$<TARGET_FILE:metatransfer>"
        "-DOPENMETA_PYTHON_EXECUTABLE=${OPENMETA_PYTHON_EXECUTABLE}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_cli_release_gate"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_release_gate.cmake"
    )

    add_test(
      NAME openmeta_cli_numeric_parse_smoke
      COMMAND ${CMAKE_COMMAND}
        "-DMETAREAD_BIN=$<TARGET_FILE:metaread>"
        "-DMETADUMP_BIN=$<TARGET_FILE:metadump>"
        "-DTHUMDUMP_BIN=$<TARGET_FILE:thumdump>"
        "-DMETAVALIDATE_BIN=$<TARGET_FILE:metavalidate>"
        "-DMETATRANSFER_BIN=$<TARGET_FILE:metatransfer>"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_cli_numeric_parse_smoke"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_numeric_parse_smoke_test.cmake"
    )
  endif()

  if(TARGET openmeta_python)
    if(NOT DEFINED Python_EXECUTABLE OR Python_EXECUTABLE STREQUAL "")
      find_package(Python REQUIRED COMPONENTS Interpreter)
    endif()

    add_test(
      NAME openmeta_python_transfer_probe_smoke
      COMMAND ${CMAKE_COMMAND}
        "-DOPENMETA_PYTHON_EXECUTABLE=${Python_EXECUTABLE}"
        "-DOPENMETA_PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_python_transfer_probe_smoke"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_transfer_probe_smoke_test.cmake"
    )

    add_test(
      NAME openmeta_python_metatransfer_edit_smoke
      COMMAND ${CMAKE_COMMAND}
        "-DOPENMETA_PYTHON_EXECUTABLE=${Python_EXECUTABLE}"
        "-DOPENMETA_PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/_python_metatransfer_edit_smoke"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_metatransfer_edit_smoke_test.cmake"
    )

    add_test(
      NAME openmeta_python_metadata_creation_smoke
      COMMAND ${CMAKE_COMMAND}
        "-DOPENMETA_PYTHON_EXECUTABLE=${Python_EXECUTABLE}"
        "-DOPENMETA_PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_metadata_creation_smoke_test.cmake"
    )

    add_test(
      NAME openmeta_python_metadata_editing_smoke
      COMMAND ${CMAKE_COMMAND}
        "-DOPENMETA_PYTHON_EXECUTABLE=${Python_EXECUTABLE}"
        "-DOPENMETA_PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_metadata_editing_smoke_test.cmake"
    )
  endif()

  if(OPENMETA_TEST_RUNTIME_LIBRARY_PATH)
    get_property(_openmeta_test_names DIRECTORY PROPERTY TESTS)
    foreach(_openmeta_test_name IN LISTS _openmeta_test_names)
      openmeta_apply_test_runtime_environment("${_openmeta_test_name}")
    endforeach()
  endif()
endif()

if(OPENMETA_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "OPENMETA_BUILD_FUZZERS requires Clang (libFuzzer).")
  endif()

  function(openmeta_apply_libfuzzer_stdlib target_name)
    # On Linux, Clang's bundled libFuzzer runtime is typically built against
    # libstdc++. If the rest of the build uses libc++ (e.g. to match prebuilt
    # GoogleTest), override the fuzz targets to use libstdc++ so they can link.
    if(OPENMETA_USE_LIBCXX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
      target_compile_options(${target_name} PRIVATE -stdlib=libstdc++)
      target_link_options(${target_name} PRIVATE -stdlib=libstdc++)
    endif()
  endfunction()

  add_executable(openmeta_fuzz_metastore
    ${OPENMETA_SOURCES}
    tests/fuzz/metastore_libfuzzer.cc
  )
  openmeta_apply_target_settings(openmeta_fuzz_metastore)
  openmeta_apply_core_deps(openmeta_fuzz_metastore)
  openmeta_apply_libfuzzer_stdlib(openmeta_fuzz_metastore)

  target_compile_options(
    openmeta_fuzz_metastore
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
  target_link_options(
    openmeta_fuzz_metastore
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )

  add_executable(openmeta_fuzz_container_scan
    ${OPENMETA_SOURCES}
    tests/fuzz/container_scan_libfuzzer.cc
  )
  openmeta_apply_target_settings(openmeta_fuzz_container_scan)
  openmeta_apply_core_deps(openmeta_fuzz_container_scan)
  openmeta_apply_libfuzzer_stdlib(openmeta_fuzz_container_scan)

  target_compile_options(
    openmeta_fuzz_container_scan
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
  target_link_options(
    openmeta_fuzz_container_scan
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )

  add_executable(openmeta_fuzz_bmff_meta
    ${OPENMETA_SOURCES}
    tests/fuzz/bmff_meta_libfuzzer.cc
  )
  openmeta_apply_target_settings(openmeta_fuzz_bmff_meta)
  openmeta_apply_core_deps(openmeta_fuzz_bmff_meta)
  openmeta_apply_libfuzzer_stdlib(openmeta_fuzz_bmff_meta)

  target_compile_options(
    openmeta_fuzz_bmff_meta
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
  target_link_options(
    openmeta_fuzz_bmff_meta
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )

  add_executable(openmeta_fuzz_jumbf_decode
    ${OPENMETA_SOURCES}
    tests/fuzz/jumbf_decode_libfuzzer.cc
  )
  openmeta_apply_target_settings(openmeta_fuzz_jumbf_decode)
  openmeta_apply_core_deps(openmeta_fuzz_jumbf_decode)
  openmeta_apply_libfuzzer_stdlib(openmeta_fuzz_jumbf_decode)

  target_compile_options(
    openmeta_fuzz_jumbf_decode
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
  target_link_options(
    openmeta_fuzz_jumbf_decode
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )

  add_executable(openmeta_fuzz_exif_tiff_decode
    ${OPENMETA_SOURCES}
    tests/fuzz/exif_tiff_decode_libfuzzer.cc
  )
  openmeta_apply_target_settings(openmeta_fuzz_exif_tiff_decode)
  openmeta_apply_core_deps(openmeta_fuzz_exif_tiff_decode)
  openmeta_apply_libfuzzer_stdlib(openmeta_fuzz_exif_tiff_decode)

  target_compile_options(
    openmeta_fuzz_exif_tiff_decode
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
  target_link_options(
    openmeta_fuzz_exif_tiff_decode
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )

  add_executable(openmeta_fuzz_metadata_transfer
    ${OPENMETA_SOURCES}
    tests/fuzz/metadata_transfer_libfuzzer.cc
  )
  openmeta_apply_target_settings(openmeta_fuzz_metadata_transfer)
  openmeta_apply_core_deps(openmeta_fuzz_metadata_transfer)
  openmeta_apply_libfuzzer_stdlib(openmeta_fuzz_metadata_transfer)

  target_compile_options(
    openmeta_fuzz_metadata_transfer
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
  target_link_options(
    openmeta_fuzz_metadata_transfer
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )

  if(OPENMETA_EXPAT_FOUND)
    add_executable(openmeta_fuzz_xmp_decode
      ${OPENMETA_SOURCES}
      tests/fuzz/xmp_decode_libfuzzer.cc
    )
    openmeta_apply_target_settings(openmeta_fuzz_xmp_decode)
    openmeta_apply_core_deps(openmeta_fuzz_xmp_decode)
    openmeta_apply_libfuzzer_stdlib(openmeta_fuzz_xmp_decode)

    target_compile_options(
      openmeta_fuzz_xmp_decode
      PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(
      openmeta_fuzz_xmp_decode
      PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
  endif()

  add_executable(openmeta_fuzz_container_payload
    ${OPENMETA_SOURCES}
    tests/fuzz/container_payload_libfuzzer.cc
  )
  openmeta_apply_target_settings(openmeta_fuzz_container_payload)
  openmeta_apply_core_deps(openmeta_fuzz_container_payload)
  openmeta_apply_libfuzzer_stdlib(openmeta_fuzz_container_payload)

  target_compile_options(
    openmeta_fuzz_container_payload
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
  target_link_options(
    openmeta_fuzz_container_payload
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
endif()

include(CMakePackageConfigHelpers)

set(_openmeta_install_targets "")
if(TARGET openmeta_static)
  list(APPEND _openmeta_install_targets openmeta_static)
endif()
if(TARGET openmeta_shared)
  list(APPEND _openmeta_install_targets openmeta_shared)
endif()

install(
  TARGETS ${_openmeta_install_targets}
  EXPORT OpenMetaTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/src/include/openmeta"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

if(TARGET metaread)
  install(TARGETS metaread RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
if(TARGET metavalidate)
  install(TARGETS metavalidate RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
if(TARGET metadump)
  install(TARGETS metadump RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
if(TARGET metatransfer)
  install(TARGETS metatransfer RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
if(TARGET thumdump)
  install(TARGETS thumdump RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if(OPENMETA_BUILD_DOCS OR OPENMETA_BUILD_SPHINX_DOCS)
  set(_openmeta_docs_root_dir "${CMAKE_CURRENT_BINARY_DIR}/docs")
  set(_openmeta_doxygen_out_dir "${_openmeta_docs_root_dir}/doxygen")
  set(_openmeta_doxygen_cfg "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaDoxyfile")

  set(_openmeta_doxygen_inputs
    "${PROJECT_SOURCE_DIR}/README.md"
    "${PROJECT_SOURCE_DIR}/src/include"
    "${PROJECT_SOURCE_DIR}/docs"
  )
  string(JOIN " " OPENMETA_DOXYGEN_INPUT ${_openmeta_doxygen_inputs})
  set(OPENMETA_DOXYGEN_OUTPUT_DIR "${_openmeta_doxygen_out_dir}")
  set(OPENMETA_DOXYGEN_MAINPAGE "${PROJECT_SOURCE_DIR}/README.md")

  set(OPENMETA_DOXYGEN_GENERATE_XML "NO")
  if(OPENMETA_BUILD_SPHINX_DOCS)
    set(OPENMETA_DOXYGEN_GENERATE_XML "YES")
  endif()

  set(_openmeta_doxygen_xml_dir "${_openmeta_doxygen_out_dir}/xml")
  file(TO_CMAKE_PATH "${_openmeta_doxygen_xml_dir}" OPENMETA_DOXYGEN_XML_DIR)

  configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/Doxyfile.in"
    "${_openmeta_doxygen_cfg}"
    @ONLY
  )

  add_custom_target(openmeta_docs
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_openmeta_doxygen_out_dir}"
    COMMAND "${DOXYGEN_EXECUTABLE}" "${_openmeta_doxygen_cfg}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Generating OpenMeta API documentation (Doxygen)"
    VERBATIM
  )

  if(OPENMETA_BUILD_DOCS)
    set(_openmeta_install_docs_script "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaInstallDocs.cmake")
    set(OPENMETA_DOXYGEN_INSTALL_SUBDIR "")
    if(OPENMETA_BUILD_SPHINX_DOCS)
      set(OPENMETA_DOXYGEN_INSTALL_SUBDIR "doxygen")
    endif()
    configure_file(
      "${PROJECT_SOURCE_DIR}/cmake/OpenMetaInstallDocs.cmake.in"
      "${_openmeta_install_docs_script}"
      @ONLY
    )
    install(SCRIPT "${_openmeta_install_docs_script}")
  endif()

  if(OPENMETA_BUILD_SPHINX_DOCS)
    find_package(Python REQUIRED COMPONENTS Interpreter)

    execute_process(
      COMMAND "${Python_EXECUTABLE}" -c "import sphinx, breathe"
      RESULT_VARIABLE _openmeta_sphinx_import_res
      OUTPUT_QUIET ERROR_QUIET
    )
    if(NOT _openmeta_sphinx_import_res EQUAL 0)
      message(FATAL_ERROR
        "OPENMETA_BUILD_SPHINX_DOCS requires Python packages 'sphinx' and 'breathe' for: ${Python_EXECUTABLE}. "
        "Install them (see docs/requirements.txt) and reconfigure.")
    endif()
    set(OPENMETA_SPHINX_FOUND ON)
    set(OPENMETA_BREATHE_FOUND ON)

    set(_openmeta_sphinx_source_dir "${PROJECT_SOURCE_DIR}/docs/sphinx")
    set(_openmeta_sphinx_conf_dir "${CMAKE_CURRENT_BINARY_DIR}/docs/sphinx")
    set(_openmeta_sphinx_out_dir "${_openmeta_docs_root_dir}/html")
    file(MAKE_DIRECTORY "${_openmeta_sphinx_conf_dir}")

    configure_file(
      "${PROJECT_SOURCE_DIR}/docs/sphinx/conf.py.in"
      "${_openmeta_sphinx_conf_dir}/conf.py"
      @ONLY
    )

    add_custom_target(openmeta_docs_sphinx
      DEPENDS openmeta_docs
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${_openmeta_sphinx_out_dir}"
      COMMAND "${Python_EXECUTABLE}" -m sphinx -b html
              -c "${_openmeta_sphinx_conf_dir}"
              "${_openmeta_sphinx_source_dir}"
              "${_openmeta_sphinx_out_dir}"
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      COMMENT "Generating OpenMeta documentation (Sphinx)"
      VERBATIM
    )

    set(_openmeta_install_sphinx_docs_script "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaInstallSphinxDocs.cmake")
    configure_file(
      "${PROJECT_SOURCE_DIR}/cmake/OpenMetaInstallSphinxDocs.cmake.in"
      "${_openmeta_install_sphinx_docs_script}"
      @ONLY
    )
    install(SCRIPT "${_openmeta_install_sphinx_docs_script}")
  endif()
endif()

set(_openmeta_pkg_dir "${CMAKE_INSTALL_LIBDIR}/cmake/OpenMeta")

set(OPENMETA_CONFIG_HAS_STATIC_TARGET 0)
if(TARGET openmeta_static)
  set(OPENMETA_CONFIG_HAS_STATIC_TARGET 1)
endif()

set(OPENMETA_CONFIG_USE_LIBCXX 0)
if(OPENMETA_USE_LIBCXX)
  set(OPENMETA_CONFIG_USE_LIBCXX 1)
endif()

install(
  EXPORT OpenMetaTargets
  NAMESPACE OpenMeta::
  FILE OpenMetaTargets.cmake
  DESTINATION "${_openmeta_pkg_dir}"
)

install(
  FILES
    "${PROJECT_SOURCE_DIR}/cmake/FindBrotli.cmake"
  DESTINATION "${_openmeta_pkg_dir}"
)

configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/OpenMetaConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaConfig.cmake"
  INSTALL_DESTINATION "${_openmeta_pkg_dir}"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/OpenMetaConfigVersion.cmake"
  DESTINATION "${_openmeta_pkg_dir}"
)

if(OPENMETA_BUILD_WHEEL)
  install(SCRIPT "${_openmeta_install_wheel_script}")
endif()

openmeta_print_config_summary()
