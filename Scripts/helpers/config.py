"""
Configuration Generation Helper Module

Handles CMake configure-step invocation for Scripts/setup.py.
"""

import os
import subprocess
from typing import Optional


def configure_build(
    source_path: str,
    build_path: str,
    cmake_generator: str,
    cmake_cxx_compiler: str,
    cmake_c_compiler: str,
    cmake_flags: list[str],
    arg_cmake_verbose: str,
    shell_flag: bool,
    generator_toolset: Optional[str] = None,
) -> int:
    """
    Configure the build system using CMake.

    Args:
        source_path: Path to source directory
        build_path: Path to build directory
        cmake_generator: CMake generator to use
        cmake_cxx_compiler: C++ compiler specification
        cmake_c_compiler: C compiler specification
        cmake_flags: Additional CMake flags
        arg_cmake_verbose: Verbosity level
        shell_flag: Whether to use shell execution
        generator_toolset: Optional VS toolset passed via -T (e.g. "v143")

    Returns:
        Exit code (0 for success, non-zero for failure)
    """
    try:
        build_folder = f"-B {build_path}"
        source_folder = f"-S {source_path}"

        cmake_cmd = [
            "cmake",
            source_folder,
            build_folder,
            "-G",
            cmake_generator,
            arg_cmake_verbose,
        ]

        if generator_toolset:
            cmake_cmd.extend(["-T", generator_toolset])

        if cmake_cxx_compiler:
            cmake_cmd.append(cmake_cxx_compiler)
        if cmake_c_compiler:
            cmake_cmd.append(cmake_c_compiler)

        cmake_cmd.extend(cmake_flags)

        subprocess.check_call(cmake_cmd, stderr=subprocess.STDOUT, shell=shell_flag)
        return 0

    except subprocess.CalledProcessError:
        return 1
    except Exception:
        return 1


def handle_xcode_project_opening() -> None:
    """Open the generated Xcode project (non-interactive; always opens)."""
    try:
        xcodeproj_files = [f for f in os.listdir(".") if f.endswith(".xcodeproj")]
        if xcodeproj_files:
            project_file = xcodeproj_files[0]
            subprocess.run(["open", project_file], check=True)
    except Exception:
        pass
