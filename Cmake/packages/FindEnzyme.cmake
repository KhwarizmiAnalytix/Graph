# FindEnzyme.cmake
# ----------------
# Locate the Enzyme AD LLVM/clang plugin (LLDEnzyme / ClangEnzyme / LLVMEnzyme).
#
# Use with module mode (default for this name): find_package(Enzyme [REQUIRED] [QUIET])
#
# CMAKE_MODULE_PATH must include the directory containing this file (e.g. Cmake/packages). CONFIG
# mode: use EnzymeConfig.cmake in the same directory, e.g. find_package(Enzyme CONFIG REQUIRED PATHS
# "${CMAKE_SOURCE_DIR}/Cmake/packages")
#
# Input cache variable (XSigma defines it in enzyme.cmake before find_package): ENZYME_PLUGIN_PATH
# - Optional explicit path to the plugin (.so/.dylib/.dll)
#
# Result variables: Enzyme_FOUND Enzyme_PLUGIN_LIBRARY - Path to the Enzyme plugin shared library
#
# Legacy compatibility (set when Enzyme_FOUND): ENZYME_PLUGIN_LIBRARY

include(FindPackageHandleStandardArgs)

set(Enzyme_PLUGIN_LIBRARY "Enzyme_PLUGIN_LIBRARY-NOTFOUND")

if(ENZYME_PLUGIN_PATH AND EXISTS "${ENZYME_PLUGIN_PATH}")
  set(Enzyme_PLUGIN_LIBRARY "${ENZYME_PLUGIN_PATH}")
  if(NOT Enzyme_FIND_QUIETLY)
    message(STATUS "Using user-specified Enzyme plugin: ${Enzyme_PLUGIN_LIBRARY}")
  endif()
else()
  if(WIN32)
    set(_enzyme_search_paths
        "$ENV{LLVM_DIR}/bin"
        "$ENV{LLVM_DIR}/lib"
        "C:/Program Files/Enzyme/bin"
        "C:/Program Files/Enzyme/lib"
        "C:/Program Files (x86)/Enzyme/bin"
        "C:/Program Files (x86)/Enzyme/lib"
        "C:/Program Files/LLVM/bin"
        "C:/Program Files/LLVM/lib"
        "C:/Program Files (x86)/LLVM/bin"
        "C:/Program Files (x86)/LLVM/lib"
    )
  else()
    set(_enzyme_search_paths
        "/usr/lib" "/usr/local/lib" "/opt/homebrew/lib" "/opt/homebrew/opt/llvm/lib"
        "$ENV{LLVM_DIR}/lib" "${CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN}/lib"
    )
  endif()

  get_filename_component(_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  get_filename_component(_llvm_root "${_compiler_dir}" DIRECTORY)
  if(WIN32)
    list(APPEND _enzyme_search_paths "${_llvm_root}/bin" "${_llvm_root}/lib")
  else()
    list(APPEND _enzyme_search_paths "${_llvm_root}/lib")
  endif()

  string(REGEX MATCH "^([0-9]+)" _llvm_major_version "${CMAKE_CXX_COMPILER_VERSION}")

  if(NOT Enzyme_FIND_QUIETLY)
    message(STATUS "Searching for Enzyme plugin...")
    message(STATUS "  Compiler version: ${CMAKE_CXX_COMPILER_VERSION}")
    message(STATUS "  LLVM major version: ${_llvm_major_version}")
    message(STATUS "  Search paths: ${_enzyme_search_paths}")
  endif()

  set(_found_enzyme_files)
  foreach(_search_path ${_enzyme_search_paths})
    if(WIN32)
      file(
        GLOB
        _enzyme_candidates
        "${_search_path}/LLDEnzyme-${CMAKE_CXX_COMPILER_VERSION}.dll"
        "${_search_path}/LLDEnzyme-${_llvm_major_version}.dll"
        "${_search_path}/LLDEnzyme.dll"
        "${_search_path}/LLDEnzyme-*.dll"
        "${_search_path}/ClangEnzyme-${CMAKE_CXX_COMPILER_VERSION}.dll"
        "${_search_path}/ClangEnzyme-${_llvm_major_version}.dll"
        "${_search_path}/ClangEnzyme.dll"
        "${_search_path}/ClangEnzyme-*.dll"
        "${_search_path}/LLVMEnzyme-${CMAKE_CXX_COMPILER_VERSION}.dll"
        "${_search_path}/LLVMEnzyme-${_llvm_major_version}.dll"
        "${_search_path}/LLVMEnzyme.dll"
        "${_search_path}/LLVMEnzyme-*.dll"
      )
    elseif(APPLE)
      file(
        GLOB
        _enzyme_candidates
        "${_search_path}/LLDEnzyme-${CMAKE_CXX_COMPILER_VERSION}.dylib"
        "${_search_path}/LLDEnzyme-${_llvm_major_version}.dylib"
        "${_search_path}/LLDEnzyme.dylib"
        "${_search_path}/LLDEnzyme-*.dylib"
        "${_search_path}/ClangEnzyme-${CMAKE_CXX_COMPILER_VERSION}.dylib"
        "${_search_path}/ClangEnzyme-${_llvm_major_version}.dylib"
        "${_search_path}/ClangEnzyme.dylib"
        "${_search_path}/ClangEnzyme-*.dylib"
        "${_search_path}/LLVMEnzyme-${CMAKE_CXX_COMPILER_VERSION}.dylib"
        "${_search_path}/LLVMEnzyme-${_llvm_major_version}.dylib"
        "${_search_path}/LLVMEnzyme.dylib"
        "${_search_path}/LLVMEnzyme-*.dylib"
      )
    else()
      file(
        GLOB
        _enzyme_candidates
        "${_search_path}/LLDEnzyme-${CMAKE_CXX_COMPILER_VERSION}.so"
        "${_search_path}/LLDEnzyme-${_llvm_major_version}.so"
        "${_search_path}/LLDEnzyme.so"
        "${_search_path}/LLDEnzyme-*.so"
        "${_search_path}/ClangEnzyme-${CMAKE_CXX_COMPILER_VERSION}.so"
        "${_search_path}/ClangEnzyme-${_llvm_major_version}.so"
        "${_search_path}/ClangEnzyme.so"
        "${_search_path}/ClangEnzyme-*.so"
        "${_search_path}/LLVMEnzyme-${CMAKE_CXX_COMPILER_VERSION}.so"
        "${_search_path}/LLVMEnzyme-${_llvm_major_version}.so"
        "${_search_path}/LLVMEnzyme.so"
        "${_search_path}/LLVMEnzyme-*.so"
      )
    endif()
    if(_enzyme_candidates)
      list(APPEND _found_enzyme_files ${_enzyme_candidates})
    endif()
  endforeach()

  if(_found_enzyme_files)
    list(GET _found_enzyme_files 0 Enzyme_PLUGIN_LIBRARY)
    if(NOT Enzyme_FIND_QUIETLY)
      message(STATUS "  Found candidates: ${_found_enzyme_files}")
      message(STATUS "  Result: ${Enzyme_PLUGIN_LIBRARY}")
      message(STATUS "Found Enzyme plugin: ${Enzyme_PLUGIN_LIBRARY}")
    endif()
  elseif(NOT Enzyme_FIND_QUIETLY)
    message(STATUS "  Result: ")
  endif()
endif()

find_package_handle_standard_args(Enzyme FOUND_VAR Enzyme_FOUND REQUIRED_VARS Enzyme_PLUGIN_LIBRARY)

if(Enzyme_FOUND)
  set(ENZYME_PLUGIN_LIBRARY "${Enzyme_PLUGIN_LIBRARY}" CACHE FILEPATH "Enzyme plugin library path"
                                                             FORCE
  )
  mark_as_advanced(ENZYME_PLUGIN_LIBRARY)
endif()

# After discovery, validate that the found DLL is ABI-compatible with the active compiler.  Two
# conditions must both hold: 1. DLL LLVM major version == compiler LLVM major version 2. On MSVC
# frontend (clang-cl): DLL must NOT be a MinGW build (MinGW Enzyme depends on libstdc++ and
# libLLVM-N.dll from MSYS2 — those DLLs are absent in the native Windows environment, causing 0x7E
# at build time regardless of whether LLVM_ENABLE_PLUGINS is set).
#
# The DLL filename encodes the LLVM version it was compiled against, e.g.: ClangEnzyme-21.dll  ->
# LLVM 21 LLVMEnzyme-22.dll   -> LLVM 22 The MinGW vs MSVC ABI distinction is detected by checking
# whether the DLL was installed into the MSYS2 prefix (path contains "msys" or "mingw") or by
# verifying a LLVMConfig.cmake with LLVM_ENABLE_PLUGINS=ON is reachable from the compiler.
if(Enzyme_FOUND AND WIN32)
  string(REGEX MATCH "Enzyme-([0-9]+)\\." _enzyme_ver_match "${Enzyme_PLUGIN_LIBRARY}")
  set(_enzyme_dll_major "${CMAKE_MATCH_1}")

  string(REGEX MATCH "^([0-9]+)" _enzyme_compiler_major "${CMAKE_CXX_COMPILER_VERSION}")

  # Check 1: version mismatch between DLL and compiler
  set(_enzyme_version_ok TRUE)
  if(_enzyme_dll_major AND _enzyme_compiler_major)
    if(NOT _enzyme_dll_major STREQUAL _enzyme_compiler_major)
      set(_enzyme_version_ok FALSE)
    endif()
  endif()

  # Check 2: MinGW-built DLL used with a native Windows compiler. Applies to both clang-cl (MSVC
  # frontend) and native clang++ targeting x86_64-pc-windows-msvc (GNU frontend, MSVC ABI).  In both
  # cases: - libLLVM-N.dll and libstdc++-6.dll are MSYS2-only DLLs absent from the native PATH → DLL
  # fails to load with error 0x7E. - The LLVM shipped with VS or from llvm.org has
  # LLVM_ENABLE_PLUGINS=OFF so the host cannot load any plugin regardless of DLL provenance.
  set(_enzyme_abi_ok TRUE)
  set(_enzyme_is_native_win_compiler FALSE)
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(_enzyme_is_native_win_compiler TRUE)
  elseif(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # clang++ targeting windows-msvc (native ABI) also cannot load MinGW DLLs
    if(CMAKE_CXX_COMPILER_TARGET MATCHES "windows-msvc" OR CMAKE_CXX_COMPILER_VERSION MATCHES
                                                           "^[0-9]"
       AND NOT CMAKE_CXX_COMPILER MATCHES "[Mm]sys|[Mm]ingw|ucrt|clang64"
    )
      set(_enzyme_is_native_win_compiler TRUE)
    endif()
  endif()

  if(_enzyme_is_native_win_compiler)
    # Flag MinGW-built DLLs (path contains msys/mingw markers)
    if(Enzyme_PLUGIN_LIBRARY MATCHES "[Mm]sys|[Mm]ingw|[Mm]inGW|ucrt64|clang64")
      set(_enzyme_abi_ok FALSE)
    else()
      # Also check the LLVM next to the compiler for LLVM_ENABLE_PLUGINS
      get_filename_component(_ecl_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
      get_filename_component(_ecl_root "${_ecl_bin}" DIRECTORY)
      set(_ecl_cfg "${_ecl_root}/lib/cmake/llvm/LLVMConfig.cmake")
      if(EXISTS "${_ecl_cfg}")
        file(STRINGS "${_ecl_cfg}" _ecl_plugins REGEX "LLVM_ENABLE_PLUGINS")
        if(_ecl_plugins MATCHES "LLVM_ENABLE_PLUGINS OFF")
          set(_enzyme_abi_ok FALSE)
        endif()
      endif()
      unset(_ecl_bin)
      unset(_ecl_root)
      unset(_ecl_cfg)
      unset(_ecl_plugins)
    endif()
  endif()
  unset(_enzyme_is_native_win_compiler)

  if(NOT _enzyme_version_ok OR NOT _enzyme_abi_ok)
    if(NOT _enzyme_version_ok)
      set(_enzyme_mismatch_reason
          "DLL version mismatch: ClangEnzyme-${_enzyme_dll_major}.dll was built\n"
          "against LLVM ${_enzyme_dll_major}, but the active compiler is\n"
          "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} (LLVM ${_enzyme_compiler_major}).\n"
      )
    else()
      set(_enzyme_mismatch_reason
          "ABI mismatch: the found DLL appears to be a MinGW/MSYS2 build\n"
          "(${Enzyme_PLUGIN_LIBRARY})\n"
          "but the active compiler uses the MSVC C++ ABI (clang-cl).\n"
          "MinGW Enzyme DLLs depend on libstdc++ and libLLVM-N.dll from MSYS2\n"
          "which are not available in the native Windows build environment.\n"
      )
    endif()
    message(
      WARNING "\n"
              "================================================================================\n"
              "WARNING: Enzyme plugin is incompatible with the active compiler\n"
              "================================================================================\n"
              "\n"
              ${_enzyme_mismatch_reason}
              "\n"
              "TO FIX:\n"
              "\n"
              "Option A — Use the MSYS2/MinGW64 Ninja build (already works):\n"
              "   python setup.py config.build.test.ninja.enzyme\n"
              "   The installed ClangEnzyme-${_enzyme_dll_major}.dll is compatible with\n"
              "   MSYS2 MinGW64 clang (LLVM ${_enzyme_dll_major}, LLVM_ENABLE_PLUGINS=ON).\n"
              "\n"
              "Option B — Rebuild Enzyme for the VS/clang-cl toolchain:\n"
              "   Requires an LLVM build with LLVM_ENABLE_PLUGINS=ON and MSVC ABI.\n"
              "   Official LLVM Windows releases and VS-bundled clang ship with\n"
              "   LLVM_ENABLE_PLUGINS=OFF and cannot host Enzyme plugins.\n"
              "   You must build LLVM from source:\n"
              "     cmake -G Ninja ../llvm-project/llvm\n"
              "       -DLLVM_ENABLE_PLUGINS=ON\n"
              "       -DLLVM_BUILD_LLVM_DYLIB=ON\n"
              "       -DLLVM_LINK_LLVM_DYLIB=ON\n"
              "       -DCMAKE_BUILD_TYPE=Release\n"
              "   Then build Enzyme against that LLVM and reinstall.\n"
              "\n"
              "Disabling Enzyme support for this build.\n"
              "================================================================================\n"
    )
    unset(_enzyme_ver_match)
    unset(_enzyme_dll_major)
    unset(_enzyme_compiler_major)
    unset(_enzyme_version_ok)
    unset(_enzyme_abi_ok)
    unset(_enzyme_mismatch_reason)
    set(CORE_ENABLE_ENZYME OFF CACHE BOOL "Enable Enzyme automatic differentiation support" FORCE)
    return()
  endif()
  unset(_enzyme_ver_match)
  unset(_enzyme_dll_major)
  unset(_enzyme_compiler_major)
  unset(_enzyme_version_ok)
  unset(_enzyme_abi_ok)
endif()
