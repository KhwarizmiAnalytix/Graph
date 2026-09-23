# =============================================================================
# Graph Third-Party Dependency Management
# =============================================================================
# This file manages all third-party dependencies for XSigma, handling both bundled and
# external (system-installed) libraries via the XSIGMA_ENABLE_EXTERNAL flag. The
# add_third_party_library() function automatically populates XSIGMA_DEPENDENCY_LIBS and
# XSIGMA_DEPENDENCY_INCLUDE_DIRS, which are propagated to the main build system.
# =============================================================================

cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0069 NEW)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# External Library Usage Flag Controls whether external copies of third-party libraries are used by
# default. When enabled, prefers external library versions over bundled versions.
option(XSIGMA_ENABLE_EXTERNAL "Use external copies of third party libraries by default" ON)
mark_as_advanced(XSIGMA_ENABLE_EXTERNAL)

# Configure Visual Studio folder structure to match file system
set_property(GLOBAL PROPERTY USE_FOLDERS ON)


# Path to this ThirdParty directory — used by library modules that call
# add_third_party_library() with absolute SOURCE_DIR / INCLUDE_DIR paths.
set(XSIGMA_THIRDPARTY_DIR "${CMAKE_CURRENT_LIST_DIR}")

# =============================================================================
# Helper Functions for Third-Party Library Management
# =============================================================================

# -----------------------------------------------------------------------------
# _filter_flags_internal: Remove specified flags from a flag string
# -----------------------------------------------------------------------------
# Internal helper to filter out specific flags from a string of compiler/linker flags
#
# Parameters:
#   input_flags - The flag string to filter
#   flags_to_remove - List of flags to remove (can be regex patterns)
#   output_var - Variable name to store the filtered result
#
function(_filter_flags_internal input_flags flags_to_remove output_var)
    set(result "${input_flags}")

    foreach(flag_pattern ${flags_to_remove})
        # Remove the flag pattern from the string
        string(REGEX REPLACE "${flag_pattern}( |$)" "" result "${result}")
    endforeach()

    # Clean up multiple spaces
    string(REGEX REPLACE "  +" " " result "${result}")
    string(STRIP "${result}" result)

    set(${output_var} "${result}" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# _save_third_party_flags: Save and optionally filter compiler/linker flags
# -----------------------------------------------------------------------------
# Saves current CMAKE_CXX_FLAGS, CMAKE_C_FLAGS, and linker flags, then optionally
# filters out problematic flags that shouldn't be applied to third-party libraries
#
# Parameters:
#   name - Library name (for namespacing saved variables)
#   filter_flags - List of flag patterns to remove (optional)
#
function(_save_third_party_flags name filter_flags)
    # Save original flags to parent scope
    set(_SAVED_CMAKE_CXX_FLAGS_${name} "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
    set(_SAVED_CMAKE_C_FLAGS_${name} "${CMAKE_C_FLAGS}" PARENT_SCOPE)
    set(_SAVED_CMAKE_EXE_LINKER_FLAGS_${name} "${CMAKE_EXE_LINKER_FLAGS}" PARENT_SCOPE)
    set(_SAVED_CMAKE_SHARED_LINKER_FLAGS_${name} "${CMAKE_SHARED_LINKER_FLAGS}" PARENT_SCOPE)
    set(_SAVED_CMAKE_MODULE_LINKER_FLAGS_${name} "${CMAKE_MODULE_LINKER_FLAGS}" PARENT_SCOPE)
    set(_SAVED_CMAKE_STATIC_LINKER_FLAGS_${name} "${CMAKE_STATIC_LINKER_FLAGS}" PARENT_SCOPE)

    # If filter flags are provided, filter them out
    if(filter_flags)
        _filter_flags_internal("${CMAKE_CXX_FLAGS}" "${filter_flags}" filtered_cxx_flags)
        _filter_flags_internal("${CMAKE_C_FLAGS}" "${filter_flags}" filtered_c_flags)
        _filter_flags_internal("${CMAKE_EXE_LINKER_FLAGS}" "${filter_flags}" filtered_exe_linker_flags)
        _filter_flags_internal("${CMAKE_SHARED_LINKER_FLAGS}" "${filter_flags}" filtered_shared_linker_flags)
        _filter_flags_internal("${CMAKE_MODULE_LINKER_FLAGS}" "${filter_flags}" filtered_module_linker_flags)
        _filter_flags_internal("${CMAKE_STATIC_LINKER_FLAGS}" "${filter_flags}" filtered_static_linker_flags)

        # Apply filtered flags to parent scope
        set(CMAKE_CXX_FLAGS "${filtered_cxx_flags}" PARENT_SCOPE)
        set(CMAKE_C_FLAGS "${filtered_c_flags}" PARENT_SCOPE)
        set(CMAKE_EXE_LINKER_FLAGS "${filtered_exe_linker_flags}" PARENT_SCOPE)
        set(CMAKE_SHARED_LINKER_FLAGS "${filtered_shared_linker_flags}" PARENT_SCOPE)
        set(CMAKE_MODULE_LINKER_FLAGS "${filtered_module_linker_flags}" PARENT_SCOPE)
        set(CMAKE_STATIC_LINKER_FLAGS "${filtered_static_linker_flags}" PARENT_SCOPE)

        message(STATUS "Filtered flags for ${name}: removed ${filter_flags}")
    endif()
endfunction()

# -----------------------------------------------------------------------------
# _restore_third_party_flags: Restore original compiler/linker flags
# -----------------------------------------------------------------------------
# Restores the compiler and linker flags that were saved before building
# a third-party library
#
# Parameters:
#   name - Library name (for namespacing saved variables)
#
function(_restore_third_party_flags name)
    # Restore original flags to parent scope
    set(CMAKE_CXX_FLAGS "${_SAVED_CMAKE_CXX_FLAGS_${name}}" PARENT_SCOPE)
    set(CMAKE_C_FLAGS "${_SAVED_CMAKE_C_FLAGS_${name}}" PARENT_SCOPE)
    set(CMAKE_EXE_LINKER_FLAGS "${_SAVED_CMAKE_EXE_LINKER_FLAGS_${name}}" PARENT_SCOPE)
    set(CMAKE_SHARED_LINKER_FLAGS "${_SAVED_CMAKE_SHARED_LINKER_FLAGS_${name}}" PARENT_SCOPE)
    set(CMAKE_MODULE_LINKER_FLAGS "${_SAVED_CMAKE_MODULE_LINKER_FLAGS_${name}}" PARENT_SCOPE)
    set(CMAKE_STATIC_LINKER_FLAGS "${_SAVED_CMAKE_STATIC_LINKER_FLAGS_${name}}" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# _set_third_party_folder_properties: Set FOLDER property for all targets
# -----------------------------------------------------------------------------
# Automatically discovers all targets created by a third-party library and
# sets their FOLDER property for IDE organization. This function recursively
# traverses all subdirectories to find targets at any nesting level.
#
# IMPORTANT: CMake's BUILDSYSTEM_TARGETS property only returns targets defined
# directly in a directory, not in subdirectories. This function uses recursive
# traversal via the SUBDIRECTORIES property to discover ALL targets.
#
# Parameters:
#   name - Library name
#   binary_dir - Binary directory where the library was built
#
function(_set_third_party_folder_properties name binary_dir)
    # Helper function to recursively collect targets from a directory tree
    # This is necessary because BUILDSYSTEM_TARGETS only returns targets
    # defined directly in the specified directory, not in subdirectories
    function(_collect_targets_recursive dir collected_targets_var)
        # Get targets defined directly in this directory
        get_property(dir_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)

        if(dir_targets)
            list(APPEND local_targets ${dir_targets})
        endif()

        # Get all subdirectories in this directory
        get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)

        # Recursively process each subdirectory
        foreach(subdir ${subdirs})
            _collect_targets_recursive("${subdir}" local_targets)
        endforeach()

        # Return the collected targets to parent scope
        set(${collected_targets_var} "${local_targets}" PARENT_SCOPE)
    endfunction()

    # Collect all targets recursively starting from the binary directory
    _collect_targets_recursive("${binary_dir}" all_targets)

    if(all_targets)
        message(STATUS "Setting FOLDER property for ${name} targets: ${all_targets}")
        foreach(target ${all_targets})
            if(TARGET ${target})
                set_target_properties(${target} PROPERTIES FOLDER "ThirdParty/${name}")
            endif()
        endforeach()
    else()
        message(STATUS "No targets found for ${name} in ${binary_dir} (this may be normal for header-only libraries)")
    endif()
endfunction()

# -----------------------------------------------------------------------------
# xsigma_clang_windows_gtest_workaround
# -----------------------------------------------------------------------------
# Clang 22+ can crash (frontend ICE) compiling gtest-all.cc with global -gcodeview and -O3 on
# Windows. -g0 on the gtest targets only overrides -gcodeview for those TUs (same approach as
# optional per-TU workarounds for heavy test translation units). Call once after all Library/* trees that
# may add googletest have been processed (root CMakeLists.txt).
function(xsigma_clang_windows_gtest_workaround)
  if(NOT WIN32)
    return()
  endif()
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    return()
  endif()
  if(MSVC)
    return()
  endif()

  foreach(_t IN ITEMS gtest gtest_main)
    if(NOT TARGET ${_t})
      continue()
    endif()
    get_target_property(_imp ${_t} IMPORTED)
    if(_imp)
      continue()
    endif()
    target_compile_options(${_t} PRIVATE -g0)
  endforeach()
endfunction()

# -----------------------------------------------------------------------------
# _create_third_party_aliases: Create library aliases
# -----------------------------------------------------------------------------
# Creates alias targets for a third-party library to provide consistent
# naming conventions (e.g., fmt::fmt as an alias for fmt)
#
# Parameters:
#   name - Library name
#   aliases - List of alias names to create
#
function(_create_third_party_aliases name aliases)
    if(NOT aliases)
        message(WARNING "No aliases provided for ${name}")
        return()
    endif()

    foreach(alias ${aliases})
        # Check if the alias already exists
        if(NOT TARGET ${alias})
            # Check if the base target exists
            if(TARGET ${name})
                add_library(${alias} ALIAS ${name})
                message(STATUS "Created alias ${alias} for ${name}")
            else()
                message(WARNING "Cannot create alias ${alias}: target ${name} does not exist")
            endif()
        else()
            message(STATUS "Alias ${alias} already exists, skipping")
        endif()
    endforeach()
endfunction()

# -----------------------------------------------------------------------------
# _create_third_party_interface_targets: Create namespaced aliases
# -----------------------------------------------------------------------------
# Creates namespaced aliases for third-party library targets.
# This provides a consistent interface for linking against third-party libraries
# throughout the XSigma project.
#
# The function handles multiple scenarios:
# 1. Target with namespace alias (e.g., fmt::fmt) - resolves the actual target
# 2. Plain target (e.g., fmt) - creates alias directly
# 3. Multiple possible target names - tries each in order
#
# Parameters:
#   xsigma_targets - List of namespaced target specifications in format:
#                    "Xxx::name=target1|target2|..."
#                    where target1, target2, etc. are tried in order
#
# Example:
#   _create_third_party_interface_targets(
#       "Fmt::fmt=fmt::fmt|fmt"
#       "Gtest::gtest=GTest::gtest|gtest;Gtest::gtest_main=GTest::gtest_main|gtest_main"
#   )
#
function(_create_third_party_interface_targets)
    set(xsigma_targets ${ARGN})

    if(NOT xsigma_targets)
        return()
    endif()

    foreach(target_spec ${xsigma_targets})
        # Parse the specification: "Xxx::name=target1|target2|..."
        string(REPLACE "=" ";" spec_parts "${target_spec}")
        list(LENGTH spec_parts spec_parts_len)

        if(NOT spec_parts_len EQUAL 2)
            message(WARNING "Invalid namespaced target specification: ${target_spec}")
            continue()
        endif()

        list(GET spec_parts 0 xsigma_name)
        list(GET spec_parts 1 source_targets)

        # Check if namespaced target already exists
        if(TARGET ${xsigma_name})
            message(STATUS "Target ${xsigma_name} already exists, skipping")
            continue()
        endif()

        # Split source targets by pipe (|) to get alternatives
        string(REPLACE "|" ";" source_target_list "${source_targets}")

        set(target_created FALSE)
        foreach(source_target ${source_target_list})
            if(TARGET ${source_target})
                # Check if source target is IMPORTED (from find_package)
                get_target_property(is_imported ${source_target} IMPORTED)

                if(is_imported)
                    # For IMPORTED targets, create an INTERFACE library that links to it
                    # (ALIAS targets cannot alias IMPORTED targets)
                    add_library(${xsigma_name} INTERFACE IMPORTED GLOBAL)
                    target_link_libraries(${xsigma_name} INTERFACE ${source_target})
                    message(STATUS "Created ${xsigma_name} -> ${source_target} (INTERFACE wrapper for IMPORTED target)")
                else()
                    # Check if source target is an alias and resolve it
                    get_target_property(aliased_target ${source_target} ALIASED_TARGET)

                    if(aliased_target)
                        # Source is an alias, create alias to the actual target
                        add_library(${xsigma_name} ALIAS ${aliased_target})
                        message(STATUS "Created ${xsigma_name} -> ${source_target} (resolved to ${aliased_target})")
                    else()
                        # Source is a real target, create alias directly
                        add_library(${xsigma_name} ALIAS ${source_target})
                        message(STATUS "Created ${xsigma_name} -> ${source_target}")
                    endif()
                endif()

                set(target_created TRUE)
                break()
            endif()
        endforeach()

        if(NOT target_created)
            message(WARNING "Could not create alias ${xsigma_name}: none of the source targets exist (${source_targets})")
        endif()
    endforeach()
endfunction()

# =============================================================================
# Main Function: add_third_party_library
# =============================================================================
# Enhanced function to add third-party libraries with comprehensive configuration
# options including flag filtering, automatic target organization, and alias creation
#
# Parameters:
#   name - Name of the third-party library (required)
#
# Optional Parameters:
#   SOURCE_DIR <path>           - Custom source directory path relative to ThirdParty/
#                                 If not specified, uses the library name as the path
#                                 Example: "googletest/googletest" for nested structures
#   EXTERNAL_NAME <name>        - Name to use when searching for external package
#   CONDITION <variable>        - CMake variable that must be true to add the library
#   LIBRARY_TYPE <type>         - Type of library: STATIC, SHARED, DEFAULT, or HEADER_ONLY
#                                 HEADER_ONLY: Creates INTERFACE library target with include dirs
#                                 Supports ALIASES and XSIGMA_TARGETS for header-only libraries
#   LIBRARY_TYPE_VAR <var>      - CMake variable name for library-specific control
#                                 Example: MI_BUILD_STATIC for mimalloc
#                                 Note: Not applicable for HEADER_ONLY libraries
#   FILTER_FLAGS <flags...>     - List of compiler/linker flags to filter out
#                                 Example: "-fno-exceptions" "-Werror"
#                                 Note: Not applicable for HEADER_ONLY libraries (no compilation)
#   DISABLE_WARNINGS            - Boolean flag to disable all compiler warnings
#                                 Adds -w for Clang/GCC and /w for MSVC
#                                 Note: Not applicable for HEADER_ONLY libraries (no compilation)
#   EXTRA_FLAGS <flags...>      - List of additional compiler flags to add
#                                 Example: "-flto=thin" "-march=native"
#                                 Note: Not applicable for HEADER_ONLY libraries (no compilation)
#   ALIASES <aliases...>        - List of alias names to create for the library
#                                 Example: "fmt::fmt-header-only" for fmt library
#                                 Supported for all library types including HEADER_ONLY
#   XSIGMA_TARGETS <specs...>   - List of namespaced target specifications
#                                 Format: "Xxx::name=target1|target2|..."
#                                 Example: "Fmt::fmt=fmt::fmt-header-only|fmt"
#                                 Supported for all library types including HEADER_ONLY
#   INCLUDE_DIR <path>          - Include directory path to add to XSIGMA_DEPENDENCY_INCLUDE_DIRS
#                                 Path is relative to CMAKE_CURRENT_SOURCE_DIR (ThirdParty/)
#                                 Example: "fmt/include" for fmt library
#   AUTO_ADD_TO_DEPENDENCIES    - Boolean flag to automatically add to XSIGMA_DEPENDENCY_LIBS
#                                 Default: ON (automatically adds Xxx::name target)
#   EXCLUDE_FROM_ALL            - If set, add_subdirectory(... EXCLUDE_FROM_ALL) for the bundled lib
#   BINARY_DIR <path>           - Out-of-tree build dir for add_subdirectory (absolute, or relative to
#                                 CMAKE_BINARY_DIR). Default: ${CMAKE_CURRENT_BINARY_DIR}/${name}_build
#
# Example Usage:
#   add_third_party_library(fmt
#       LIBRARY_TYPE STATIC
#       FILTER_FLAGS "-fno-exceptions" "-Werror"
#       ALIASES "fmt::fmt"
#       XSIGMA_TARGETS "Fmt::fmt=fmt::fmt|fmt"
#   )
#
#   add_third_party_library(cpuinfo
#       LIBRARY_TYPE STATIC
#       SOURCE_DIR cpuinfo
#       DISABLE_WARNINGS
#   )
#
function(add_third_party_library name)
    # Parse function arguments
    cmake_parse_arguments(
        TPL                                                           # Prefix
        "DISABLE_WARNINGS;AUTO_ADD_TO_DEPENDENCIES;EXCLUDE_FROM_ALL" # Options (boolean flags)
        "SOURCE_DIR;EXTERNAL_NAME;CONDITION;LIBRARY_TYPE;LIBRARY_TYPE_VAR;INCLUDE_DIR;BINARY_DIR" # One-value keywords
        "FILTER_FLAGS;EXTRA_FLAGS;ALIASES;XSIGMA_TARGETS"             # Multi-value keywords
        ${ARGN}                                                       # Arguments to parse
    )

    # Set default values for optional parameters
    if(NOT DEFINED TPL_EXTERNAL_NAME)
        set(TPL_EXTERNAL_NAME ${name})
    endif()

    # Default AUTO_ADD_TO_DEPENDENCIES to ON if not explicitly set to OFF
    # Note: cmake_parse_arguments sets boolean options to FALSE when not specified,
    # so we check if it's in the ARGN list to determine if it was explicitly set
    if("AUTO_ADD_TO_DEPENDENCIES" IN_LIST ARGN)
        # Explicitly specified, use the parsed value
    else()
        # Not specified, default to ON
        set(TPL_AUTO_ADD_TO_DEPENDENCIES ON)
    endif()

    # SOURCE_DIR defaults to the library name if not specified
    if(NOT DEFINED TPL_SOURCE_DIR)
        set(TPL_SOURCE_DIR ${name})
    endif()

    # -------------------------------------------------------------------------
    # Resolve SOURCE_DIR and INCLUDE_DIR to absolute paths.
    # Callers outside ThirdParty/ pass absolute paths; callers inside pass
    # relative names.  IS_ABSOLUTE lets both conventions coexist.
    # -------------------------------------------------------------------------
    if(IS_ABSOLUTE "${TPL_SOURCE_DIR}")
        set(_tpl_abs_source_dir "${TPL_SOURCE_DIR}")
    else()
        set(_tpl_abs_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/${TPL_SOURCE_DIR}")
    endif()

    if(DEFINED TPL_INCLUDE_DIR)
        if(IS_ABSOLUTE "${TPL_INCLUDE_DIR}")
            set(_tpl_abs_include_dir "${TPL_INCLUDE_DIR}")
        else()
            set(_tpl_abs_include_dir "${CMAKE_CURRENT_SOURCE_DIR}/${TPL_INCLUDE_DIR}")
        endif()
    endif()

    # -------------------------------------------------------------------------
    # Step 1: Check if already processed to avoid duplicates
    # -------------------------------------------------------------------------
    get_property(XSIGMA_${name}_PROCESSED GLOBAL PROPERTY XSIGMA_${name}_PROCESSED)
    if(XSIGMA_${name}_PROCESSED)
        message(STATUS "Third-party library ${name} already processed, skipping")
        return()
    endif()

    # -------------------------------------------------------------------------
    # Step 2: Determine library type
    # -------------------------------------------------------------------------
    # Priority: LIBRARY_TYPE parameter
    set(lib_type "DEFAULT")
    if(DEFINED TPL_LIBRARY_TYPE)
        set(lib_type ${TPL_LIBRARY_TYPE})
    endif()

    # -------------------------------------------------------------------------
    # Step 3: Early handling for HEADER_ONLY libraries
    # -------------------------------------------------------------------------
    # Header-only libraries don't need compilation, so we handle them early
    # and return to avoid unnecessary external library searches or condition checks
    if(lib_type STREQUAL "HEADER_ONLY")
        message(STATUS "Processing header-only library ${name}")

        # Verify that INCLUDE_DIR is specified for header-only libraries
        if(NOT DEFINED TPL_INCLUDE_DIR)
            message(WARNING "Header-only library ${name} specified but no INCLUDE_DIR provided - skipping")
            return()
        endif()

        # Create an INTERFACE library target for the header-only library
        # This allows it to be used with target_link_libraries() like any other library
        if(NOT TARGET ${name})
            add_library(${name} INTERFACE)
            message(STATUS "Created INTERFACE library target: ${name}")
        endif()

        # Add include directory to the INTERFACE library
        # This will be propagated to any target that links against this library
        target_include_directories(${name} INTERFACE "${_tpl_abs_include_dir}")
        message(STATUS "Added include directory to ${name}: ${_tpl_abs_include_dir}")

        # Create default namespaced alias for the INTERFACE library
        if(NOT TARGET ${name}::${name})
            add_library(${name}::${name} ALIAS ${name})
            message(STATUS "Created alias: ${name}::${name} -> ${name}")
        endif()

        # Add to XSIGMA_DEPENDENCY_LIBS so it can be linked against
        get_property(_temp_libs GLOBAL PROPERTY _XSIGMA_DEPENDENCY_LIBS)
        list(APPEND _temp_libs ${name}::${name})
        set_property(GLOBAL PROPERTY _XSIGMA_DEPENDENCY_LIBS "${_temp_libs}")
        message(STATUS "Dependency: ${name}::${name} added to XSIGMA_DEPENDENCY_LIBS (header-only)")

        # Add include directory to global include paths for project-wide access
        get_property(_temp_includes GLOBAL PROPERTY _XSIGMA_DEPENDENCY_INCLUDE_DIRS)
        list(APPEND _temp_includes "${_tpl_abs_include_dir}")
        set_property(GLOBAL PROPERTY _XSIGMA_DEPENDENCY_INCLUDE_DIRS "${_temp_includes}")
        message(STATUS "Dependency: ${TPL_INCLUDE_DIR} added to XSIGMA_DEPENDENCY_INCLUDE_DIRS (header-only)")

        # =====================================================================
        # Create library aliases if requested (supported for HEADER_ONLY)
        # =====================================================================
        if(TPL_ALIASES)
            _create_third_party_aliases(${name} "${TPL_ALIASES}")
        endif()

        # =====================================================================
        # Create XSigma:: namespace interface targets if requested (supported for HEADER_ONLY)
        # =====================================================================
        if(TPL_XSIGMA_TARGETS)
            _create_third_party_interface_targets("${TPL_XSIGMA_TARGETS}")
        endif()

        # Early return - no further processing needed for header-only libraries
        set_property(GLOBAL PROPERTY XSIGMA_${name}_PROCESSED TRUE)
        return()
    endif()

    # -------------------------------------------------------------------------
    # Step 4: Try to find external library first if requested
    # -------------------------------------------------------------------------
    if(XSIGMA_ENABLE_EXTERNAL)
        find_package(${TPL_EXTERNAL_NAME} QUIET GLOBAL)
        if(${TPL_EXTERNAL_NAME}_FOUND OR ${name}_FOUND)
            message(STATUS "================================================================")
            message(STATUS "Found external ${TPL_EXTERNAL_NAME}")

            # ADD THIS DEBUG OUTPUT:
            message(STATUS "DEBUG: Checking available targets for ${TPL_EXTERNAL_NAME}:")

            # Check common target naming patterns
            foreach(target_suffix "" "::${name}" "::${TPL_EXTERNAL_NAME}" "-static" "-shared")
                set(target_name "${name}${target_suffix}")
                if(TARGET ${target_name})
                    message(STATUS "  Found target: ${target_name}")
                    get_target_property(target_type ${target_name} TYPE)
                    message(STATUS "    Type: ${target_type}")
                endif()
            endforeach()

            # Print all variables set by find_package
            get_cmake_property(_variableNames VARIABLES)
            foreach(_variableName ${_variableNames})
                if(_variableName MATCHES "^${name}" OR _variableName MATCHES "^${TPL_EXTERNAL_NAME}")
                    message(STATUS "  ${_variableName}=${${_variableName}}")
                endif()
            endforeach()

            message(STATUS "================================================================")

            # Create namespaced target using XSIGMA_TARGETS if provided, otherwise use default pattern
            if(TPL_XSIGMA_TARGETS)
                _create_third_party_interface_targets("${TPL_XSIGMA_TARGETS}")
            else()
                _create_third_party_interface_targets(
                    "${name}::${name}=${name}::${name}-header-only||${name}::${name}|${name}|${name}-static"
                )
            endif()

            # Automatically populate dependency lists if requested (default: ON)
            if(TARGET ${name}::${name})
                # Add to XSIGMA_DEPENDENCY_LIBS using global property
                get_property(_temp_libs GLOBAL PROPERTY _XSIGMA_DEPENDENCY_LIBS)
                list(APPEND _temp_libs ${name}::${name})
                set_property(GLOBAL PROPERTY _XSIGMA_DEPENDENCY_LIBS "${_temp_libs}")
                message(STATUS "Dependency: ${name}::${name} added to XSIGMA_DEPENDENCY_LIBS (external)")
            endif()

            # Add include directory if specified (external path from find_package)
            if(DEFINED ${name}_INCLUDE_DIR)
                # Fall back to the include directory set by find_package
                get_property(_temp_includes GLOBAL PROPERTY _XSIGMA_DEPENDENCY_INCLUDE_DIRS)
                list(APPEND _temp_includes ${${name}_INCLUDE_DIR})
                set_property(GLOBAL PROPERTY _XSIGMA_DEPENDENCY_INCLUDE_DIRS "${_temp_includes}")
                message(STATUS "Dependency: ${${name}_INCLUDE_DIR} added to XSIGMA_DEPENDENCY_INCLUDE_DIRS (external)")
            endif()

            set_property(GLOBAL PROPERTY XSIGMA_${name}_PROCESSED TRUE)
            return()
        endif()
    endif()

    # -------------------------------------------------------------------------
    # Step 5: Check condition (early return pattern)
    # -------------------------------------------------------------------------
    if(DEFINED TPL_CONDITION)
        if(NOT ${TPL_CONDITION})
            message(STATUS "Skipping ${name}: condition ${TPL_CONDITION} is not met")
            return()
        endif()
    endif()

    message("================================================================")
    message("Build from source ${TPL_EXTERNAL_NAME}")
    message("================================================================")

    # -------------------------------------------------------------------------
    # Step 6: Save current state (BUILD_SHARED_LIBS and flags)
    # -------------------------------------------------------------------------
    set(_saved_build_shared_libs ${BUILD_SHARED_LIBS})

    # Save and optionally filter compiler/linker flags when we plan to mutate them
    set(_TPL_SAVED_FLAGS FALSE)
    set(_tpl_filter_flags "")
    if(TPL_FILTER_FLAGS)
        set(_tpl_filter_flags "${TPL_FILTER_FLAGS}")
    endif()
    if(TPL_FILTER_FLAGS OR TPL_DISABLE_WARNINGS OR TPL_EXTRA_FLAGS)
        _save_third_party_flags(${name} "${_tpl_filter_flags}")
        set(_TPL_SAVED_FLAGS TRUE)
    endif()

    # -------------------------------------------------------------------------
    # Step 6b: Handle DISABLE_WARNINGS and EXTRA_FLAGS
    # -------------------------------------------------------------------------
    if(TPL_DISABLE_WARNINGS)
        # Remove all existing warning-related flags
        # Patterns: -W*, -Wno-*, /wd* (but keep /W* for MSVC warning level)
        if(MSVC)
            # For MSVC: remove only /wd* (specific warning disables) and -W* flags
            _filter_flags_internal("${CMAKE_CXX_FLAGS}" "-W.*;/wd.*" filtered_cxx_flags_no_warnings)
            _filter_flags_internal("${CMAKE_C_FLAGS}" "-W.*;/wd.*" filtered_c_flags_no_warnings)

            set(CMAKE_CXX_FLAGS "${filtered_cxx_flags_no_warnings}")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
            set(CMAKE_C_FLAGS "${filtered_c_flags_no_warnings}")
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" PARENT_SCOPE)

            # Add /W0 to disable all warnings while keeping the warning level infrastructure
            string(APPEND CMAKE_CXX_FLAGS " /W0")
            string(APPEND CMAKE_C_FLAGS " /W0")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" PARENT_SCOPE)
        else()
            # For Clang/GCC: remove all -W* and -Wno-* flags
            _filter_flags_internal("${CMAKE_CXX_FLAGS}" "-W.*;-Wno-.*" filtered_cxx_flags_no_warnings)
            _filter_flags_internal("${CMAKE_C_FLAGS}" "-W.*;-Wno-.*" filtered_c_flags_no_warnings)

            set(CMAKE_CXX_FLAGS "${filtered_cxx_flags_no_warnings}")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
            set(CMAKE_C_FLAGS "${filtered_c_flags_no_warnings}")
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" PARENT_SCOPE)

            # Add -w to disable all warnings
            string(APPEND CMAKE_CXX_FLAGS " -w")
            string(APPEND CMAKE_C_FLAGS " -w")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" PARENT_SCOPE)
        endif()
        message(STATUS "Disabled all warnings for ${name} (removed existing warning flags and added suppression)")
    endif()

    # Add extra compiler flags if provided
    if(TPL_EXTRA_FLAGS)
        foreach(flag ${TPL_EXTRA_FLAGS})
            string(APPEND CMAKE_CXX_FLAGS " ${flag}")
            string(APPEND CMAKE_C_FLAGS " ${flag}")
        endforeach()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" PARENT_SCOPE)
        message(STATUS "Added extra flags for ${name}: ${TPL_EXTRA_FLAGS}")
    endif()

    # -------------------------------------------------------------------------
    # Step 7: Configure library type (STATIC or SHARED)
    # -------------------------------------------------------------------------
    # Note: HEADER_ONLY is handled earlier in Step 3 and returns early
    if(lib_type STREQUAL "STATIC")
        set(BUILD_SHARED_LIBS OFF)
        message(STATUS "Configuring ${name} as STATIC library")

        # Set library-specific variable if provided
        if(DEFINED TPL_LIBRARY_TYPE_VAR)
            set(${TPL_LIBRARY_TYPE_VAR} ON CACHE BOOL "Build ${name} as static library" FORCE)
        endif()
    elseif(lib_type STREQUAL "SHARED")
        set(BUILD_SHARED_LIBS ON)
        message(STATUS "Configuring ${name} as SHARED library")

        # Set library-specific variable if provided
        if(DEFINED TPL_LIBRARY_TYPE_VAR)
            set(${TPL_LIBRARY_TYPE_VAR} OFF CACHE BOOL "Build ${name} as shared library" FORCE)
        endif()
    else()
        message(STATUS "Configuring ${name} with DEFAULT library type")
    endif()

    # -------------------------------------------------------------------------
    # Step 8: Add the third-party library subdirectory
    # -------------------------------------------------------------------------
        # Support both standard (SOURCE_DIR/CMakeLists.txt) and nested (SOURCE_DIR/subdir/CMakeLists.txt) structures
        set(lib_source_dir "${_tpl_abs_source_dir}")
        set(lib_cmakelists_path "${lib_source_dir}/CMakeLists.txt")

        # If CMakeLists.txt doesn't exist at the top level, check if it's in a subdirectory
        # This handles cases where CMakeLists.txt lives in an immediate subdirectory
        if(NOT EXISTS "${lib_cmakelists_path}")
            # Try to find CMakeLists.txt in immediate subdirectories
            file(GLOB subdirs LIST_DIRECTORIES true "${lib_source_dir}/*")
            foreach(subdir ${subdirs})
                if(IS_DIRECTORY "${subdir}")
                    get_filename_component(subdir_name "${subdir}" NAME)
                    set(potential_cmakelists "${subdir}/CMakeLists.txt")
                    if(EXISTS "${potential_cmakelists}")
                        set(lib_source_dir "${subdir}")
                        set(lib_cmakelists_path "${potential_cmakelists}")
                        message(STATUS "Found CMakeLists.txt in nested directory: ${subdir_name}")
                        break()
                    endif()
                endif()
            endforeach()
        endif()

        if(EXISTS "${lib_cmakelists_path}")
            message(STATUS "Using bundled ${name}")

            # Store the binary directory for later use (caller may pin a global path when SOURCE_DIR
            # is absolute and lives outside CMAKE_CURRENT_BINARY_DIR, e.g. tests calling from Library/*).
            if(DEFINED TPL_BINARY_DIR)
                if(IS_ABSOLUTE "${TPL_BINARY_DIR}")
                    set(lib_binary_dir "${TPL_BINARY_DIR}")
                else()
                    set(lib_binary_dir "${CMAKE_BINARY_DIR}/${TPL_BINARY_DIR}")
                endif()
            else()
                set(lib_binary_dir "${CMAKE_CURRENT_BINARY_DIR}/${name}_build")
            endif()

            # Add the library
            if(TPL_EXCLUDE_FROM_ALL)
                add_subdirectory(${lib_source_dir} ${lib_binary_dir} EXCLUDE_FROM_ALL)
            else()
                add_subdirectory(${lib_source_dir} ${lib_binary_dir})
            endif()

            # -------------------------------------------------------------------------
            # Step 9: Automatically set FOLDER properties for IDE organization
            # -------------------------------------------------------------------------
            _set_third_party_folder_properties(${name} "${lib_binary_dir}")

            # -------------------------------------------------------------------------
            # Step 10: Create library aliases if requested
            # -------------------------------------------------------------------------
            if(TPL_ALIASES)
                _create_third_party_aliases(${name} "${TPL_ALIASES}")
            endif()

            # -------------------------------------------------------------------------
            # Step 11: Create namespaced interface targets if requested
            # -------------------------------------------------------------------------
            if(TPL_XSIGMA_TARGETS)
                _create_third_party_interface_targets("${TPL_XSIGMA_TARGETS}")
            endif()

            # -------------------------------------------------------------------------
            # Step 12: Automatically populate dependency lists
            # -------------------------------------------------------------------------
            # This step automatically adds the library to XSIGMA_DEPENDENCY_LIBS and
            # XSIGMA_DEPENDENCY_INCLUDE_DIRS if requested (default: ON)

            if(TPL_AUTO_ADD_TO_DEPENDENCIES)
                # Check if the namespaced target was created
                if(TARGET ${name}::${name})
                    # Add to XSIGMA_DEPENDENCY_LIBS using global property
                    # We use global properties because PARENT_SCOPE doesn't update the variable
                    # in the current scope, so subsequent function calls wouldn't see the changes
                    get_property(_temp_libs GLOBAL PROPERTY _XSIGMA_DEPENDENCY_LIBS)
                    list(APPEND _temp_libs ${name}::${name})
                    set_property(GLOBAL PROPERTY _XSIGMA_DEPENDENCY_LIBS "${_temp_libs}")
                    message(STATUS "Dependency: ${name}::${name} added to XSIGMA_DEPENDENCY_LIBS")
                endif()

                # Add include directory if specified
                if(DEFINED TPL_INCLUDE_DIR)
                    # Add to XSIGMA_DEPENDENCY_INCLUDE_DIRS using global property
                    get_property(_temp_includes GLOBAL PROPERTY _XSIGMA_DEPENDENCY_INCLUDE_DIRS)
                    list(APPEND _temp_includes "${_tpl_abs_include_dir}")
                    set_property(GLOBAL PROPERTY _XSIGMA_DEPENDENCY_INCLUDE_DIRS "${_temp_includes}")
                    message(STATUS "Dependency: ${_tpl_abs_include_dir} added to XSIGMA_DEPENDENCY_INCLUDE_DIRS")
                endif()
            endif()

            set_property(GLOBAL PROPERTY XSIGMA_${name}_PROCESSED TRUE)
        else()
            message(WARNING "Third-party library ${name} not found (expected ${lib_cmakelists_path})")
        endif()

    # -------------------------------------------------------------------------
    # Step 13: Restore original state
    # -------------------------------------------------------------------------
    # Restore BUILD_SHARED_LIBS
    set(BUILD_SHARED_LIBS ${_saved_build_shared_libs})

    # Restore compiler/linker flags if they were filtered.
    # _restore_third_party_flags sets PARENT_SCOPE = this function's scope.
    # We must then propagate back up to the caller's (directory) scope,
    # because DISABLE_WARNINGS leaked the modified flags there via PARENT_SCOPE.
    if(_TPL_SAVED_FLAGS)
        _restore_third_party_flags(${name})
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" PARENT_SCOPE)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}" PARENT_SCOPE)
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}" PARENT_SCOPE)
        set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS}" PARENT_SCOPE)
        set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS}" PARENT_SCOPE)
    endif()
endfunction()

# =============================================================================
# GoogleTest — bundled via add_third_party_library (shared by all */Testing/Cxx)
# =============================================================================
function(xsigma_add_googletest)
    set(BUILD_GMOCK OFF CACHE BOOL "Disable Google Mock" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "Disable Google Test install" FORCE)
    set(gtest_build_samples OFF CACHE BOOL "Disable Google Test samples" FORCE)
    set(gtest_build_tests OFF CACHE BOOL "Disable Google Test tests" FORCE)
    if(MSVC)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    endif()
    add_third_party_library(
        googletest
        SOURCE_DIR "${XSIGMA_THIRDPARTY_DIR}/googletest"
        BINARY_DIR "${CMAKE_BINARY_DIR}/ThirdParty/googletest_build"
        EXTERNAL_NAME GTest
        LIBRARY_TYPE STATIC
        EXCLUDE_FROM_ALL
        AUTO_ADD_TO_DEPENDENCIES OFF
        XSIGMA_TARGETS
            "Gtest::gtest=GTest::gtest|gtest"
            "Gtest::gtest_main=GTest::gtest_main|gtest_main"
    )
    _create_third_party_interface_targets(
        "Gtest::gtest=GTest::gtest|gtest"
        "Gtest::gtest_main=GTest::gtest_main|gtest_main"
    )
endfunction()

# =============================================================================
# Parallel — bundled via add_third_party_library (ThirdParty/Parallel submodule)
#
# Parallel is a pure third-party dependency (same treatment as Profiler):
# XSigma sets no PARALLEL_ENABLE_* feature flags anywhere outside this file,
# never fans coverage/sanitizer/clang-tidy/IWYU/spell/C++-standard into it, and
# exposes no setup.py --project.parallel switch for it. Tests and examples
# build only from the standalone Parallel repo. The backend selector
# (PARALLEL_BACKEND) remains a user-facing XSigma flag — consumed by
# Parallel's own CMakeLists when the subproject configures. TBB/OpenMP are
# resolved by Parallel's own finder modules (Cmake/ inside the submodule).
# Called by consuming libraries (Graph) before their
# if(TARGET Parallel::Parallel) checks — idempotent.
# =============================================================================
function(xsigma_add_parallel)
    if(TARGET Parallel::Parallel)
        return()
    endif()
    set(_parallel_src "${XSIGMA_THIRDPARTY_DIR}/Parallel")
    if(NOT EXISTS "${_parallel_src}/CMakeLists.txt")
        message(FATAL_ERROR "ThirdParty/Parallel is missing. Initialize the submodule:\n"
                            "  git submodule update --init ThirdParty/Parallel")
    endif()
    set(PARALLEL_ENABLE_TESTING OFF CACHE BOOL "Parallel tests build only standalone" FORCE)
    set(PARALLEL_ENABLE_EXAMPLES OFF CACHE BOOL "Parallel examples build only standalone" FORCE)
    add_third_party_library(
        parallel
        SOURCE_DIR "${_parallel_src}"
        BINARY_DIR "${CMAKE_BINARY_DIR}/ThirdParty/Parallel"
        EXTERNAL_NAME Parallel
        EXCLUDE_FROM_ALL
        AUTO_ADD_TO_DEPENDENCIES OFF
    )
    # Parallel's CMake exposes the repo root (`<Parallel/parallel.h>`). Add the
    # nested Parallel/ directory so XSigma clients can include `<parallel.h>`,
    # matching Profiler's `<profiler.h>`. Do not edit the submodule.
    if(TARGET Parallel)
        target_include_directories(
            Parallel PUBLIC $<BUILD_INTERFACE:${_parallel_src}/Parallel>
        )
        if(WIN32)
            target_compile_definitions(Parallel PUBLIC NOMINMAX)
        endif()
    endif()
endfunction()

