"""
Build Operations Helper Module

Handles the build step (ninja / xcodebuild / msvc) for Scripts/setup.py.
"""

import os
import subprocess
from typing import Optional

try:
    import resource
except ImportError:  # resource is POSIX-only; unavailable on Windows.
    resource = None  # type: ignore[assignment]


def _raise_stack_limit() -> None:
    """Raise the process stack ulimit (soft) to the hard limit.

    Some clang releases blow the default 8MB stack during optimization passes
    (-O1 and above) on large translation units (e.g. vendored googletest),
    crashing with an internal "stack smashing detected" / instruction-selection
    segfault. Raising RLIMIT_STACK before spawning the build (inherited by
    ninja's clang child processes) avoids the crash.
    """
    if resource is None:
        return
    try:
        soft, hard = resource.getrlimit(resource.RLIMIT_STACK)
        if hard == resource.RLIM_INFINITY or hard > soft:
            resource.setrlimit(resource.RLIMIT_STACK, (hard, hard))
    except (ValueError, OSError):
        pass


def get_logical_processor_count() -> Optional[int]:
    """Get the number of logical processors available."""
    try:
        import psutil  # type: ignore[import-untyped]

        return psutil.cpu_count(logical=True)  # type: ignore[no-untyped-call,no-any-return]
    except ImportError:
        try:
            return os.cpu_count()
        except AttributeError:
            import multiprocessing

            return multiprocessing.cpu_count()


def build_project(builder: str, build_enum: str, system: str, shell_flag: bool) -> int:
    """
    Build the project using the specified builder.

    Args:
        builder: Build system (ninja, xcodebuild, cmake)
        build_enum: Build type (Release, Debug, RelWithDebInfo)
        system: Operating system (Linux, Darwin, Windows)
        shell_flag: Whether to use shell execution

    Returns:
        Exit code (0 for success, non-zero for failure)
    """
    try:
        if system == "Linux" or builder == "ninja":
            n = get_logical_processor_count()
            cmake_cmd_build = [builder, "-j", str(n)]
        elif builder == "xcodebuild":
            n = get_logical_processor_count()
            cmake_cmd_build = [
                "xcodebuild",
                "-configuration",
                build_enum,
                "-parallelizeTargets",
                "-jobs",
                str(n),
            ]
        elif system == "Windows":
            cmake_cmd_build = [builder, "--build", ".", "--config", build_enum]
        else:
            return 1

        _raise_stack_limit()
        subprocess.check_call(cmake_cmd_build, stderr=subprocess.STDOUT, shell=shell_flag)
        return 0

    except subprocess.CalledProcessError:
        return 1
    except Exception:
        return 1
