# Graph

[![CI](https://github.com/KhwarizmiAnalytix/Graph/actions/workflows/build.yml/badge.svg)](https://github.com/KhwarizmiAnalytix/Graph/actions/workflows/build.yml)
[![codecov](https://codecov.io/gh/KhwarizmiAnalytix/Graph/branch/main/graph/badge.svg)](https://codecov.io/gh/KhwarizmiAnalytix/Graph)
[![License: GPL v3 / Commercial](https://img.shields.io/badge/license-GPL--3.0--or--later%20%2F%20commercial-blue.svg)](LICENSE)

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

### Reproducing CI with the setup helper

CI and local builds use the documented dotted actions and named options from
`python Scripts/setup.py --help`:

```sh
python Scripts/setup.py config.build.test.release.ninja.clang --build-dir build
python Scripts/setup.py install.release --build-dir build --install-prefix install
python Scripts/setup.py config.build.test --build-dir build-debug --compiler clang-cl --build-type Debug --testing on --benchmark off
```

Only declared options are accepted. Tool commands, raw build-system arguments,
and a `--` argument separator are rejected. Paths, backend selections, GPU
architectures, test filters, and installation settings have named options in
the usage text. `--source-dir` selects a dependency or installed-package consumer
project. `verifycache --build-dir build --target TARGET` checks the launcher and
clean-rebuild cache hits. `verifytoolchain` checks the compiler selected by Ninja.
Run Windows Ninja builds in a Visual Studio developer environment.

Bazel CI uses the same action style and the options listed by
`python Scripts/setup_bazel.py --help`, for example:

```sh
python Scripts/setup_bazel.py build.test --target //Testing/... --compiler default --test-output errors
```
