# =============================================================================
# XSigma Spell
# Checking Configuration Module

# This module configures codespell as a check-only build step. Misspellings fail the
# build; sources are never rewritten. ThirdParty trees are skipped.
#
# Usage in each module's CMakeLists.txt:
#
# option(XXX_ENABLE_SPELL "Enable spell checking for XXX" OFF)
# mark_as_advanced(XXX_ENABLE_SPELL)
# if(XXX_ENABLE_SPELL)
#   include(spell)
# endif()

# Skip spell checking for third-party libraries
get_filename_component(_spell_dir_name "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
if(_spell_dir_name STREQUAL "ThirdParty"
   OR CMAKE_CURRENT_SOURCE_DIR MATCHES ".*/ThirdParty/.*"
   OR CMAKE_CURRENT_SOURCE_DIR MATCHES ".*/third_party/.*"
   OR CMAKE_CURRENT_SOURCE_DIR MATCHES ".*/3rdparty/.*"
)
  unset(_spell_dir_name)
  return()
endif()

# Find codespell executable (cached — find_program is a no-op on repeat calls)
find_program(
  CODESPELL_EXECUTABLE
  NAMES codespell
  PATHS "$ENV{HOME}/.local/bin" "/usr/local/bin" "/usr/bin"
        "$ENV{USERPROFILE}/AppData/Local/Programs/Python/Python*/Scripts"
        "$ENV{PROGRAMFILES}/Python*/Scripts"
  DOC "Path to codespell executable"
)

if(NOT CODESPELL_EXECUTABLE)
  message(
    FATAL_ERROR
      "Codespell requested but not found!

Please install codespell:

  - pip install codespell
  - conda install -c conda-forge codespell
  - Ubuntu/Debian: sudo apt-get install codespell
  - macOS: brew install codespell
  - Windows: pip install codespell

Or set ${_spell_dir_name}_ENABLE_SPELL=OFF to disable spell checking"
  )
else()
  message(STATUS "Found codespell: ${CODESPELL_EXECUTABLE}")

  set(_spell_args)
  set(_spell_ignore_file "${CMAKE_SOURCE_DIR}/Scripts/suppressions/spell_suppressions.txt")
  if(NOT EXISTS "${_spell_ignore_file}")
    message(FATAL_ERROR "Spell suppression file not found: ${_spell_ignore_file}")
  endif()
  list(APPEND _spell_args "--ignore-words=${_spell_ignore_file}")
  message(STATUS "Using spell suppression file: ${_spell_ignore_file}")

  # Always skip vendored trees. `ThirdParty` alone only matches a directory
  # *entry* during os.walk; `*ThirdParty*` also matches full paths if codespell
  # is pointed at a ThirdParty file or the walk starts inside that tree.
  set(_spell_skip
      ".git,.augment,.github,.vscode,build,Build,Cmake,ThirdParty,third_party,3rdparty,*ThirdParty*,*third_party*,*3rdparty*"
  )
  list(APPEND _spell_args "--skip=${_spell_skip}")

  set(CODESPELL_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.codespellrc")
  if(EXISTS ${CODESPELL_CONFIG_FILE})
    message(STATUS "Using codespell configuration: ${CODESPELL_CONFIG_FILE}")
  endif()

  # Target names are unique per module directory to avoid conflicts
  string(TOLOWER "${_spell_dir_name}" _spell_dir_lower)

  add_custom_target(
    spell_check_${_spell_dir_lower}
    COMMAND ${CODESPELL_EXECUTABLE} ${_spell_args} ${CMAKE_CURRENT_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running spell check for ${_spell_dir_name}..."
    VERBATIM
  )

  add_custom_target(
    spell_check_build_${_spell_dir_lower} ALL
    COMMAND ${CODESPELL_EXECUTABLE} ${_spell_args} ${CMAKE_CURRENT_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running spell check for ${_spell_dir_name} during build..."
    VERBATIM
  )

  message(
    STATUS "${_spell_dir_name}_ENABLE_SPELL is ON: codespell is check-only (no --write-changes)"
  )
endif()

unset(_spell_dir_name)
unset(_spell_dir_lower)
unset(_spell_args)
unset(_spell_ignore_file)
unset(_spell_skip)
