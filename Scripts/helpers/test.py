"""
Test Execution Helper Module

Handles ctest / valgrind invocation for Scripts/setup.py.
"""

import os
import subprocess
from typing import Optional


def run_ctest(
    builder: str,
    build_enum: str,
    system: str,
    verbosity: str,
    shell_flag: bool,
    sanitizer_type: Optional[str] = None,
    source_path: Optional[str] = None,
) -> int:
    """
    Run tests using ctest.

    Args:
        builder: Build system (ninja, xcodebuild, cmake)
        build_enum: Build type (Release, Debug, RelWithDebInfo)
        system: Operating system (Linux, Darwin, Windows)
        verbosity: Verbosity flag
        shell_flag: Whether to use shell execution
        sanitizer_type: Type of sanitizer being used (address, thread, etc.)
        source_path: Path to source directory

    Returns:
        Exit code (0 for success, non-zero for failure)
    """
    try:
        ctest_cmd = ["ctest"]

        if system == "Windows" and builder != "ninja":
            ctest_cmd.extend(["-C", build_enum])
        if builder == "xcodebuild":
            ctest_cmd.extend(["-C", build_enum])
        if verbosity:
            ctest_cmd.append(verbosity)

        env = os.environ.copy()

        if sanitizer_type and source_path:
            suppressions_file = os.path.join(
                source_path,
                "Scripts",
                "suppressions",
                f"{sanitizer_type}san_suppressions.txt",
            )
            if os.path.exists(suppressions_file):
                sanitizer_option = f"{sanitizer_type.upper()}SAN_OPTIONS"
                env[sanitizer_option] = (
                    f"suppressions={suppressions_file}:print_suppressions=1"
                )

        return subprocess.check_call(
            ctest_cmd, stderr=subprocess.STDOUT, shell=shell_flag, env=env
        )

    except subprocess.CalledProcessError:
        return 1
    except Exception:
        return 1


def run_valgrind_test(source_path: str, build_path: str, shell_flag: bool) -> int:
    """
    Run tests with Valgrind memory checking.

    Args:
        source_path: Path to source directory
        build_path: Path to build directory
        shell_flag: Whether to use shell execution

    Returns:
        Exit code (0 for success, non-zero for failure)
    """
    script_full_path = os.path.join(source_path, "Scripts", "valgrind_ctest.sh")

    if not os.path.exists(script_full_path):
        return 1

    try:
        os.chmod(script_full_path, 0o755)
    except Exception:
        pass

    try:
        return subprocess.call(
            [script_full_path, build_path],
            stdin=subprocess.PIPE,
            shell=shell_flag,
        )
    except Exception:
        return 1
