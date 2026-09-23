# =============================================================================
# XSigma
# Valgrind Memory Checking Configuration Module

# This module configures Valgrind memory checking for CTest. All Valgrind options and settings are
# centralized here. Provides comprehensive memory leak detection and error tracking.
#
# Usage in each module's CMakeLists.txt:
#
# option(XXX_ENABLE_VALGRIND "Execute XXX test suite with Valgrind" OFF)
# mark_as_advanced(XXX_ENABLE_VALGRIND) ... if(XXX_ENABLE_VALGRIND) include(valgrind) endif() ...
# if(XXX_ENABLE_VALGRIND AND XXX_ENABLE_TESTING) xsigma_apply_valgrind_timeouts() endif()

message(STATUS "Configuring Valgrind memory checking...")

# =============================================================================
# Platform
# Detection and Compatibility Checks

cmake_host_system_information(RESULT PLATFORM_ARCH QUERY OS_PLATFORM)
message(STATUS "Platform architecture: ${PLATFORM_ARCH}")

if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
  message(WARNING "Valgrind does not support Apple Silicon (ARM64) architecture")
  message(WARNING "Consider using sanitizers instead:")
  message(WARNING "  AddressSanitizer: -DXXX_ENABLE_SANITIZER=ON")
  message(WARNING "  LeakSanitizer:    -DXXX_SANITIZER_TYPE=leak")
  message(
    WARNING "Continuing with Valgrind configuration (will fail if Valgrind is not installed)..."
  )
endif()

# =============================================================================
# Find Valgrind
# Executable

find_program(CMAKE_MEMORYCHECK_COMMAND valgrind)

if(NOT CMAKE_MEMORYCHECK_COMMAND)
  message(FATAL_ERROR "Valgrind not found! Please install Valgrind or set XXX_ENABLE_VALGRIND=OFF")
endif()

message(STATUS "Found Valgrind: ${CMAKE_MEMORYCHECK_COMMAND}")

execute_process(
  COMMAND ${CMAKE_MEMORYCHECK_COMMAND} --version OUTPUT_VARIABLE VALGRIND_VERSION_OUTPUT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "Valgrind version: ${VALGRIND_VERSION_OUTPUT}")

# =============================================================================
# Valgrind Timeout
# Configuration

set(PROJECT_VALGRIND_TIMEOUT_MULTIPLIER 20
    CACHE STRING "Timeout multiplier for tests running under Valgrind"
)
set(CTEST_TEST_TIMEOUT 1800 CACHE STRING
                                  "Global timeout in seconds for CTest when running with Valgrind"
)

message(STATUS "Valgrind timeout multiplier: ${PROJECT_VALGRIND_TIMEOUT_MULTIPLIER}x")
message(STATUS "Global CTest timeout: ${CTEST_TEST_TIMEOUT} seconds")

# =============================================================================
# Valgrind Command
# Options

# CTest's MemCheck driver writes its own per-test log file (MemoryChecker.<N>.log),
# so we deliberately do NOT set Valgrind --log-file/--xml-file here.
#
# Also: treat only *definite* leaks as defects, to avoid "still reachable" noise
# (common in libstdc++/gtest/static initialization) being reported as leaks.
set(CMAKE_MEMORYCHECK_COMMAND_OPTIONS
    "--tool=memcheck" "--leak-check=full" "--show-leak-kinds=definite"
    "--errors-for-leak-kinds=definite" "--track-origins=yes" "--track-fds=yes" "--num-callers=50"
    "--trace-children=yes" "--error-exitcode=1" "--gen-suppressions=all"
)

# Wire options into CTest/DartConfiguration.tcl (used by `ctest -T memcheck`).
# These must be CACHE entries for CTest to pick them up during `include(CTest)`.
set(CTEST_MEMORYCHECK_COMMAND "${CMAKE_MEMORYCHECK_COMMAND}" CACHE FILEPATH
                                                                   "CTest memcheck command" FORCE
)
string(JOIN " " _ctest_memcheck_opts ${CMAKE_MEMORYCHECK_COMMAND_OPTIONS})
set(CTEST_MEMORYCHECK_COMMAND_OPTIONS "${_ctest_memcheck_opts}"
    CACHE STRING "CTest memcheck command options" FORCE
)

# =============================================================================
# Suppression File
# Configuration

set(CTEST_MEMORYCHECK_SUPPRESSIONS_FILE
    "${PROJECT_SOURCE_DIR}/Scripts/suppressions/valgrind_suppression.txt"
)

if(EXISTS "${CTEST_MEMORYCHECK_SUPPRESSIONS_FILE}")
  message(STATUS "Using Valgrind suppression file: ${CTEST_MEMORYCHECK_SUPPRESSIONS_FILE}")
  list(APPEND CMAKE_MEMORYCHECK_COMMAND_OPTIONS
       "--suppressions=${CTEST_MEMORYCHECK_SUPPRESSIONS_FILE}"
  )
  set(CTEST_MEMORYCHECK_COMMAND_OPTIONS
      "${CTEST_MEMORYCHECK_COMMAND_OPTIONS} --suppressions=${CTEST_MEMORYCHECK_SUPPRESSIONS_FILE}"
      CACHE STRING "CTest memcheck command options" FORCE
  )
else()
  message(WARNING "Valgrind suppression file not found: ${CTEST_MEMORYCHECK_SUPPRESSIONS_FILE}")
  message(WARNING "Consider creating a suppression file to filter known false positives")
endif()

# =============================================================================
# Summary

set(memcheck_command "${CMAKE_MEMORYCHECK_COMMAND}")
foreach(opt ${CMAKE_MEMORYCHECK_COMMAND_OPTIONS})
  list(APPEND memcheck_command ${opt})
endforeach()

message(STATUS "Valgrind command: ${memcheck_command}")
message(STATUS "Valgrind configuration complete")
message(STATUS "Use 'ctest -T memcheck' to run tests with Valgrind")

# =============================================================================
# Helper Function:
# Apply Valgrind Timeouts to Tests
#
# Call after all tests are registered in the module. Multiplies existing timeouts by
# PROJECT_VALGRIND_TIMEOUT_MULTIPLIER.

function(xsigma_apply_valgrind_timeouts)
  get_property(all_tests DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY TESTS)

  foreach(test_name ${all_tests})
    get_test_property(${test_name} TIMEOUT current_timeout)

    if(current_timeout)
      math(EXPR new_timeout "${current_timeout} * ${PROJECT_VALGRIND_TIMEOUT_MULTIPLIER}")
      set_tests_properties(${test_name} PROPERTIES TIMEOUT ${new_timeout})
      message(STATUS "  ${test_name}: timeout ${current_timeout}s -> ${new_timeout}s")
    else()
      set_tests_properties(${test_name} PROPERTIES TIMEOUT ${CTEST_TEST_TIMEOUT})
      message(STATUS "  ${test_name}: timeout not set, using global ${CTEST_TEST_TIMEOUT}s")
    endif()
  endforeach()

  message(STATUS "Applied Valgrind timeout multiplier to registered tests")
endfunction()
