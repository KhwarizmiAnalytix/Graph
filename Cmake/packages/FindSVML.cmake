# FindSVML.cmake — Locate Intel SVML (Short Vector Math Library) ThirdParty binaries
#
# Resolves the ThirdParty/svml directory relative to this find module:
#   <root>/Cmake/packages/FindSVML.cmake  →  <root>/ThirdParty/svml
#
# Imported targets (created only when SVML_FOUND):
#   SVML::SVML    — main SVML library (Windows: import lib + DLL; Unix: .so)
#   SVML::IRC     — IRC companion runtime         (Unix only)
#   SVML::INTLC   — INTLC companion runtime       (Unix only)
#
# Output variables:
#   SVML_FOUND               — TRUE when the core SVML library is located
#   SVML_LIBRARIES           — all link-time library paths
#   SVML_RUNTIME_LIBRARIES   — runtime files (DLLs / .so) to stage in the build tree

# ---------------------------------------------------------------------------
# Locate the ThirdParty/svml root from this file's position
# ---------------------------------------------------------------------------
get_filename_component(_svml_mod_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_svml_tp_root "${_svml_mod_dir}/../.." ABSOLUTE)
set(_svml_tp "${_svml_tp_root}/ThirdParty/svml")

set(SVML_LIBRARIES)
set(SVML_RUNTIME_LIBRARIES)

# ---------------------------------------------------------------------------
# Windows — link via import library; stage the DLL at runtime
# ---------------------------------------------------------------------------
if(WIN32)
  set(_svml_lib_dir "${_svml_tp}/windows/lib")
  set(_svml_bin_dir "${_svml_tp}/windows/svml")

  find_library(SVML_LIBRARY_SVML
    NAMES svml_dispmd
    PATHS "${_svml_lib_dir}"
    NO_DEFAULT_PATH
  )
  find_file(SVML_DLL_SVML
    NAMES svml_dispmd.dll
    PATHS "${_svml_bin_dir}"
    NO_DEFAULT_PATH
  )

  if(SVML_LIBRARY_SVML)
    list(APPEND SVML_LIBRARIES "${SVML_LIBRARY_SVML}")
  endif()
  if(SVML_DLL_SVML)
    list(APPEND SVML_RUNTIME_LIBRARIES "${SVML_DLL_SVML}")
  endif()
endif()

# ---------------------------------------------------------------------------
# Unix — shared objects; all three must be staged at runtime
# ---------------------------------------------------------------------------
if(UNIX)
  set(_svml_lib_dir "${_svml_tp}/unix/lib")

  find_library(SVML_LIBRARY_SVML   NAMES svml   PATHS "${_svml_lib_dir}" NO_DEFAULT_PATH)
  find_library(SVML_LIBRARY_IRC    NAMES irc    PATHS "${_svml_lib_dir}" NO_DEFAULT_PATH)
  find_library(SVML_LIBRARY_INTLC  NAMES intlc  PATHS "${_svml_lib_dir}" NO_DEFAULT_PATH)

  foreach(_svml_var IN ITEMS SVML_LIBRARY_SVML SVML_LIBRARY_IRC SVML_LIBRARY_INTLC)
    if(${_svml_var})
      list(APPEND SVML_LIBRARIES        "${${_svml_var}}")
      list(APPEND SVML_RUNTIME_LIBRARIES "${${_svml_var}}")
    endif()
  endforeach()
  unset(_svml_var)

  # Also stage the versioned intlc symlink (libintlc.so.5) if present
  if(EXISTS "${_svml_lib_dir}/libintlc.so.5")
    list(APPEND SVML_RUNTIME_LIBRARIES "${_svml_lib_dir}/libintlc.so.5")
  endif()
endif()

# ---------------------------------------------------------------------------
# Standard args — only SVML_LIBRARY_SVML is strictly required
# ---------------------------------------------------------------------------
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SVML DEFAULT_MSG SVML_LIBRARY_SVML)

# ---------------------------------------------------------------------------
# Create IMPORTED targets when the package is found
# ---------------------------------------------------------------------------
if(SVML_FOUND)
  if(NOT TARGET SVML::SVML)
    if(WIN32)
      # SHARED IMPORTED lets CMake track both the import lib and the DLL
      add_library(SVML::SVML SHARED IMPORTED GLOBAL)
      set_target_properties(SVML::SVML PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_IMPLIB   "${SVML_LIBRARY_SVML}"
        IMPORTED_LOCATION "${SVML_DLL_SVML}"
      )
    else()
      add_library(SVML::SVML UNKNOWN IMPORTED GLOBAL)
      set_target_properties(SVML::SVML PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION "${SVML_LIBRARY_SVML}"
      )
    endif()
  endif()

  if(UNIX)
    if(SVML_LIBRARY_IRC AND NOT TARGET SVML::IRC)
      add_library(SVML::IRC UNKNOWN IMPORTED GLOBAL)
      set_target_properties(SVML::IRC PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION "${SVML_LIBRARY_IRC}"
      )
    endif()

    if(SVML_LIBRARY_INTLC AND NOT TARGET SVML::INTLC)
      add_library(SVML::INTLC UNKNOWN IMPORTED GLOBAL)
      set_target_properties(SVML::INTLC PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION "${SVML_LIBRARY_INTLC}"
      )
    endif()
  endif()
endif()

mark_as_advanced(SVML_LIBRARY_SVML SVML_LIBRARY_IRC SVML_LIBRARY_INTLC SVML_DLL_SVML)

unset(_svml_mod_dir)
unset(_svml_tp_root)
unset(_svml_tp)
unset(_svml_lib_dir)
unset(_svml_bin_dir)
