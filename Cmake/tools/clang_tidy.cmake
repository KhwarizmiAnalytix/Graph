# =============================================================================
# XSigma
# Clang-Tidy Static Analysis Configuration Module

# This module configures clang-tidy for static code analysis and automated fixes. It enables code
# quality checks and optional automatic error correction.

# Include guard to prevent multiple inclusions
include_guard(GLOBAL)

# XSigma ClangTidy Configuration
find_program(CLANG_TIDY_PATH NAMES clang-tidy DOC "Path to clang-tidy.")

if(NOT CLANG_TIDY_PATH)
  message(FATAL_ERROR "Could not find clang-tidy.")
endif()
set(CLANG_TIDY_FOUND ON CACHE BOOL "Found clang-tidy.")
mark_as_advanced(CLANG_TIDY_FOUND)

# --exclude-header-filter landed in LLVM 19. Ubuntu 24.04's apt clang-tidy is 18
# and rejects the flag ("Unknown command line argument"), which then makes
# CMake's __run_co_compile fail with "cannot specify -o when generating
# multiple output files". --header-filter already limits reports to
# Library|Cmake|Tools|Examples, so skipping the exclude filter on older
# tidy still keeps ThirdParty out of the report.
execute_process(
  COMMAND "${CLANG_TIDY_PATH}" --version
  OUTPUT_VARIABLE _clang_tidy_version_out
  ERROR_VARIABLE _clang_tidy_version_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if("${_clang_tidy_version_out}" MATCHES "version ([0-9]+)")
  set(CLANG_TIDY_VERSION_MAJOR "${CMAKE_MATCH_1}")
else()
  set(CLANG_TIDY_VERSION_MAJOR 0)
endif()
set(CLANG_TIDY_VERSION_MAJOR "${CLANG_TIDY_VERSION_MAJOR}" CACHE STRING
                                                                 "Major version of clang-tidy"
)
mark_as_advanced(CLANG_TIDY_VERSION_MAJOR)
message(STATUS "clang-tidy: ${CLANG_TIDY_PATH} (major ${CLANG_TIDY_VERSION_MAJOR})")

# enable_fix — pass the caller's XXX_ENABLE_FIX variable value as the second argument. WARNING: fix
# mode modifies source files. Use with caution in version control.
function(xsigma_target_clang_tidy target_name enable_fix)
  set(XSIGMA_CLANG_TIDY_HEADER_FILTER "^${PROJECT_SOURCE_DIR}/(Library|Cmake|Tools|Examples)/.*")
  set(XSIGMA_CLANG_TIDY_EXCLUDE_FILTER ".*/(ThirdParty|third_party|3rdparty|third-party)/.*")

  set(_tidy_args "${CLANG_TIDY_PATH}")
  if(enable_fix)
    message(WARNING "Applying clang-tidy fix to target: ${target_name}")
    list(APPEND _tidy_args -fix-errors -fix)
  endif()
  list(APPEND _tidy_args "-warnings-as-errors=*"
       "--header-filter=${XSIGMA_CLANG_TIDY_HEADER_FILTER}"
  )
  if(CLANG_TIDY_VERSION_MAJOR GREATER_EQUAL 19)
    list(APPEND _tidy_args "--exclude-header-filter=${XSIGMA_CLANG_TIDY_EXCLUDE_FILTER}")
  endif()

  set_target_properties(
    ${target_name} PROPERTIES C_CLANG_TIDY "${_tidy_args}" CXX_CLANG_TIDY "${_tidy_args}"
  )
endfunction()

function(disable_clang_tidy_for_target target_name)
  set_target_properties(${target_name} PROPERTIES C_CLANG_TIDY "" CXX_CLANG_TIDY "")
endfunction()
