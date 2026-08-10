cmake_minimum_required(VERSION 3.20)

project(OpenMetaSharedLibraryConsumer LANGUAGES CXX)

find_package(OpenMeta CONFIG REQUIRED)

if(NOT TARGET OpenMeta::openmeta_shared)
  message(FATAL_ERROR "OpenMeta shared target was not installed")
endif()

add_executable(openmeta_shared_consumer main.cc)
target_link_libraries(openmeta_shared_consumer PRIVATE OpenMeta::openmeta_shared)

file(GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/openmeta_shared_consumer_path_$<CONFIG>.txt"
  CONTENT "$<TARGET_FILE:openmeta_shared_consumer>\n")
