# Standalone Graph; reusable through add_subdirectory or FetchContent.
cmake_minimum_required(VERSION 3.22)
cmake_policy(SET CMP0069 NEW)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(Graph VERSION 1.0.0 LANGUAGES CXX C)
  include(CTest)
  enable_testing()
  option(BUILD_SHARED_LIBS "Build shared libraries" ON)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()
list(PREPEND CMAKE_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/Cmake/tools"
  "${CMAKE_CURRENT_SOURCE_DIR}/Cmake/flags"
  "${CMAKE_CURRENT_SOURCE_DIR}/Cmake/packages")
include("${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/CMakeLists.txt")
add_subdirectory(Graph)
