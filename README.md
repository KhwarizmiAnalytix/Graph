# Graph

Standalone C++20 Graph library extracted from XSigma. Public target: `Graph::Graph`.

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGRAPH_ENABLE_BENCHMARK=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Dependencies are pinned Git submodules in `ThirdParty`: Parallel, googletest, benchmark.
Logging and Profiler use product-only CMake overlays, matching XSigma's integration.
Existing dependency targets are reused when embedded. Tests remain owned by this repository;
set `GRAPH_ENABLE_TESTING=OFF` when consuming only the library.

```cmake
add_subdirectory(ThirdParty/Graph)
target_link_libraries(my_app PRIVATE Graph::Graph)
```

The supported standalone build is CMake. XSigma maintains its Bazel overlays separately.
Source headers and implementation are in `Graph/`; tests are in `Graph/Testing/`.

GPU toolchains and optional numerical backends require their corresponding SDKs.
