# FindTBB.cmake - Find Intel Threading Building Blocks (TBB) Enhanced TBB finder with support for
# multiple installation methods Supports: vcpkg, apt, homebrew, Intel oneAPI, manual installations

cmake_minimum_required(VERSION 3.15)

# Prevent repeated execution
if(TARGET TBB::tbb)
  set(TBB_FOUND TRUE)
  return()
endif()

# Set module information
set(TBB_FIND_VERSION_MAJOR 2021)
set(TBB_FIND_VERSION_MINOR 0)
set(TBB_FIND_VERSION "${TBB_FIND_VERSION_MAJOR}.${TBB_FIND_VERSION_MINOR}")

# Find TBB root directory
if(NOT TBB_ROOT)
  if(DEFINED ENV{TBB_ROOT})
    set(TBB_ROOT $ENV{TBB_ROOT})
  elseif(DEFINED ENV{TBBROOT})
    set(TBB_ROOT $ENV{TBBROOT})
  elseif(TBB_DIR)
    set(TBB_ROOT ${TBB_DIR})
  endif()
endif()

if(TBB_ROOT)
  file(TO_CMAKE_PATH "${TBB_ROOT}" TBB_ROOT)
endif()

# Set up search paths
set(_tbb_search_paths)
if(TBB_ROOT)
  list(APPEND _tbb_search_paths ${TBB_ROOT})
  get_filename_component(_tbb_parent "${TBB_ROOT}" DIRECTORY)
  list(APPEND _tbb_search_paths ${_tbb_parent})
endif()

# Add common system paths for TBB
list(
  APPEND
  _tbb_search_paths
  # Standard Unix paths
  /usr/local
  /usr
  /opt/local # MacPorts
  # Intel-specific paths
  /opt/intel/tbb
  /opt/intel/oneapi/tbb/latest
  /opt/intel/oneapi/tbb/2021.11.0
  /opt/intel/oneapi/tbb/2021.10.0
  # Windows Intel paths
  $ENV{PROGRAMFILES}/Intel/TBB
  $ENV{PROGRAMFILES}/Intel/oneAPI/tbb/latest
  "C:/Program Files/Intel/TBB"
  "C:/Program Files/Intel/oneAPI/tbb/latest"
  # vcpkg paths (will be handled by CMAKE_PREFIX_PATH usually) Homebrew paths (will be handled by
  # CMAKE_PREFIX_PATH usually)
)

# Find include directory
find_path(
  TBB_INCLUDE_DIR
  NAMES tbb/tbb.h
  PATHS ${_tbb_search_paths}
  PATH_SUFFIXES include
  NO_DEFAULT_PATH
)

find_path(TBB_INCLUDE_DIR NAMES tbb/tbb.h PATH_SUFFIXES include)

if(NOT TBB_INCLUDE_DIR)
  if(TBB_FIND_REQUIRED)
    message(
      FATAL_ERROR "Could not find TBB include directory. Searched paths: ${_tbb_search_paths}"
    )
  elseif(NOT TBB_FIND_QUIETLY)
    message(STATUS "TBB include directory not found. Searched paths: ${_tbb_search_paths}")
  endif()
  return()
endif()

if(NOT TBB_FIND_QUIETLY)
  message(STATUS "Found TBB include directory: ${TBB_INCLUDE_DIR}")
endif()

# Set up library search paths based on platform and architecture
set(_tbb_lib_paths)
foreach(path ${_tbb_search_paths})
  # Determine architecture suffix
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_arch_suffix "intel64")
    set(_arch_suffix_alt "x64")
  else()
    set(_arch_suffix "ia32")
    set(_arch_suffix_alt "x86")
  endif()

  # Platform-specific library paths
  if(WIN32)
    # Windows paths with various compiler versions
    list(
      APPEND
      _tbb_lib_paths
      ${path}/lib/${_arch_suffix}/vc14
      ${path}/lib/${_arch_suffix}/vc_mt
      ${path}/lib/${_arch_suffix}
      ${path}/lib
      ${path}/bin/${_arch_suffix}
      ${path}/bin
    )
  elseif(APPLE)
    # macOS paths
    list(APPEND _tbb_lib_paths ${path}/lib ${path}/lib/${_arch_suffix} ${path}/lib/darwin)
  else()
    # Linux and other Unix-like systems
    list(
      APPEND
      _tbb_lib_paths
      ${path}/lib
      ${path}/lib/${_arch_suffix}
      ${path}/lib64
      ${path}/lib/x86_64-linux-gnu
      ${path}/lib/i386-linux-gnu
    )
  endif()
endforeach()

# Set up platform-specific library names
if(WIN32)
  set(_tbb_lib_names tbb12 tbb tbb_debug)
  set(_tbb_malloc_lib_names tbbmalloc tbbmalloc_debug)
elseif(APPLE)
  set(_tbb_lib_names tbb libtbb.dylib)
  set(_tbb_malloc_lib_names tbbmalloc libtbbmalloc.dylib)
else()
  # Linux and other Unix-like systems
  set(_tbb_lib_names tbb libtbb.so libtbb.so.12 libtbb.so.2)
  set(_tbb_malloc_lib_names tbbmalloc libtbbmalloc.so libtbbmalloc.so.2)
endif()

# Find TBB libraries
find_library(
  TBB_LIBRARY_RELEASE
  NAMES ${_tbb_lib_names}
  PATHS ${_tbb_lib_paths}
  NO_DEFAULT_PATH
)

find_library(TBB_LIBRARY_RELEASE NAMES ${_tbb_lib_names})

find_library(
  TBB_MALLOC_LIBRARY_RELEASE
  NAMES ${_tbb_malloc_lib_names}
  PATHS ${_tbb_lib_paths}
  NO_DEFAULT_PATH
)

find_library(TBB_MALLOC_LIBRARY_RELEASE NAMES ${_tbb_malloc_lib_names})

# Find debug libraries (optional)
if(WIN32)
  set(_tbb_debug_lib_names tbb_debug tbb12_debug)
  set(_tbb_malloc_debug_lib_names tbbmalloc_debug)

  find_library(
    TBB_LIBRARY_DEBUG
    NAMES ${_tbb_debug_lib_names}
    PATHS ${_tbb_lib_paths}
    NO_DEFAULT_PATH
  )

  find_library(TBB_LIBRARY_DEBUG NAMES ${_tbb_debug_lib_names})

  find_library(
    TBB_MALLOC_LIBRARY_DEBUG
    NAMES ${_tbb_malloc_debug_lib_names}
    PATHS ${_tbb_lib_paths}
    NO_DEFAULT_PATH
  )

  find_library(TBB_MALLOC_LIBRARY_DEBUG NAMES ${_tbb_malloc_debug_lib_names})
endif()

# Handle REQUIRED and QUIET arguments
include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
  TBB
  FOUND_VAR TBB_FOUND
  REQUIRED_VARS TBB_INCLUDE_DIR TBB_LIBRARY_RELEASE
  VERSION_VAR TBB_FIND_VERSION
  HANDLE_COMPONENTS
)

if(NOT TBB_FOUND)
  if(NOT TBB_FIND_QUIETLY)
    message(STATUS "TBB not found. Searched library paths: ${_tbb_lib_paths}")
    message(STATUS "TBB library names searched: ${_tbb_lib_names}")
  endif()
  return()
endif()

if(NOT TBB_FIND_QUIETLY)
  message(STATUS "Found TBB library: ${TBB_LIBRARY_RELEASE}")
  if(TBB_MALLOC_LIBRARY_RELEASE)
    message(STATUS "Found TBB malloc library: ${TBB_MALLOC_LIBRARY_RELEASE}")
  endif()
endif()

# Create imported targets
if(NOT TARGET TBB::tbb)
  add_library(TBB::tbb UNKNOWN IMPORTED)
  set_target_properties(
    TBB::tbb PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${TBB_INCLUDE_DIR}" IMPORTED_LOCATION
                                                                           "${TBB_LIBRARY_RELEASE}"
  )

  # Set debug library if available
  if(TBB_LIBRARY_DEBUG)
    set_target_properties(TBB::tbb PROPERTIES IMPORTED_LOCATION_DEBUG "${TBB_LIBRARY_DEBUG}")
  endif()

  # Add platform-specific link requirements
  if(UNIX AND NOT APPLE)
    set_target_properties(TBB::tbb PROPERTIES INTERFACE_LINK_LIBRARIES "pthread;dl")
  elseif(WIN32)
    # Windows may need additional libraries
    set_target_properties(TBB::tbb PROPERTIES INTERFACE_LINK_LIBRARIES "")
  endif()

  if(NOT TBB_FIND_QUIETLY)
    message(STATUS "Created TBB::tbb imported target")
  endif()
endif()

if(TBB_MALLOC_LIBRARY_RELEASE AND NOT TARGET TBB::tbbmalloc)
  add_library(TBB::tbbmalloc UNKNOWN IMPORTED)
  set_target_properties(
    TBB::tbbmalloc PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${TBB_INCLUDE_DIR}"
                              IMPORTED_LOCATION "${TBB_MALLOC_LIBRARY_RELEASE}"
  )

  # Set debug library if available
  if(TBB_MALLOC_LIBRARY_DEBUG)
    set_target_properties(
      TBB::tbbmalloc PROPERTIES IMPORTED_LOCATION_DEBUG "${TBB_MALLOC_LIBRARY_DEBUG}"
    )
  endif()

  if(NOT TBB_FIND_QUIETLY)
    message(STATUS "Created TBB::tbbmalloc imported target")
  endif()
elseif(NOT TBB_FIND_QUIETLY AND NOT TBB_MALLOC_LIBRARY_RELEASE)
  message(STATUS "TBB malloc library not found - TBB::tbbmalloc target not created")
endif()

# Mark variables as advanced
mark_as_advanced(
  TBB_INCLUDE_DIR TBB_LIBRARY_RELEASE TBB_LIBRARY_DEBUG TBB_MALLOC_LIBRARY_RELEASE
  TBB_MALLOC_LIBRARY_DEBUG
)
