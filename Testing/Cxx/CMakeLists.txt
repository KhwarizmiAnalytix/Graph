# =============================================================================
# Graph Library C++ Tests
# =============================================================================
xsigma_add_googletest()

file(GLOB_RECURSE test_sources LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/Test*.cpp")

# Custom main (not Gtest::gtest_main) -- see GraphTestMain.cpp for why: on macOS+ASan this
# binary needs to bypass a known third-party (TBB) static-destructor crash at exit that is
# unrelated to anything these tests check.
add_executable(GraphCxxTests ${test_sources} GraphTestMain.cpp)

target_compile_definitions(
  GraphCxxTests PRIVATE GRAPH_HAS_GTEST=1 GRAPH_GOOGLE_TEST USE_GTEST _VARIADIC_MAX=10
)

target_link_libraries(GraphCxxTests PRIVATE Gtest::gtest Graph::Graph)

set_target_properties(
  GraphCxxTests PROPERTIES FOLDER "Tests" VS_DEBUGGER_WORKING_DIRECTORY
                                          "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

add_test(NAME GraphCxxTests COMMAND GraphCxxTests
         WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

set_tests_properties(GraphCxxTests PROPERTIES TIMEOUT 120 LABELS "Graph;Unit")

if(COMMAND graph_set_target_output_directories)
  graph_set_target_output_directories(GraphCxxTests)
endif()

# =============================================================================
# Individual Benchmark Executables
# =============================================================================
if(GRAPH_ENABLE_BENCHMARK)
  # --- Google Benchmark setup (mirrors Parallel/Logging) ---
  if(NOT TARGET benchmark)
    set(_bench_external_found FALSE)
    if(XSIGMA_ENABLE_EXTERNAL)
      find_package(benchmark QUIET)
      if(benchmark_FOUND)
        message(STATUS "Found external Google Benchmark")
        _create_third_party_interface_targets(
          "Benchmark::benchmark=benchmark::benchmark|benchmark"
          "Benchmark::benchmark_main=benchmark::benchmark_main|benchmark_main"
        )
        set(_bench_external_found TRUE)
      endif()
    endif()
    if(NOT _bench_external_found)
      if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        string(APPEND CMAKE_CXX_FLAGS " -Wno-c2y-extensions")
      endif()
      if(NOT DEFINED HAVE_STD_REGEX)
        set(HAVE_STD_REGEX 1 CACHE INTERNAL "")
      endif()
      set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark tests" FORCE)
      set(BENCHMARK_ENABLE_EXCEPTIONS ON CACHE BOOL "Enable benchmark exceptions" FORCE)
      set(BENCHMARK_ENABLE_LTO OFF CACHE BOOL "Disable benchmark LTO" FORCE)
      set(BENCHMARK_USE_LIBCXX OFF CACHE BOOL "Disable benchmark libcxx" FORCE)
      set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "Disable benchmark werror" FORCE)
      set(BENCHMARK_FORCE_WERROR OFF CACHE BOOL "Disable benchmark force werror" FORCE)
      set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Disable benchmark install" FORCE)
      set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "Disable benchmark docs install" FORCE)
      set(BENCHMARK_ENABLE_DOXYGEN OFF CACHE BOOL "Disable benchmark doxygen" FORCE)
      set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "Disable benchmark gtest tests" FORCE)
      set(BENCHMARK_USE_BUNDLED_GTEST OFF CACHE BOOL "Don't use bundled gtest for benchmark" FORCE)
      set(BENCHMARK_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "Don't download dependencies" FORCE)
      add_subdirectory(
        "${XSIGMA_THIRDPARTY_DIR}/benchmark" "${CMAKE_BINARY_DIR}/ThirdParty/benchmark_build"
        EXCLUDE_FROM_ALL
      )
      _set_third_party_folder_properties(
        "benchmark" "${CMAKE_BINARY_DIR}/ThirdParty/benchmark_build"
      )
    endif()
  endif()
  _create_third_party_interface_targets(
    "Benchmark::benchmark=benchmark::benchmark|benchmark"
    "Benchmark::benchmark_main=benchmark::benchmark_main|benchmark_main"
  )
  if(TARGET Benchmark::benchmark AND NOT TARGET Graph::benchmark)
    add_library(Graph::benchmark ALIAS benchmark)
    add_library(Graph::benchmark_main ALIAS benchmark_main)
  endif()

  file(GLOB_RECURSE bench_sources LIST_DIRECTORIES false
       "${CMAKE_CURRENT_SOURCE_DIR}/Benchmark*.cpp"
  )

  foreach(_bench_source IN ITEMS ${bench_sources})
    get_filename_component(bench_name "${_bench_source}" NAME_WE)

    string(REPLACE "Benchmark" "" bench_suffix "${bench_name}")
    string(TOLOWER "${bench_suffix}" bench_suffix_lower)
    set(target_name "benchmark_graph_${bench_suffix_lower}")

    add_executable(${target_name} "${_bench_source}")

    target_link_libraries(
      ${target_name} PRIVATE Graph::benchmark Graph::benchmark_main Graph::Graph
    )

    set_target_properties(
      ${target_name} PROPERTIES FOLDER "Benchmarks" OUTPUT_NAME "${target_name}"
    )

    if(COMMAND graph_set_target_output_directories)
      graph_set_target_output_directories(${target_name})
    else()
      set_target_properties(
        ${target_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
      )
    endif()

    add_test(NAME ${target_name} COMMAND ${target_name} --benchmark_min_time=0.01s
             WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )

    set_tests_properties(${target_name} PROPERTIES TIMEOUT 600 LABELS "Benchmark;Performance;Graph")

    message(STATUS "✓ Benchmark target configured: ${target_name} (from ${bench_name}.cpp)")
  endforeach()
endif()
