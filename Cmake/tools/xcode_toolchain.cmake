# =============================================================================
# Xcode Toolchain
# Configuration Module When building with the Xcode generator, this module configures CMake to use
# the same LLVM as both (1) Xcode's TOOLCHAINS setting and (2) CMAKE_*_COMPILER. Compilers are the
# clang/clang++ inside the registered *.xctoolchain (e.g. LLVM22.1.2.xctoolchain), not a separate
# path under /opt/homebrew/opt/llvm/bin, so compile and link steps use one toolchain.
#
# Include this file before project() so compiler detection picks the xctoolchain.
#
# Prerequisites (one-time setup): brew install llvm ln -sf
# /opt/homebrew/opt/llvm/Toolchains/LLVM<ver>.xctoolchain \
# ~/Library/Developer/Toolchains/LLVM<ver>.xctoolchain

include_guard(GLOBAL)

if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
  return()
endif()

# ── Locate Homebrew LLVM ─────────────────────────────────────────────────────
set(_BREW_LLVM_PREFIX "/opt/homebrew/opt/llvm")
set(_BREW_LLVM_CONFIG "${_BREW_LLVM_PREFIX}/bin/llvm-config")

if(NOT EXISTS "${_BREW_LLVM_CONFIG}")
  message(STATUS "xcode_toolchain: Homebrew LLVM not found, keeping default Apple Clang toolchain")
  return()
endif()

execute_process(
  COMMAND "${_BREW_LLVM_CONFIG}" --version OUTPUT_VARIABLE _BREW_LLVM_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
)

# ── Verify the toolchain is registered with Xcode ────────────────────────────
set(_XCODE_TC_DIR "$ENV{HOME}/Library/Developer/Toolchains")

# Try full-version name first (e.g. LLVM22.1.2.xctoolchain), then major-only
set(_XCODE_TC_PATH "${_XCODE_TC_DIR}/LLVM${_BREW_LLVM_VERSION}.xctoolchain")
if(NOT EXISTS "${_XCODE_TC_PATH}")
  string(REGEX MATCH "^([0-9]+)" _LLVM_MAJOR "${_BREW_LLVM_VERSION}")
  set(_XCODE_TC_PATH "${_XCODE_TC_DIR}/LLVM${_LLVM_MAJOR}.xctoolchain")
endif()

if(NOT EXISTS "${_XCODE_TC_PATH}")
  message(
    WARNING
      "xcode_toolchain: Homebrew LLVM ${_BREW_LLVM_VERSION} found but toolchain not registered.\n"
      "Run once to register it:\n"
      "  ln -sf ${_BREW_LLVM_PREFIX}/Toolchains/LLVM${_BREW_LLVM_VERSION}.xctoolchain "
      "${_XCODE_TC_DIR}/LLVM${_BREW_LLVM_VERSION}.xctoolchain\n"
      "Keeping default Apple Clang toolchain."
  )
  return()
endif()

# ── Apply Xcode TOOLCHAINS (Homebrew LLVM) ────────────────────────────────────
set(_BREW_LLVM_TC_ID "org.llvm.${_BREW_LLVM_VERSION}")

set(CMAKE_XCODE_ATTRIBUTE_TOOLCHAINS "${_BREW_LLVM_TC_ID}"
    CACHE STRING "Xcode toolchain identifier (Homebrew LLVM)" FORCE
)

message(STATUS "xcode_toolchain: using LLVM ${_BREW_LLVM_VERSION} xctoolchain for Xcode + CMake")
message(STATUS "  Identifier : ${_BREW_LLVM_TC_ID}")
message(STATUS "  Path       : ${_XCODE_TC_PATH}")

# ── Compilers: same binaries as the xctoolchain (usr/bin, not brew .../bin) ─
set(_XCODE_TC_CLANG "${_XCODE_TC_PATH}/usr/bin/clang")
set(_XCODE_TC_CLANGXX "${_XCODE_TC_PATH}/usr/bin/clang++")
if(EXISTS "${_XCODE_TC_CLANG}" AND EXISTS "${_XCODE_TC_CLANGXX}")
  set(CMAKE_C_COMPILER "${_XCODE_TC_CLANG}" CACHE FILEPATH "C compiler (LLVM xctoolchain)" FORCE)
  set(CMAKE_CXX_COMPILER "${_XCODE_TC_CLANGXX}" CACHE FILEPATH "C++ compiler (LLVM xctoolchain)"
                                                      FORCE
  )
  # libc++ / libLLVM under Homebrew prefix — same install as this xctoolchain
  set(PROJECT_LLVM_INSTALL_PREFIX "${_BREW_LLVM_PREFIX}"
      CACHE PATH "Homebrew LLVM prefix (linker -L/-rpath; matches xctoolchain)" FORCE
  )
  message(STATUS "  Compiler   : ${_XCODE_TC_CLANG}")
else()
  message(WARNING "xcode_toolchain: missing ${_XCODE_TC_CLANG} or clang++; "
                  "Xcode TOOLCHAINS is set but CMake compilers are unchanged (possible mismatch)."
  )
endif()
