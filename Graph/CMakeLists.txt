# =============================================================================
# Library/Graph — CMakeLists.txt
#
# Generic dependency-DAG task scheduler: build a graph of type-erased work
# items with declared dependencies (graph_builder), then execute it in
# parallel (graph_executor), respecting dependency order, on top of
# Parallel (`<parallel.h>`).
# =============================================================================
cmake_minimum_required(VERSION 3.16)

include(lto)
xsigma_lto_compute_default(_lto_default PROFILE SUPPORT)
set(GRAPH_LTO_MODE "${_lto_default}" CACHE STRING
                                           "LTO mode for Graph: off | thin | full | ipo | auto"
)
set_property(CACHE GRAPH_LTO_MODE PROPERTY STRINGS off thin full ipo auto)

option(GRAPH_ENABLE_COVERAGE "Enable coverage instrumentation for Graph" OFF)
option(GRAPH_ENABLE_TESTING "Build Graph test suite" ON)
option(GRAPH_ENABLE_EXAMPLES "Build Graph example programs" OFF)
option(GRAPH_ENABLE_GTEST "Enable GoogleTest for Graph" ON)
option(GRAPH_ENABLE_BENCHMARK "Enable Google Benchmark for Graph" OFF)

option(GRAPH_ENABLE_ICECC "Use Icecream (icecc) distributed compiler for Graph" OFF)
mark_as_advanced(GRAPH_ENABLE_ICECC)
option(GRAPH_ENABLE_CACHE "Enable compiler caching for the Graph target" ON)
mark_as_advanced(GRAPH_ENABLE_CACHE)
option(GRAPH_ENABLE_CLANGTIDY "Enable clang-tidy static analysis for Graph" OFF)
mark_as_advanced(GRAPH_ENABLE_CLANGTIDY)
option(GRAPH_ENABLE_FIX "Enable clang-tidy fix-errors and fix options for Graph" OFF)
mark_as_advanced(GRAPH_ENABLE_FIX)
option(GRAPH_ENABLE_IWYU "Enable include-what-you-use analysis for Graph" OFF)
mark_as_advanced(GRAPH_ENABLE_IWYU)
option(GRAPH_ENABLE_SANITIZER "Build Graph with sanitizer support (Clang/GCC only)" OFF)
set(GRAPH_SANITIZER_TYPE "address"
    CACHE STRING "Sanitizer type for Graph. Options: address, undefined, thread, memory, leak"
)
set_property(CACHE GRAPH_SANITIZER_TYPE PROPERTY STRINGS address undefined thread memory leak)
mark_as_advanced(GRAPH_ENABLE_SANITIZER GRAPH_SANITIZER_TYPE)
option(GRAPH_ENABLE_SPELL "Enable check-only spell checking for Graph" OFF)
mark_as_advanced(GRAPH_ENABLE_SPELL)
option(GRAPH_ENABLE_VALGRIND "Execute Graph test suite with Valgrind" OFF)
mark_as_advanced(GRAPH_ENABLE_VALGRIND)
set(GRAPH_LINKER_CHOICE "default" CACHE STRING
                                        "Linker for Graph: default, lld, mold, gold, lld-link"
)
set_property(CACHE GRAPH_LINKER_CHOICE PROPERTY STRINGS default lld mold gold lld-link)
mark_as_advanced(GRAPH_LINKER_CHOICE)
set(GRAPH_CACHE_BACKEND "none"
    CACHE STRING "Compiler cache backend for Graph. Options: none, ccache, sccache, buildcache"
)
set_property(CACHE GRAPH_CACHE_BACKEND PROPERTY STRINGS none ccache sccache buildcache)
mark_as_advanced(GRAPH_CACHE_BACKEND)

set(GRAPH_CXX_STANDARD "20" CACHE STRING "C++ standard for Graph. Options: 11 14 17 20 23")
set_property(CACHE GRAPH_CXX_STANDARD PROPERTY STRINGS 11 14 17 20 23)
if(NOT GRAPH_CXX_STANDARD MATCHES "^(11|14|17|20|23)$")
  message(FATAL_ERROR "GRAPH_CXX_STANDARD must be one of: 11 14 17 20 23")
endif()

include(CheckCXXSourceCompiles)
include(CheckCXXSourceRuns)
include(CheckSymbolExists)
include(compiler_checks)
include(checks)
include(build_type)
include(platform)
include(cache)
include(CMakeDependentOption)
include(helper_macros)
if(GRAPH_ENABLE_SANITIZER)
  include(sanitize)
endif()
if(GRAPH_ENABLE_SPELL)
  include(spell)
endif()
if(GRAPH_ENABLE_VALGRIND)
  include(valgrind)
endif()
if(GRAPH_ENABLE_COVERAGE)
  include(coverage)
elseif(MSVC)
  string(APPEND CMAKE_EXE_LINKER_FLAGS " /DEBUG:FULL")
  string(APPEND CMAKE_SHARED_LINKER_FLAGS " /DEBUG:FULL")
endif()

set(graph_headers "")
set(graph_sources "")
set(CMAKE_CXX_STANDARD ${GRAPH_CXX_STANDARD})

file(GLOB_RECURSE graph_headers LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
file(GLOB_RECURSE graph_sources LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(FILTER graph_headers EXCLUDE REGEX ".*/Testing/.*")
list(FILTER graph_sources EXCLUDE REGEX ".*/Testing/.*")

add_library(Graph ${graph_sources} ${graph_headers})
xsigma_target_apply_lto(Graph GRAPH)
if(GRAPH_ENABLE_SANITIZER)
  xsigma_configure_sanitizer(GRAPH)
endif()
xsigma_find_linker(${GRAPH_LINKER_CHOICE} Graph GRAPH_LTO_MODE)
xsigma_target_apply_cache(Graph GRAPH_ENABLE_CACHE GRAPH_CACHE_BACKEND)
xsigma_target_optional_icecc(Graph GRAPH_ENABLE_ICECC)

# Graph always links Parallel::Parallel. Parallel is pure third-party
# (ThirdParty/CMakeLists.txt) with no XSigma-library dependency of its own,
# so this keeps Graph's dependency footprint minimal. Clients include
# `<parallel.h>`.
xsigma_add_parallel()
set(GRAPH_DEPENDENCY_LIBS "")
if(NOT TARGET Parallel::Parallel)
  message(
    FATAL_ERROR
      "Graph requires Parallel::Parallel. xsigma_add_parallel() should have provided it — "
      "is ThirdParty/Parallel initialized?"
  )
endif()
list(APPEND GRAPH_DEPENDENCY_LIBS Parallel::Parallel)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(Graph PUBLIC GRAPH_SHARED_DEFINE PRIVATE GRAPH_BUILDING_DLL)
  if(COMMAND xsigma_set_target_output_directories)
    xsigma_set_target_output_directories(Graph)
  endif()
else()
  target_compile_definitions(Graph PUBLIC GRAPH_STATIC_DEFINE)
endif()

target_link_libraries(Graph PUBLIC ${GRAPH_DEPENDENCY_LIBS})

add_library(Graph::Graph ALIAS Graph)

xsigma_module_create_filters("${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(
  Graph PROPERTIES CXX_STANDARD ${GRAPH_CXX_STANDARD} CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(Graph PRIVATE /WX)
else()
  target_compile_options(Graph PRIVATE -include cstdlib)
endif()

target_include_directories(
  Graph
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Testing>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/Testing>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/Testing/GraphTest.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/Testing/GraphTest.h" @ONLY
)

if(GRAPH_ENABLE_CLANGTIDY)
  include(clang_tidy)
  xsigma_target_clang_tidy(Graph ${GRAPH_ENABLE_FIX})
endif()
if(GRAPH_ENABLE_IWYU)
  include(iwyu)
  xsigma_apply_iwyu(Graph)
endif()

install(
  TARGETS Graph
  EXPORT GraphTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES
  DESTINATION include
)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/" DESTINATION include/Graph
        FILES_MATCHING PATTERN "*.h" PATTERN "Testing" EXCLUDE)

if(GRAPH_ENABLE_TESTING)
  add_subdirectory(Testing/Cxx)
endif()
if(GRAPH_ENABLE_VALGRIND AND GRAPH_ENABLE_TESTING)
  xsigma_apply_valgrind_timeouts()
endif()

message("")
message("******** Graph module (CMake flags) ********")
message("  Cxx standard     : ${GRAPH_CXX_STANDARD}")
message("  Cache            : ${GRAPH_ENABLE_CACHE}")
message("  Cache backend    : ${GRAPH_CACHE_BACKEND}")
message("  Icecc            : ${GRAPH_ENABLE_ICECC}")
xsigma_print_lto_linker_summary(GRAPH)
message("  Coverage         : ${GRAPH_ENABLE_COVERAGE}")
message("  Testing          : ${GRAPH_ENABLE_TESTING}")
message("  Gtest            : ${GRAPH_ENABLE_GTEST}")
message("  Benchmark        : ${GRAPH_ENABLE_BENCHMARK}")
message("  Clang-tidy       : ${GRAPH_ENABLE_CLANGTIDY}")
message("  Sanitizer        : ${GRAPH_ENABLE_SANITIZER}")
message("  Valgrind         : ${GRAPH_ENABLE_VALGRIND}")
message("  Parallel         : required (Parallel::Parallel)")
message("")
