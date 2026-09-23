# =============================================================================
# XSigma Helper
# Macros Module

# This module provides utility macros for common CMake operations used throughout the XSigma build
# system, including source file filtering and organization.

# Include guard to prevent multiple inclusions
include_guard(GLOBAL)

macro(xsigma_module_create_filters name)
  file(
    GLOB_RECURSE _source_list
    LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/*.inl"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cu"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cuh"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hxx"
  )

  set(includ_dirs)
  foreach(_source IN ITEMS ${_source_list})
    get_filename_component(_source_path "${_source}" PATH)
    string(REPLACE "${name}" "" _group_path "${_source_path}")
    string(REPLACE "/" "\\" _group_path "${_group_path}")
    source_group("${_group_path}" FILES "${_source}")
    string(REPLACE "\\" "/" _group_path "${_group_path}")
    list(APPEND includ_dirs "${name}${_group_path}")
  endforeach()
  list(REMOVE_DUPLICATES includ_dirs)
  # include_directories(${includ_dirs})
endmacro()

# Use Icecream (icecc) as the compiler launcher for a single target when XXX_ENABLE_ICECC is ON.
function(xsigma_target_optional_icecc target_name enable_var)
  if(NOT ${enable_var})
    return()
  endif()
  if(NOT TARGET "${target_name}")
    message(FATAL_ERROR "xsigma_target_optional_icecc: target '${target_name}' does not exist")
  endif()
  if(NOT XSIGMA_ICECC_EXECUTABLE)
    find_program(XSIGMA_ICECC_EXECUTABLE NAMES icecc)
  endif()
  if(XSIGMA_ICECC_EXECUTABLE)
    set_property(
      TARGET "${target_name}" PROPERTY CXX_COMPILER_LAUNCHER "${XSIGMA_ICECC_EXECUTABLE}"
    )
    set_property(TARGET "${target_name}" PROPERTY C_COMPILER_LAUNCHER "${XSIGMA_ICECC_EXECUTABLE}")
    message(STATUS "Icecream compiler launcher for ${target_name}: ${XSIGMA_ICECC_EXECUTABLE}")
  else()
    message(STATUS "Icecream enabled for ${target_name} but icecc was not found in PATH")
  endif()
endfunction()

macro(xsigma_module_remove_underscores name_in name_out)
  string(REPLACE "_" ";" name_splited ${name_in})
  set(the_list "")
  foreach(name IN ITEMS ${name_splited})
    string(SUBSTRING ${name} 0 1 first_letter)
    string(TOUPPER ${first_letter} first_letter)
    string(REGEX REPLACE "^.(.*)" "${first_letter}\\1" result "${name}")
    list(APPEND the_list ${result})
  endforeach()
  string(REPLACE ";" "" name_splited_merge ${the_list})
  string(SUBSTRING ${name_splited_merge} 0 1 first_letter)
  string(TOLOWER ${first_letter} first_letter)
  string(REGEX REPLACE "^.(.*)" "${first_letter}\\1" ${name_out} "${name_splited_merge}")
endmacro()
