# =============================================================================
# Graph — compatibility shim for ThirdParty/Parallel's Bazel package
# =============================================================================
# ThirdParty/Parallel is vendored as a plain subdirectory of this workspace
# (matching the #include <parallel.h> path baked into
# include/graph_executor.h, resolved via the -IThirdParty/Parallel/Parallel
# copt in .bazelrc), not a separate Bazel repository. Its own BUILD.bazel
# loads its compile-option helpers via the absolute label
# "//bazel:parallel.bzl" — evaluated inside this (the only) workspace, that
# label resolves to this file rather than Parallel's own bazel/parallel.bzl.
#
# Re-export the real implementation instead of duplicating it, so upstream
# changes to Parallel's own helpers keep applying automatically; per
# .augment/rules/ThirdParty.md, vendored sources are never edited in place.
# =============================================================================

load(
    "//ThirdParty/Parallel/bazel:parallel.bzl",
    _parallel_copts = "parallel_copts",
    _parallel_defines = "parallel_defines",
    _parallel_linkopts = "parallel_linkopts",
)

parallel_copts = _parallel_copts
parallel_defines = _parallel_defines
parallel_linkopts = _parallel_linkopts
