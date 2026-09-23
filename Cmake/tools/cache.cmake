# =============================================================================
# XSigma
# Compiler-Cache Configuration Module
#
# Provides xsigma_target_apply_cache() — a per-target compiler-cache setter. Each Library module
# declares its own XXX_ENABLE_CACHE / XXX_CACHE_BACKEND and calls this function after add_library()
# so that compiler caches are scoped to individual targets rather than globally overriding
# CMAKE_*_COMPILER_LAUNCHER.
#
# Supported backends: none | ccache | sccache | buildcache
#
# Note: compiler caches are safe to apply per-target because they only intercept compilation; they
# never alter flags or output semantics.
# =============================================================================
# include_guard(GLOBAL)

# xsigma_target_apply_cache(<target> <enable_var> <backend_var>) enable_var  – boolean CACHE
# variable controlling whether caching is active (e.g. LOGGING_ENABLE_CACHE) backend_var – string
# CACHE variable selecting the cache program (e.g. LOGGING_CACHE_BACKEND:
# none|ccache|sccache|buildcache)
function(xsigma_target_apply_cache target_name enable_var backend_var)
  if(NOT ${enable_var})
    message(STATUS "  -cache: ${target_name}: DISABLED")
    return()
  endif()

  set(_backend "${${backend_var}}")

  if(NOT _backend MATCHES "^(none|ccache|sccache|buildcache)$")
    message(FATAL_ERROR "Invalid ${backend_var}: '${_backend}'. "
                        "Valid options are: none, ccache, sccache, buildcache"
    )
  endif()

  if(_backend STREQUAL "none")
    message(STATUS "  -cache: ${target_name}: ENABLED. program: none")
    return()
  endif()

  string(TOUPPER "${_backend}" _backend_upper)
  set(_cache_var "XSIGMA_${_backend_upper}_PROGRAM")

  if(NOT DEFINED ${_cache_var} OR ${_cache_var} STREQUAL "")
    find_program(${_cache_var} NAMES ${_backend})
  endif()

  if(DEFINED ${_cache_var} AND NOT ${_cache_var} STREQUAL "")
    message(STATUS "  -cache: ${target_name}: ENABLED. program: ${${_cache_var}}")
    set_property(TARGET "${target_name}" PROPERTY CXX_COMPILER_LAUNCHER "${${_cache_var}}")
    set_property(TARGET "${target_name}" PROPERTY C_COMPILER_LAUNCHER "${${_cache_var}}")
    if(CMAKE_CUDA_COMPILER)
      set_property(TARGET "${target_name}" PROPERTY CUDA_COMPILER_LAUNCHER "${${_cache_var}}")
    endif()
  else()
    message(STATUS "  -cache: ${target_name}: ENABLED. program: ${_backend} NOT FOUND in PATH")
  endif()
endfunction()
