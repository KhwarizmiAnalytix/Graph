# XSigma CMake tools (`Cmake/tools`)

This directory holds **CMake modules** (`.cmake` files) that extend the XSigma build: developer tooling, diagnostics, faster linking, compiler caches, and Xcode integration. They are loaded by name via CMake’s `include()` after the project prepends this folder to `CMAKE_MODULE_PATH`.

**Location in the build:** the top-level `CMakeLists.txt` does:

```cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Cmake/tools")
```

Modules are then included as `include(<name>)` (e.g. `include(cache)`), not `include(Cmake/tools/cache.cmake)`.

---

## Module index

| Module | Role |
|--------|------|
| [`xcode_toolchain.cmake`](#xcode_toolchaincmake) | Align Xcode generator with Homebrew LLVM `xctoolchain` (before `project()`) |
| [`summary.cmake`](#summarycmake) | `print_configuration_summary()` — human-readable configure report |
| [`cache.cmake`](#cachecmake) | Per-target compiler launchers (`ccache`, `sccache`, `buildcache`) |
| [`helper_macros.cmake`](#helper_macroscmake) | IDE source groups, Icecream launcher, naming helper |
| [`lto.cmake`](#ltocmake) | Per-target LTO (`xsigma_target_apply_lto`) + `xsigma_find_linker` (fast / LTO-safe linkers) |
| [`coverage.cmake`](#coveragecmake) | Compiler flags for GCC/Clang/MSVC coverage workflows |
| [`sanitize.cmake`](#sanitizecmake) | Per-module ASan/UBSan/TSan/MSan/LSan via `*build` interface targets |
| [`clang_tidy.cmake`](#clang_tidycmake) | `clang-tidy` on targets; optional fix mode |
| [`iwyu.cmake`](#iwyucmake) | Include-what-you-use with logging wrappers and mapping files |
| [`valgrind.cmake`](#valgrindcmake) | CTest memcheck defaults, suppressions, timeouts |
| [`spell.cmake`](#spellcmake) | `codespell` custom targets (can rewrite sources) |

---

## `xcode_toolchain.cmake`

**When:** Must be **`include()`d before `project()`** so compiler detection uses the toolchain’s `clang`/`clang++`.

**Behavior:**

- No-op unless `CMAKE_GENERATOR` is `Xcode`.
- Looks for Homebrew LLVM at `/opt/homebrew/opt/llvm` via `llvm-config`.
- Expects a registered Xcode toolchain under `~/Library/Developer/Toolchains`, e.g. `LLVM<version>.xctoolchain` (symlink from `.../opt/llvm/Toolchains/...`).
- Sets `CMAKE_XCODE_ATTRIBUTE_TOOLCHAINS` to `org.llvm.<version>`.
- Forces `CMAKE_C_COMPILER` / `CMAKE_CXX_COMPILER` to the toolchain’s `usr/bin/clang` and `clang++`.
- Sets `PROJECT_LLVM_INSTALL_PREFIX` to the Homebrew LLVM prefix for consistent link paths.

If LLVM is missing or the toolchain is not registered, the module falls back to Apple’s default Clang and logs status or warning messages.

---

## `summary.cmake`

**API:** `print_configuration_summary()`

**Purpose:** Prints a **global** configure summary: CMake and compiler identity, default C++ standard, toolchain flags, build type, top-level compile definitions, paths, `BUILD_SHARED_LIBS`, and `XSIGMA_ENABLE_EXTERNAL`. Per-library CMake flags are printed at the end of each `Library/*/CMakeLists.txt`.

**Note:** Call `print_configuration_summary()` once after all `add_subdirectory(Library/...)` calls (see root `CMakeLists.txt`).

---

## `cache.cmake`

**API:** `xsigma_target_apply_cache(<target> <enable_var> <backend_var>)`

**Purpose:** Applies a **per-target** `CXX_COMPILER_LAUNCHER` / `C_COMPILER_LAUNCHER` (and `CUDA_COMPILER_LAUNCHER` when CUDA is enabled) instead of globally overriding `CMAKE_*_COMPILER_LAUNCHER`.

**Backends:** `none`, `ccache`, `sccache`, `buildcache` (validated; invalid values are fatal).

**Discovery:** For a backend `foo`, looks up cache variable `XSIGMA_FOO_PROGRAM`; if unset/empty, uses `find_program` for `foo`.

**Typical use:** Each library module defines `XXX_ENABLE_CACHE` and `XXX_CACHE_BACKEND`, then calls this after `add_library()` for that module’s targets.

---

## `helper_macros.cmake`

### `xsigma_module_create_filters(name)`

Recursively globs common source extensions under `CMAKE_CURRENT_SOURCE_DIR` and assigns **Visual Studio / Xcode `source_group`** paths derived from `name`, so IDE trees mirror directory layout. Computes a list of include-style paths (currently not applied via `include_directories` in the macro).

### `xsigma_target_optional_icecc(target_name enable_var)`

If `enable_var` is true, sets C/C++ compiler launcher to **Icecream** (`icecc`) when found. Errors if the target does not exist.

### `xsigma_module_remove_underscores(name_in name_out)`

String transform: split on `_`, capitalize each segment’s first letter, concatenate, then force the **first character of the whole string to lowercase** (camelCase-style output in `name_out`).

---

## `lto.cmake`

**API:** `xsigma_lto_compute_default`, `xsigma_target_apply_lto`, `xsigma_find_linker` — see the module header in `lto.cmake`.

**Linker helper:** `xsigma_find_linker(<linker_choice> <target> [<PREFIX>_LTO_MODE])` with `linker_choice` = `default` or `mold` / `lld` / `gold` / `lld-link`. Chooses an **LTO-compatible** linker when LTO is on (e.g. `ld.lld` for Clang ThinLTO on Linux), and fast linkers when LTO is off. Windows **cl** / **clang-cl** use the MSVC link stack; **GNU Clang** uses `-fuse-ld=lld` like Unix.

---

## `coverage.cmake`

**Purpose:** Appends **toolchain-appropriate coverage flags** to global `CMAKE_*_FLAGS` at include time.

| Compiler | Effect (high level) |
|----------|---------------------|
| **GNU** | `--coverage`, `-g`, `-O0`, `-fprofile-arcs`, `-ftest-coverage` |
| **Clang** (non–clang-cl) | `-g -O0 -fprofile-instr-generate -fcoverage-mapping` |
| **Clang** (clang-cl / MSVC frontend) | `/Zi /Od` + profile/coverage mapping flags |
| **MSVC** | Release and global linker flags for **PDBs** (`/Zi`, `/DEBUG`, …) and optional `CMAKE_PDB_OUTPUT_DIRECTORY` so **OpenCppCoverage** can find symbols |

Unsupported compilers get a warning only.

---

## `sanitize.cmake`

**API:** `xsigma_configure_sanitizer(<MODULE_NAME>)`

**Expects:** Cache variables `<MODULE_NAME>_ENABLE_SANITIZER` and `<MODULE_NAME>_SANITIZER_TYPE` (e.g. `address`, `undefined`, `thread`, `memory`, `leak`), and an interface target named `<module_name>build` (lowercase module prefix + `build`), e.g. `loggingbuild` for `LOGGING`.

**Behavior:**

- **GCC or Clang only**; others get a warning and no flags.
- Adds compile options: `-O1 -g -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize=<type>`.
- **Clang:** generates `${PROJECT_BINARY_DIR}/sanitizer_ignore.txt` once from `Scripts/suppressions/sanitizer_ignore.txt.in` and passes `-fsanitize-blacklist=...` (via `SHELL:` generator expression).
- **Linux (non-Apple):** for `address` or `undefined`, may `find_library` for `libasan.so.6` / `.5` (advanced cache per module).
- Link options: `$<BUILD_INTERFACE:-fsanitize=<type>>` on the module’s `*build` interface.

Adapted from smtk (Kitware).

---

## `clang_tidy.cmake`

**Requirements:** `find_program` for `clang-tidy` — missing executable is **fatal** when this module is included.

**API:**

- `xsigma_target_clang_tidy(target_name enable_fix)` — sets `C_CLANG_TIDY` / `CXX_CLANG_TIDY`. If `enable_fix` is true, adds `-fix-errors -fix` and treats warnings as errors (`-warnings-as-errors=*`). **Fix mode rewrites sources.**
- `disable_clang_tidy_for_target(target_name)` — clears those properties.

---

## `iwyu.cmake`

**Requirements:** `include-what-you-use` or `iwyu` on PATH when IWYU is enabled — otherwise **fatal** with install hints.

**Configure-time side effects:**

- Sets `PROJECT_IWYU_EXECUTABLE`, `PROJECT_IWYU_ARGS`, log path `IWYU_LOG_FILE` (`${CMAKE_BINARY_DIR}/iwyu.log`).
- Optional mapping: `Scripts/iwyu/iwyu_exclusion.imp`; prepends IWYU’s **`libcxx.imp`** from the install’s `share/include-what-you-use` when present.
- On **macOS**, may append `-isysroot` from `xcrun --show-sdk-path`.
- If `Scripts/iwyu/iwyu_configure_detector.py` exists, runs it against `Library/` to produce configure-header reports under the binary dir.

**API:**

- `xsigma_create_iwyu_wrapper(target_name lang [extra_args...])` — writes `${CMAKE_BINARY_DIR}/iwyu_wrapper_<target>_<lang>.cmake` that runs IWYU and appends stdout/stderr to `iwyu.log`.
- `xsigma_apply_iwyu(target_name)` — skips ThirdParty-like source trees; sets `CXX_INCLUDE_WHAT_YOU_USE` / `C_INCLUDE_WHAT_YOU_USE` to `cmake -P` the wrapper scripts (adds MSVC/STL compatibility defines for CXX/C).

---

## `valgrind.cmake`

**Purpose:** Central **CTest memcheck** setup when included (typically under a module flag like `XXX_ENABLE_VALGRIND`).

**Platform:** Warns on **Apple Silicon (arm64)** that Valgrind may be unsuitable; suggests sanitizers instead.

**Finds:** `valgrind` → `CMAKE_MEMORYCHECK_COMMAND`; logs version.

**Cache variables:**

- `PROJECT_VALGRIND_TIMEOUT_MULTIPLIER` (default `20`) — scales per-test timeouts.
- `CTEST_TEST_TIMEOUT` (default `1800`) — fallback global timeout for tests without an explicit timeout.

**`CMAKE_MEMORYCHECK_COMMAND_OPTIONS`:** memcheck with full leak info, origins, FD tracking, verbose output, XML + log under `${CMAKE_BINARY_DIR}/Testing/Temporary/`, error exit code 1, suppression generation, and optional **`Scripts/suppressions/valgrind_suppression.txt`** if it exists.

**API:** `xsigma_apply_valgrind_timeouts()` — multiplies each registered test’s `TIMEOUT` in the current directory by `PROJECT_VALGRIND_TIMEOUT_MULTIPLIER`, or sets `CTEST_TEST_TIMEOUT` if unset. Call after tests are defined.

**Running:** e.g. `ctest -T memcheck` (standard CMake workflow).

---

## `spell.cmake`

**Purpose:** Integrates **codespell** for the **including** module’s tree.

**Guards:** Immediately returns without doing anything if the current source dir is (or is under) `ThirdParty`, `third_party`, or `3rdparty`.

**Behavior when active:**

- `find_program(CODESPELL_EXECUTABLE codespell ...)` — not found is **fatal** with install instructions.
- Always passes **`--skip`** globs that exclude `ThirdParty` / `third_party` / `3rdparty` (directory names and full paths).
- Always passes **`--ignore-words=Scripts/suppressions/spell_suppressions.txt`**. Add excluded words there (one per line, sorted), not in `spell.cmake`.
- Check-only: does **not** pass `--write-changes`.
- Adds `spell_check_<dir>` and **`spell_check_build_<dir>`** (`ALL`) custom targets so spell check can run on every build.

Emits a **WARNING** that enabling spell check can modify files; commit before building.

---

## Typical include flow in library modules

Library `CMakeLists.txt` files generally:

1. `include(cache)` early, then call `xsigma_target_apply_cache` per target.
2. `include(lto)` and `xsigma_find_linker(...)` (same module as LTO) where linker choice is configured.
3. Conditionally `include(coverage)`, `include(sanitize)` + `xsigma_configure_sanitizer`, `include(clang_tidy)`, `include(iwyu)`, `include(spell)`, `include(valgrind)` behind options.
4. After tests exist, optionally `xsigma_apply_valgrind_timeouts()`.
5. Optionally emit a **module CMake flag summary** (inline `message(...)` blocks at the end of the file).

The root project includes `xcode_toolchain` before `project()`, and `include(summary)` plus `print_configuration_summary()` after all library subdirectories for the global report.

---

## Related paths outside this folder

| Path | Used by |
|------|---------|
| `Scripts/suppressions/valgrind_suppression.txt` | `valgrind.cmake` |
| `Scripts/suppressions/sanitizer_ignore.txt.in` | `sanitize.cmake` |
| `Scripts/iwyu/iwyu_exclusion.imp`, `iwyu_configure_detector.py` | `iwyu.cmake` |

---

## Naming note

The directory is **`Cmake/tools`** (capital **C**, lowercase **make**), matching `list(APPEND CMAKE_MODULE_PATH ...)` in the root `CMakeLists.txt`. Tools or documentation that refer to `CMake/tools` mean this same path.
