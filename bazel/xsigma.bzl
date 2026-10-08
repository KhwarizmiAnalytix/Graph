# =============================================================================
# Graph — shared Bazel compile/link helpers (standalone)
# =============================================================================
# Copy of XSigma's bazel/xsigma.bzl, trimmed to what this repo uses (the Enzyme
# helpers are XSigma-only and intentionally omitted).
# =============================================================================

def xsigma_copts(cxx_std = "c++20", cstdlib_include = True):
    """Returns common compiler options for Graph targets.

    Args:
        cxx_std: C++ standard to use (default: c++20, matches CMake default).
        cstdlib_include: Whether to force-include <cstdlib> on non-Windows (mirrors
                 CMake's `target_compile_options(Graph PRIVATE -include cstdlib)`).
    """
    return select({
        "@platforms//os:windows": [
            "/std:" + cxx_std,
            "/Zc:__cplusplus",  # expose correct __cplusplus value (mirrors CMake /Zc:__cplusplus)
            "/EHsc",            # structured exception handling
            "/bigobj",          # large object files (mirrors CMake /bigobj)
            "/utf-8",           # UTF-8 source/output encoding (mirrors CMake /utf-8)
            "/wd4244",          # narrowing conversion (mirrors CMake /wd4244)
            "/wd4267",          # size_t → int conversion (mirrors CMake /wd4267)
            "/wd4715",          # not all control paths return (mirrors CMake /wd4715)
            "/wd4018",          # signed/unsigned comparison (mirrors CMake /wd4018)
            "/WX",              # warnings as errors (mirrors CMake /WX)
        ],
        "//conditions:default": [
            "-std=" + cxx_std,
            "-Wall",
            "-Wextra",
            "-Wpedantic",
        ] + (["-include", "cstdlib"] if cstdlib_include else []),
    })

def xsigma_defines():
    """Returns project-wide preprocessor defines (Windows CRT safety, mirrors
    CMake compiler_checks.cmake)."""
    return select({
        "@platforms//os:windows": [
            "_CRT_SECURE_NO_DEPRECATE",
            "_CRT_NONSTDC_NO_DEPRECATE",
            "_CRT_SECURE_NO_WARNINGS",
            "_SCL_SECURE_NO_DEPRECATE",
            "_SCL_SECURE_NO_WARNINGS",
        ],
        "//conditions:default": [],
    })

def xsigma_linkopts():
    """Returns common linker options for Graph targets."""
    return select({
        "@platforms//os:windows": [],
        "@platforms//os:macos": [
            "-undefined",
            "dynamic_lookup",
        ],
        "//conditions:default": [
            "-lpthread",
            "-ldl",
        ],
    })

def xsigma_test_copts():
    """Returns compiler options for test targets."""
    return xsigma_copts()

def xsigma_test_linkopts():
    """Returns linker options for test targets."""
    return xsigma_linkopts()
