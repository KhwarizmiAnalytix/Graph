# Make sure Crun is linked in with the native compiler; it is not used by default for shared
# libraries and is required for things like Java to work.
if(CMAKE_SYSTEM MATCHES "SunOS.*")
  if(NOT CMAKE_COMPILER_IS_GNUCXX)
    message(STATUS "SunOS + Sun CC: searching for Crun/Cstd runtime libraries")
    find_library(PROJECT_SUNCC_CRUN_LIBRARY Crun /opt/SUNWspro/lib)
    if(PROJECT_SUNCC_CRUN_LIBRARY)
      message(STATUS "  Linking Crun: ${PROJECT_SUNCC_CRUN_LIBRARY}")
      link_libraries(${PROJECT_SUNCC_CRUN_LIBRARY})
    endif()
    find_library(PROJECT_SUNCC_CSTD_LIBRARY Cstd /opt/SUNWspro/lib)
    if(PROJECT_SUNCC_CSTD_LIBRARY)
      message(STATUS "  Linking Cstd: ${PROJECT_SUNCC_CSTD_LIBRARY}")
      link_libraries(${PROJECT_SUNCC_CSTD_LIBRARY})
    endif()
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  message(STATUS "Emscripten: enabling WebAssembly exception handling (-fwasm-exceptions)")
  # Enable exceptions because XSigma and third party code rely on C++ exceptions. Allow C++ to
  # catch exceptions. Emscripten disables it by default due to high overhead. Generate helper
  # functions to get stack traces for uncaught exceptions
  string(APPEND CMAKE_CXX_FLAGS " -fwasm-exceptions")
  string(APPEND CMAKE_C_FLAGS " -fwasm-exceptions")
  string(APPEND CMAKE_EXE_LINKER_FLAGS " -fwasm-exceptions -sEXCEPTION_STACK_TRACES=1")
  string(APPEND CMAKE_SHARED_LINKER_FLAGS " -fwasm-exceptions -sEXCEPTION_STACK_TRACES=1")
  string(APPEND CMAKE_MODULE_LINKER_FLAGS " -fwasm-exceptions -sEXCEPTION_STACK_TRACES=1")
  # Consumers linking to XSigma also need to add the exception flag.
  if(TARGET XSigmaPlatform)
    target_link_options(XSigmaPlatform INTERFACE "-fwasm-exceptions" "-sEXCEPTION_STACK_TRACES=1")
  endif()
  if(PROJECT_WEBASSEMBLY_THREADS)
    message(STATUS "Emscripten: enabling pthreads (-pthread -Wno-pthreads-mem-growth)")
    # Remove after https://github.com/WebAssembly/design/issues/1271 is closed Set Wno flag globally
    # because even though the flag is added in XSigmaCompilerWarningFlags.cmake, wrapping tools do
    # not link with `XSigmaPlatform`
    string(APPEND CMAKE_CXX_FLAGS " -pthread -Wno-pthreads-mem-growth")
    string(APPEND CMAKE_C_FLAGS " -pthread -Wno-pthreads-mem-growth")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -pthread")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -pthread")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS " -pthread")
    # Consumers linking to XSigma also need to add the pthread flag.
    if(TARGET XSigmaPlatform)
      target_compile_options(XSigmaPlatform INTERFACE "-pthread" "-Wno-pthreads-mem-growth")
      target_link_options(XSigmaPlatform INTERFACE "-pthread")
    endif()
  endif()
  if(PROJECT_WEBASSEMBLY_64_BIT)
    message(STATUS "Emscripten: enabling 64-bit memory (-sMEMORY64=1)")
    string(APPEND CMAKE_CXX_FLAGS " -sMEMORY64=1")
    string(APPEND CMAKE_C_FLAGS " -sMEMORY64=1")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -sMEMORY64=1")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -sMEMORY64=1")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS " -sMEMORY64=1")
    # Consumers linking to XSigma also need to add the memory64 flag.
    if(TARGET XSigmaPlatform)
      target_compile_options(XSigmaPlatform INTERFACE "-sMEMORY64=1")
      target_link_options(XSigmaPlatform INTERFACE "-sMEMORY64=1")
    endif()
  endif()
endif()

# A GCC compiler.
if(CMAKE_COMPILER_IS_GNUCXX)
  message(STATUS "GCC compiler detected")
  if(WIN32)
    # The platform is gcc on cygwin.
    message(STATUS "  GCC/Cygwin: adding -mwin32, linking gdi32")
    string(APPEND CMAKE_CXX_FLAGS " -mwin32")
    string(APPEND CMAKE_C_FLAGS " -mwin32")
    link_libraries(-lgdi32)
  endif()
  if(MINGW)
    message(STATUS "  MinGW: adding -mthreads")
    string(APPEND CMAKE_CXX_FLAGS " -mthreads")
    string(APPEND CMAKE_C_FLAGS " -mthreads")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -mthreads")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -mthreads")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS " -mthreads")
  endif()
  if(CMAKE_SYSTEM MATCHES "SunOS.*")
    # Disable warnings that occur in X11 headers.
    if(DART_ROOT AND BUILD_TESTING)
      message(STATUS "  GCC/SunOS: suppressing unknown-pragmas warnings for X11 headers")
      string(APPEND CMAKE_CXX_FLAGS " -Wno-unknown-pragmas")
      string(APPEND CMAKE_C_FLAGS " -Wno-unknown-pragmas")
    endif()
  endif()
else()
  if(CMAKE_ANSI_CFLAGS)
    message(STATUS "Non-GCC: appending ANSI C flags: ${CMAKE_ANSI_CFLAGS}")
    string(APPEND CMAKE_C_FLAGS " ${CMAKE_ANSI_CFLAGS}")
  endif()
  if(CMAKE_SYSTEM MATCHES "OSF1-V.*")
    message(STATUS "OSF1: adding -timplicit_local -no_implicit_include")
    string(APPEND CMAKE_CXX_FLAGS " -timplicit_local -no_implicit_include")
  endif()
  if(CMAKE_SYSTEM MATCHES "AIX.*")
    message(
      STATUS "AIX: enabling RTTI (-qrtti=all), suppressing duplicate symbol warnings (-bhalt:5)"
    )
    # allow t-ypeid and d-ynamic_cast usage (normally off by default on xlC)
    string(APPEND CMAKE_CXX_FLAGS " -qrtti=all")
    # silence duplicate symbol warnings on AIX
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -bhalt:5")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -bhalt:5")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS " -bhalt:5")
  endif()
  if(CMAKE_SYSTEM MATCHES "HP-UX.*")
    message(STATUS "HP-UX: adding compatibility warning flags (+W2111 +W2236 +W4276)")
    string(APPEND CMAKE_C_FLAGS " +W2111 +W2236 +W4276")
    string(APPEND CMAKE_CXX_FLAGS " +W2111 +W2236 +W4276")
  endif()
endif()

# figure out whether the compiler might be the Intel compiler
set(_MAY_BE_INTEL_COMPILER FALSE)
if(UNIX)
  if(CMAKE_CXX_COMPILER_ID)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Intel")
      set(_MAY_BE_INTEL_COMPILER TRUE)
    endif()
  else()
    if(NOT CMAKE_COMPILER_IS_GNUCXX)
      set(_MAY_BE_INTEL_COMPILER TRUE)
    endif()
  endif()
endif()

# if so, test whether -i_dynamic is needed
if(_MAY_BE_INTEL_COMPILER)
  message(STATUS "Intel compiler detected: testing whether -i_dynamic is required")
  include(${CMAKE_CURRENT_LIST_DIR}/TestNO_ICC_IDYNAMIC_NEEDED.cmake)
  testno_icc_idynamic_needed(NO_ICC_IDYNAMIC_NEEDED ${CMAKE_CURRENT_LIST_DIR})
  if(NO_ICC_IDYNAMIC_NEEDED)
    message(STATUS "  Intel compiler: -i_dynamic not needed")
  else()
    message(STATUS "  Intel compiler: adding -i_dynamic")
    string(APPEND CMAKE_CXX_FLAGS " -i_dynamic")
  endif()
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "PGI")
  message(
    STATUS
      "PGI compiler: suppressing diagnostic 236 (constant value asserts) and 381 (redundant semicolons)"
  )
  # --diag_suppress=236 is for constant value asserts used for error handling This can be restricted
  # to the implementation and doesn't need to propagate
  string(APPEND CMAKE_CXX_FLAGS " --diag_suppress=236")

  # --diag_suppress=381 is for redundant semi-colons used in macros This needs to propagate to
  # anything that includes XSigma headers
  string(APPEND CMAKE_CXX_FLAGS " --diag_suppress=381")
endif()

if(MSVC)
  message(STATUS "MSVC compiler detected: applying MSVC-specific flags")
  # Use the highest warning level for visual c++ compiler. set(CMAKE_CXX_WARNING_LEVEL 4)
  # if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]") string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS
  # "${CMAKE_CXX_FLAGS}") else() set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4") endif() Enable C++20
  # support: /Zc:__cplusplus
  message(STATUS "  MSVC: enabling C++20 __cplusplus macro (/Zc:__cplusplus)")
  string(APPEND CMAKE_CXX_FLAGS " /Zc:__cplusplus")
  # Treat warnings as errors set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /WX") Disable C4244: conversion
  # warnings
  message(
    STATUS
      "  MSVC: disabling conversion/truncation/signed warnings (/wd4244 /wd4267 /wd4715 /wd4018)"
  )
  string(APPEND CMAKE_CXX_FLAGS " /wd4244 /wd4267 /wd4715 /wd4018")
  string(APPEND CMAKE_C_FLAGS " /wd4244 /wd4267 /wd4715 /wd4018")

  # Disable deprecation warnings for standard C and STL functions in VS2015+ and later
  add_definitions(-D_CRT_SECURE_NO_DEPRECATE -D_CRT_NONSTDC_NO_DEPRECATE -D_CRT_SECURE_NO_WARNINGS)
  add_definitions(-D_SCL_SECURE_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS)

  # Enable /MP flag for Visual Studio
  set(CMAKE_CXX_MP_FLAG ON CACHE BOOL "Build with /MP flag enabled")
  set(PROCESSOR_COUNT "$ENV{NUMBER_OF_PROCESSORS}")
  set(CMAKE_CXX_MP_NUM_PROCESSORS ${PROCESSOR_COUNT}
      CACHE STRING "The maximum number of processes for the /MP flag"
  )
  if(CMAKE_CXX_MP_FLAG AND NOT (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_GENERATOR MATCHES
                                                                           "Ninja")
  )
    message(STATUS "  MSVC: parallel compilation enabled (/MP${CMAKE_CXX_MP_NUM_PROCESSORS})")
    string(APPEND CMAKE_CXX_FLAGS " /MP${CMAKE_CXX_MP_NUM_PROCESSORS}")
    string(APPEND CMAKE_C_FLAGS " /MP${CMAKE_CXX_MP_NUM_PROCESSORS}")
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_GENERATOR MATCHES "Ninja")
    message(STATUS "  Clang/Ninja: skipping /MP — Ninja handles parallelism")
  endif()

  # Enable /bigobj for MSVC to allow larger symbol tables
  message(STATUS "  MSVC: enabling large object files (/bigobj)")
  string(APPEND CMAKE_CXX_FLAGS " /bigobj")
  string(APPEND CMAKE_C_FLAGS " /bigobj")

  # Enable faster PDB generation
  message(STATUS "  MSVC: enabling PDB debug info (/Zi)")
  string(APPEND CMAKE_CXX_FLAGS " /Zi")
  string(APPEND CMAKE_C_FLAGS " /Zi")

  # Use /utf-8 so that MSVC uses utf-8 in source files and object files
  message(STATUS "  MSVC: setting source/output encoding to UTF-8 (/utf-8)")
  string(APPEND CMAKE_CXX_FLAGS " /utf-8")
  string(APPEND CMAKE_C_FLAGS " /utf-8")

  # use /EHsc for exception handling
  message(STATUS "  MSVC: enabling structured exception handling (/EHsc)")
  string(APPEND CMAKE_CXX_FLAGS " /EHsc")
  string(APPEND CMAKE_C_FLAGS " /EHsc")
endif()

# windows.h (and CUDA's rpcndr.h pull-in) define min/max macros that break
# std::min/std::max in TBB headers. Must apply to MSVC and clang++ on Windows.
if(WIN32)
  add_definitions(-DNOMINMAX)
endif()

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(STATUS "Clang on Windows: applying Clang-specific flags")

  # Colored diagnostics — Ninja relays Clang's ANSI codes correctly; MSVC color codes are not
  string(APPEND CMAKE_CXX_FLAGS " -fcolor-diagnostics")
  string(APPEND CMAKE_C_FLAGS " -fcolor-diagnostics")

  # Suppress spurious warnings that fire when MSVC-style flags are mixed with Clang
  string(APPEND CMAKE_CXX_FLAGS " -Wno-unused-command-line-argument")
  string(APPEND CMAKE_C_FLAGS " -Wno-unused-command-line-argument")

  # MSVC CRT marks getenv/strcpy/etc. with __declspec(deprecated) under Clang;
  # _CRT_SECURE_NO_WARNINGS removes the #pragma form but not the __declspec form, so Clang still
  # fires -Wdeprecated-declarations
  string(APPEND CMAKE_CXX_FLAGS " -Wno-deprecated-declarations")
  string(APPEND CMAKE_C_FLAGS " -Wno-deprecated-declarations")

  # Third-party C code (e.g. mimalloc) uses old-style K&R function declarations without prototypes
  string(APPEND CMAKE_C_FLAGS " -Wno-strict-prototypes")

  if(CMAKE_GENERATOR MATCHES "Ninja")
    message(STATUS "  Clang/Ninja/Windows: applying Ninja-specific adjustments")

    if(NOT MSVC)
      # Plain clang++ driver: emit CodeView debug info so WinDbg / VS can read symbols
      string(APPEND CMAKE_CXX_FLAGS " -gcodeview")
      string(APPEND CMAKE_C_FLAGS " -gcodeview")
    endif()
  endif()
endif()

if(APPLE)
  message(STATUS "Applying macOS LLVM linker options")

  if(DEFINED PROJECT_LLVM_INSTALL_PREFIX AND PROJECT_LLVM_INSTALL_PREFIX)
    set(_xsigma_llvm_prefix "${PROJECT_LLVM_INSTALL_PREFIX}")
  elseif(EXISTS "/opt/homebrew/opt/llvm/lib")
    set(_xsigma_llvm_prefix "/opt/homebrew/opt/llvm")
  elseif(EXISTS "/usr/local/opt/llvm/lib")
    set(_xsigma_llvm_prefix "/usr/local/opt/llvm")
  else()
    set(_xsigma_llvm_prefix "/opt/homebrew/opt/llvm")
  endif()

  set(LLVM_LINK_FLAGS
      -L${_xsigma_llvm_prefix}/lib/c++ -L${_xsigma_llvm_prefix}/lib
      -Wl,-rpath,${_xsigma_llvm_prefix}/lib/c++ -Wl,-rpath,${_xsigma_llvm_prefix}/lib
  )

  # Add each flag only if not already included
  foreach(flag IN LISTS LLVM_LINK_FLAGS)
    string(FIND "${CMAKE_EXE_LINKER_FLAGS}" "${flag}" flag_found)
    if(flag_found EQUAL -1)
      add_link_options(${flag})
    endif()
  endforeach()
endif()

if(UNIX AND NOT APPLE)
  include(CheckCXXCompilerFlag)
  include(CheckCCompilerFlag)
  include(CheckCXXSourceCompiles)

  # Append flag to CMAKE_CXX_FLAGS and CMAKE_C_FLAGS if both compilers accept it.
  macro(_xsigma_add_compile_flag _flag)
    string(MAKE_C_IDENTIFIER "${_flag}" _id)
    check_cxx_compiler_flag("${_flag}" _CXX_HAS${_id})
    check_c_compiler_flag("${_flag}" _C_HAS${_id})
    if(_CXX_HAS${_id})
      string(APPEND CMAKE_CXX_FLAGS " ${_flag}")
    endif()
    if(_C_HAS${_id})
      string(APPEND CMAKE_C_FLAGS " ${_flag}")
    endif()
  endmacro()

  # Append flag to all linker flag variables if the linker accepts it. Uses
  # CMAKE_REQUIRED_LINK_OPTIONS (CMake >= 3.14) since check_linker_flag needs 3.18.
  macro(_xsigma_add_linker_flag _flag)
    string(MAKE_C_IDENTIFIER "${_flag}" _id)
    set(CMAKE_REQUIRED_LINK_OPTIONS "${_flag}")
    check_cxx_source_compiles("int main(){}" _LINKER_HAS${_id})
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if(_LINKER_HAS${_id})
      string(APPEND CMAKE_EXE_LINKER_FLAGS " ${_flag}")
      string(APPEND CMAKE_SHARED_LINKER_FLAGS " ${_flag}")
      string(APPEND CMAKE_MODULE_LINKER_FLAGS " ${_flag}")
    endif()
  endmacro()

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    _xsigma_add_compile_flag("-fPIC")
    _xsigma_add_compile_flag("-pipe")
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    _xsigma_add_compile_flag("-fcolor-diagnostics")
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    _xsigma_add_compile_flag("-fdiagnostics-color=always")
  endif()
endif()
