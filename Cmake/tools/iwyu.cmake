# =============================================================================
# XSigma
# Include-What-You-Use (IWYU) Configuration Module

# This module configures include-what-you-use for analyzing include dependencies. It detects
# unnecessary includes and suggests improvements to include structure.

# Include guard to prevent multiple inclusions
include_guard(GLOBAL)

find_program(
  IWYU_EXECUTABLE
  NAMES include-what-you-use iwyu
  PATHS "C:/Program Files (x86)/include-what-you-use/bin"
        "C:/Program Files/include-what-you-use/bin"
  PATH_SUFFIXES bin
)

if(NOT IWYU_EXECUTABLE)
  message(
    FATAL_ERROR
      "IWYU requested but not found!

Please install include-what-you-use:

  - Ubuntu/Debian: sudo apt-get install iwyu
  - CentOS/RHEL/Fedora: sudo dnf install iwyu
  - macOS: brew install include-what-you-use
  - Windows: Download from https://include-what-you-use.org/
  - Manual: Build from https://github.com/include-what-you-use/include-what-you-use

Or set XXX_ENABLE_IWYU=OFF to disable include analysis"
  )
else()
  message(STATUS "IWYU found: ${IWYU_EXECUTABLE}")

  # Check if mapping file exists
  set(IWYU_MAPPING_FILE "${PROJECT_SOURCE_DIR}/Scripts/iwyu/iwyu_exclusion.imp")
  if(EXISTS "${IWYU_MAPPING_FILE}")
    message(STATUS "Using IWYU mapping file: ${IWYU_MAPPING_FILE}")
  else()
    message(WARNING "IWYU mapping file not found: ${IWYU_MAPPING_FILE}")
    set(IWYU_MAPPING_FILE "")
  endif()

  # Locate IWYU's built-in mapping files (ship alongside the executable)
  get_filename_component(_iwyu_bin_dir "${IWYU_EXECUTABLE}" DIRECTORY)
  get_filename_component(_iwyu_prefix "${_iwyu_bin_dir}" DIRECTORY)
  set(_iwyu_share "${_iwyu_prefix}/share/include-what-you-use")
  # Also try the libexec layout used by Homebrew (bin is a symlink into libexec/bin)
  if(NOT EXISTS "${_iwyu_share}")
    get_filename_component(_iwyu_real_exec "${IWYU_EXECUTABLE}" REALPATH)
    get_filename_component(_iwyu_real_bin "${_iwyu_real_exec}" DIRECTORY)
    get_filename_component(_iwyu_real_prefix "${_iwyu_real_bin}" DIRECTORY)
    set(_iwyu_share "${_iwyu_real_prefix}/share/include-what-you-use")
  endif()

  # Load libcxx.imp for libc++ internal __<module>/ header redirects. stl.c.headers.imp is
  # intentionally excluded: it marks C headers (e.g. <stdint.h>) as "public", which conflicts with
  # our "private" mappings that redirect them to <cstdint> etc.
  set(IWYU_BUILTIN_MAPPING_FILES "")
  foreach(_builtin_map IN ITEMS libcxx.imp)
    if(EXISTS "${_iwyu_share}/${_builtin_map}")
      list(APPEND IWYU_BUILTIN_MAPPING_FILES "${_iwyu_share}/${_builtin_map}")
      message(STATUS "IWYU built-in mapping: ${_iwyu_share}/${_builtin_map}")
    endif()
  endforeach()

  # Create IWYU log directory
  set(IWYU_LOG_DIR "${CMAKE_BINARY_DIR}/iwyu_logs")
  file(MAKE_DIRECTORY "${IWYU_LOG_DIR}")

  # Set IWYU log file path — CACHE INTERNAL so it survives include_guard(GLOBAL) across all modules
  # that call xsigma_apply_iwyu from their own scope.
  set(IWYU_LOG_FILE "${CMAKE_BINARY_DIR}/iwyu.log" CACHE INTERNAL "IWYU log file path")

  # Prepare IWYU arguments with crash-resistant settings
  set(PROJECT_IWYU_ARGS
      "-Xiwyu"
      "--cxx17ns"
      "-Xiwyu"
      "--max_line_length=120"
      "-Xiwyu"
      "--verbose=1"
      "-Xiwyu"
      "--comment_style=short"
      "-Xiwyu"
      "--error=0"
      "-Xiwyu"
      "--no_fwd_decls"
      "-Xiwyu"
      "--quoted_includes_first"
      "-Xiwyu"
      "--transitive_includes_only"
  )

  # Add system root for macOS to help IWYU find system headers
  if(APPLE)
    execute_process(
      COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE MACOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE
                                                                   ERROR_QUIET
    )
    if(MACOS_SDK_PATH)
      list(APPEND PROJECT_IWYU_ARGS "-isysroot" "${MACOS_SDK_PATH}")
      message(STATUS "IWYU using macOS SDK: ${MACOS_SDK_PATH}")
    endif()
  endif()

  if(IWYU_MAPPING_FILE)
    list(PREPEND PROJECT_IWYU_ARGS "-Xiwyu" "--mapping_file=${IWYU_MAPPING_FILE}")
  endif()

  # Prepend built-in mapping files so the custom file's rules take precedence
  foreach(_builtin_map IN LISTS IWYU_BUILTIN_MAPPING_FILES)
    list(PREPEND PROJECT_IWYU_ARGS "-Xiwyu" "--mapping_file=${_builtin_map}")
  endforeach()

  # Create configure detector script path
  set(CONFIGURE_DETECTOR_SCRIPT "${PROJECT_SOURCE_DIR}/Scripts/iwyu/iwyu_configure_detector.py")

  message(STATUS "IWYU will analyze include dependencies for XSigma targets only")
  message(STATUS "IWYU analysis will be logged to: ${IWYU_LOG_FILE}")

  # Run configure header detection analysis
  if(EXISTS "${CONFIGURE_DETECTOR_SCRIPT}")
    message(STATUS "Running XSigma configure header detection...")
    execute_process(
      COMMAND
        ${CMAKE_COMMAND} -E env python "${CONFIGURE_DETECTOR_SCRIPT}"
        "${PROJECT_SOURCE_DIR}/Library" --log-file "${PROJECT_BINARY_DIR}/configure_detection.log"
        --report-file "${PROJECT_BINARY_DIR}/configure_analysis_report.txt" --recursive
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      RESULT_VARIABLE CONFIGURE_DETECTION_RESULT
      OUTPUT_VARIABLE CONFIGURE_DETECTION_OUTPUT
      ERROR_VARIABLE CONFIGURE_DETECTION_ERROR
    )

    if(CONFIGURE_DETECTION_RESULT EQUAL 0)
      message(STATUS "Configure header detection completed successfully")
    else()
      message(
        WARNING
          "Configure header detection found issues - check ${PROJECT_BINARY_DIR}/configure_analysis_report.txt"
      )
    endif()

    message(STATUS "Configure analysis report: ${PROJECT_BINARY_DIR}/configure_analysis_report.txt")
  else()
    message(WARNING "Configure detector script not found: ${CONFIGURE_DETECTOR_SCRIPT}")
  endif()
endif()

# Function to create IWYU wrapper script for enhanced logging. Optional ARGN items are extra flags
# appended to IWYU args (e.g. per-language -D defines).
function(xsigma_create_iwyu_wrapper target_name lang)
  set(WRAPPER_SCRIPT "${PROJECT_BINARY_DIR}/iwyu_wrapper_${target_name}_${lang}.cmake")

  # Build per-item list(APPEND) calls to safely embed all IWYU args in the generated script
  set(IWYU_ARGS_INIT_CODE "set(IWYU_EXTRA_ARGS)\n")
  foreach(arg IN LISTS PROJECT_IWYU_ARGS)
    string(APPEND IWYU_ARGS_INIT_CODE "list(APPEND IWYU_EXTRA_ARGS \"${arg}\")\n")
  endforeach()
  foreach(arg IN LISTS ARGN)
    string(APPEND IWYU_ARGS_INIT_CODE "list(APPEND IWYU_EXTRA_ARGS \"${arg}\")\n")
  endforeach()

  # Create wrapper script that logs IWYU output
  file(
    WRITE "${WRAPPER_SCRIPT}"
    "
# IWYU Wrapper Script for ${target_name} (${lang})
# Invoked by CMake as: cmake -P <this_script> <source_file> [compile_flags...]
# IWYU writes its include suggestions to stderr, which we capture to iwyu.log.

set(IWYU_EXECUTABLE \"${IWYU_EXECUTABLE}\")
set(IWYU_LOG_FILE \"${IWYU_LOG_FILE}\")

# Embed IWYU-specific flags (generated at configure time)
${IWYU_ARGS_INIT_CODE}
# Collect source file and compile flags passed by CMake (start at argv3)
set(COMPILE_ARGS)
set(_i 3)
while(_i LESS CMAKE_ARGC)
  list(APPEND COMPILE_ARGS \"\${CMAKE_ARGV\${_i}}\")
  math(EXPR _i \"\${_i} + 1\")
endwhile()

# Execute IWYU — suggestions are written to stderr
execute_process(
  COMMAND \"\${IWYU_EXECUTABLE}\" \${IWYU_EXTRA_ARGS} \${COMPILE_ARGS}
  OUTPUT_VARIABLE IWYU_OUTPUT
  ERROR_VARIABLE IWYU_SUGGESTIONS
  RESULT_VARIABLE IWYU_RESULT
)

# Append suggestions to iwyu.log
if(IWYU_SUGGESTIONS)
  file(APPEND \"\${IWYU_LOG_FILE}\" \"\${IWYU_SUGGESTIONS}\")
endif()
if(IWYU_OUTPUT)
  file(APPEND \"\${IWYU_LOG_FILE}\" \"\${IWYU_OUTPUT}\")
endif()
"
  )
endfunction()

# Function to apply IWYU to a target
function(xsigma_apply_iwyu target_name)
  if(NOT IWYU_EXECUTABLE)
    return()
  endif()

  # Skip third-party targets
  get_target_property(target_source_dir ${target_name} SOURCE_DIR)
  if(target_source_dir MATCHES ".*/ThirdParty/.*" OR target_source_dir MATCHES ".*/third_party/.*"
     OR target_source_dir MATCHES ".*/3rdparty/.*"
  )
    message(STATUS "Skipping IWYU for third-party target: ${target_name}")
    return()
  endif()

  # Initialize the log file with basic header information
  if(NOT EXISTS "${IWYU_LOG_FILE}")
    file(WRITE "${IWYU_LOG_FILE}" "# IWYU Analysis Log for XSigma Project\n")
    file(APPEND "${IWYU_LOG_FILE}" "# Generated by CMake IWYU integration\n")
    file(APPEND "${IWYU_LOG_FILE}" "# IWYU executable: ${IWYU_EXECUTABLE}\n")
    file(APPEND "${IWYU_LOG_FILE}" "# Mapping file: ${IWYU_MAPPING_FILE}\n")
    file(APPEND "${IWYU_LOG_FILE}" "# Use Scripts/run_iwyu_analysis.py for detailed analysis\n")
    file(APPEND "${IWYU_LOG_FILE}" "\n")
  endif()

  # Generate per-target, per-language wrapper scripts that capture IWYU output to iwyu.log
  xsigma_create_iwyu_wrapper(
    ${target_name} CXX "-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH"
    "-D_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING"
  )
  xsigma_create_iwyu_wrapper(${target_name} C "-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH")

  # Apply IWYU via the logging wrapper (cmake -P; IWYU suggestions go to iwyu.log via stderr)
  set_target_properties(
    ${target_name}
    PROPERTIES CXX_INCLUDE_WHAT_YOU_USE
               "${CMAKE_COMMAND};-P;${PROJECT_BINARY_DIR}/iwyu_wrapper_${target_name}_CXX.cmake"
               C_INCLUDE_WHAT_YOU_USE
               "${CMAKE_COMMAND};-P;${PROJECT_BINARY_DIR}/iwyu_wrapper_${target_name}_C.cmake"
  )

  message(STATUS "Applied IWYU to target: ${target_name} (logging to ${IWYU_LOG_FILE})")
endfunction()
