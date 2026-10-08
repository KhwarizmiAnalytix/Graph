load("//bazel:xsigma.bzl", "xsigma_copts", "xsigma_defines", "xsigma_linkopts")

# C++ standard for Graph — mirrors CMake GRAPH_CXX_STANDARD (default: 20)
GRAPH_CXX_STD = "c++20"

def graph_copts():
    return xsigma_copts(cxx_std = GRAPH_CXX_STD)

def graph_defines():
    """Returns compile definitions for Graph.

    Graph has no backend-specific GRAPH_HAS_* toggles of its own (unlike
    Parallel) — only the shared/static export macro, added by BUILD.bazel's
    select() on top of this. Project-wide flags come from xsigma_defines().
    """
    return xsigma_defines()

def graph_linkopts():
    return xsigma_linkopts()
