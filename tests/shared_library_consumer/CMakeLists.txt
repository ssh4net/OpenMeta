cmake_minimum_required(VERSION 3.20)

project(OpenMetaSharedLibraryConsumer LANGUAGES CXX)

find_package(OpenMeta 0.6 CONFIG REQUIRED)

if(NOT OpenMeta_ABI_VERSION STREQUAL "4")
  message(FATAL_ERROR "OpenMeta 0.6 must publish ABI 4")
endif()

if(NOT TARGET OpenMeta::openmeta_shared)
  message(FATAL_ERROR "OpenMeta shared target was not installed")
endif()

add_executable(openmeta_shared_consumer main.cc)
target_link_libraries(openmeta_shared_consumer PRIVATE OpenMeta::openmeta_shared)

file(GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/openmeta_shared_consumer_path_$<CONFIG>.txt"
  CONTENT "$<TARGET_FILE:openmeta_shared_consumer>\n")
