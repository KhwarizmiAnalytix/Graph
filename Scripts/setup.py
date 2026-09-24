#!/usr/bin/env python3
"""Graph CMake Build Configuration Script.

Design follows KhwarizmiAnalytix/XSigma's Scripts/setup.py (dotted-token CLI,
XSigmaFlags/XSigmaConfiguration split, coverage-tool integration) scaled down
to the CMake options this standalone repo actually defines.

Usage:
    python Scripts/setup.py config.build.test
    python Scripts/setup.py config.build.test.coverage
    python Scripts/setup.py config.build.test.gcc.release
"""

import glob
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

try:
    import colorama
    from colorama import Fore, Style

    colorama.init()
except ImportError:  # Windows CLI smoke and some CI jobs skip pip install

    class Fore:  # pylint: disable=too-few-public-methods
        CYAN = GREEN = YELLOW = RED = WHITE = ""

    class Style:  # pylint: disable=too-few-public-methods
        RESET_ALL = ""

from helpers import build as build_helper, config as config_helper, cppcheck as cppcheck_helper, test as test_helper

DEBUG_FLAG = False


class ErrorLogger:
    """Centralized error logging system for comprehensive error tracking."""

    def __init__(self, log_dir: str = "logs"):
        self.log_dir = Path(log_dir)
        self.log_dir.mkdir(exist_ok=True)
        self.log_file = (
            self.log_dir / f"logging_build_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"
        )
        self.errors = []

    def log_error(
        self,
        command: str,
        error_output: str,
        context: str = "",
        suggestions: Optional[list[str]] = None,
    ):
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self.errors.append(
            {
                "timestamp": timestamp,
                "command": command,
                "error_output": error_output,
                "context": context,
                "suggestions": suggestions or [],
            }
        )
        with open(self.log_file, "a", encoding="utf-8") as f:
            f.write(f"\n{'=' * 80}\n")
            f.write(f"ERROR LOG ENTRY - {timestamp}\n")
            f.write(f"{'=' * 80}\n")
            f.write(f"Context: {context}\n")
            f.write(f"Command: {command}\n")
            f.write(f"Error Output:\n{error_output}\n")
            if suggestions:
                f.write("Troubleshooting Suggestions:\n")
                for i, suggestion in enumerate(suggestions, 1):
                    f.write(f"  {i}. {suggestion}\n")
            f.write(f"{'=' * 80}\n\n")

    def get_log_file_path(self) -> str:
        return str(self.log_file)

    def has_errors(self) -> bool:
        return len(self.errors) > 0


class SummaryReporter:
    """Generate and display summary reports for various analysis tools."""

    def __init__(self):
        self.reports = {}

    def add_cppcheck_report(self, log_file: str, exit_code: int):
        if not os.path.exists(log_file):
            self.reports["cppcheck"] = {"status": "not_run", "message": "Cppcheck was not executed"}
            return
        try:
            with open(log_file, encoding="utf-8") as f:
                content = f.read()
            issues = {
                "error": len(re.findall(r",error,", content)),
                "warning": len(re.findall(r",warning,", content)),
                "style": len(re.findall(r",style,", content)),
                "performance": len(re.findall(r",performance,", content)),
                "portability": len(re.findall(r",portability,", content)),
                "information": len(re.findall(r",information,", content)),
            }
            self.reports["cppcheck"] = {
                "status": "completed",
                "exit_code": exit_code,
                "total_issues": sum(issues.values()),
                "issues_by_type": issues,
                "log_file": log_file,
            }
        except Exception as e:
            self.reports["cppcheck"] = {"status": "error", "message": f"Failed to parse cppcheck results: {e}"}

    def add_valgrind_report(self, build_path: str, exit_code: int):
        valgrind_logs = glob.glob(os.path.join(build_path, "Testing", "Temporary", "MemoryChecker.*.log"))
        if not valgrind_logs:
            self.reports["valgrind"] = {"status": "not_run", "message": "Valgrind was not executed or no logs found"}
            return
        try:
            memory_leaks = 0
            memory_errors = 0
            for log_file in valgrind_logs:
                with open(log_file, encoding="utf-8") as f:
                    content = f.read()
                memory_leaks += sum(int(m) for m in re.findall(r"definitely lost: (\d+)", content) if int(m) > 0)
                memory_errors += sum(int(m) for m in re.findall(r"ERROR SUMMARY: (\d+) errors", content) if int(m) > 0)
            self.reports["valgrind"] = {
                "status": "completed",
                "exit_code": exit_code,
                "memory_leaks": memory_leaks,
                "memory_errors": memory_errors,
                "log_files": valgrind_logs,
            }
        except Exception as e:
            self.reports["valgrind"] = {"status": "error", "message": f"Failed to parse valgrind results: {e}"}

    def add_coverage_report(self, build_path: str, exit_code: int):
        """Parse the coverage-tool JSON report and extract summary metrics."""
        import json

        coverage_json_paths = [
            os.path.join(build_path, "coverage_report", "coverage_summary.json"),
            os.path.join(build_path, "coverage_report", "coverage.json"),
        ]
        coverage_json = next((p for p in coverage_json_paths if os.path.exists(p)), None)
        if not coverage_json:
            self.reports["coverage"] = {"status": "not_run", "message": "Coverage report not found"}
            return
        try:
            with open(coverage_json, encoding="utf-8") as f:
                coverage_data = json.load(f)
            if "global_metrics" in coverage_data:
                metrics = coverage_data["global_metrics"]
                self.reports["coverage"] = {
                    "status": "completed",
                    "exit_code": exit_code,
                    "total_lines": metrics.get("total_lines", 0),
                    "covered_lines": metrics.get("covered_lines", 0),
                    "line_coverage_percent": metrics.get("line_coverage_percent", 0.0),
                    "total_functions": metrics.get("total_functions", 0),
                    "covered_functions": metrics.get("covered_functions", 0),
                    "function_coverage_percent": metrics.get("function_coverage_percent", 0.0),
                    "total_regions": metrics.get("total_regions", 0),
                    "covered_regions": metrics.get("covered_regions", 0),
                    "region_coverage_percent": metrics.get("region_coverage_percent", 0.0),
                    "report_file": coverage_json,
                }
            elif "summary" in coverage_data:
                summary = coverage_data["summary"]
                line_cov = summary.get("line_coverage", {})
                func_cov = summary.get("function_coverage", {})
                self.reports["coverage"] = {
                    "status": "completed",
                    "exit_code": exit_code,
                    "total_lines": line_cov.get("total", 0),
                    "covered_lines": line_cov.get("covered", 0),
                    "line_coverage_percent": line_cov.get("percent", 0.0),
                    "total_functions": func_cov.get("total", 0),
                    "covered_functions": func_cov.get("covered", 0),
                    "function_coverage_percent": func_cov.get("percent", 0.0),
                    "total_regions": 0,
                    "covered_regions": 0,
                    "region_coverage_percent": 0.0,
                    "report_file": coverage_json,
                }
            else:
                self.reports["coverage"] = {"status": "error", "message": "Coverage JSON format not recognized"}
        except Exception as e:
            self.reports["coverage"] = {"status": "error", "message": f"Failed to parse coverage results: {e}"}

    def display_summary(self):
        if not self.reports:
            return
        print_status("\n" + "=" * 80, "INFO")
        print_status("BUILD AND ANALYSIS SUMMARY REPORT", "INFO")
        print_status("=" * 80, "INFO")
        for tool, report in self.reports.items():
            self._display_tool_summary(tool, report)
        print_status("=" * 80, "INFO")

    def _display_tool_summary(self, tool: str, report: dict):
        tool_name = tool.upper()
        if report["status"] == "not_run":
            print_status(f"{tool_name}: Not executed", "INFO")
            return
        if report["status"] == "error":
            print_status(f"{tool_name}: Error - {report['message']}", "ERROR")
            return

        if tool == "cppcheck":
            total = report["total_issues"]
            if total == 0:
                print_status(f"{tool_name}: No issues found", "SUCCESS")
            else:
                print_status(f"{tool_name}: Found {total} issues", "WARNING")
                for issue_type, count in report["issues_by_type"].items():
                    if count > 0:
                        print_status(f"  - {issue_type}: {count}", "INFO")
                print_status(f"  Log file: {report['log_file']}", "INFO")

        elif tool == "valgrind":
            leaks = report["memory_leaks"]
            errors = report["memory_errors"]
            if leaks == 0 and errors == 0:
                print_status(f"{tool_name}: No memory issues found", "SUCCESS")
            else:
                if leaks > 0:
                    print_status(f"{tool_name}: Found {leaks} memory leaks", "ERROR")
                if errors > 0:
                    print_status(f"{tool_name}: Found {errors} memory errors", "ERROR")

        elif tool == "coverage":
            print_status("\n" + "=" * 80, "INFO")
            print_status("CODE COVERAGE SUMMARY", "INFO")
            print_status("=" * 80, "INFO")
            total_lines = report.get("total_lines", 0)
            covered_lines = report.get("covered_lines", 0)
            coverage_percent = report.get("line_coverage_percent", 0.0)
            print_status(f"Total Lines:    {total_lines}", "INFO")
            print_status(f"Covered Lines:  {covered_lines}", "INFO")
            print_status(
                f"Coverage:       {coverage_percent:.2f}%",
                "SUCCESS" if coverage_percent >= 95.0 else "WARNING" if coverage_percent >= 80.0 else "ERROR",
            )
            if report.get("total_functions", 0) > 0:
                print_status(f"Function Coverage: {report.get('function_coverage_percent', 0.0):.2f}%", "INFO")
            if report.get("total_regions", 0) > 0:
                print_status(f"Region Coverage:   {report.get('region_coverage_percent', 0.0):.2f}%", "INFO")
            report_file = report.get("report_file", "")
            if report_file:
                report_dir = os.path.dirname(report_file)
                for html_path in (
                    os.path.join(report_dir, "html", "index.html"),
                    os.path.join(report_dir, "index.html"),
                ):
                    if os.path.exists(html_path):
                        print_status(f"\nHTML Report: {html_path}", "INFO")
                        break
            print_status("=" * 80, "INFO")


def check_dependencies() -> list[str]:
    """Check if required dependencies are installed."""
    missing_deps = []
    try:
        subprocess.run(["cmake", "--version"], capture_output=True, check=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        missing_deps.append("CMake")

    if platform.system() == "Windows":
        try:
            subprocess.run(["clang", "--version"], capture_output=True, check=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            try:
                subprocess.run(["cl"], capture_output=True)
            except (subprocess.CalledProcessError, FileNotFoundError):
                missing_deps.append("C++ compiler (MSVC or Clang)")
    elif platform.system() == "Darwin":
        try:
            subprocess.run(["xcode-select", "--print-path"], capture_output=True, check=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            missing_deps.append("Xcode Command Line Tools (run: xcode-select --install)")
        try:
            subprocess.run(["clang++", "--version"], capture_output=True, check=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            missing_deps.append("C++ compiler (Clang)")
    else:
        try:
            subprocess.run(["clang++", "--version"], capture_output=True, check=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            try:
                subprocess.run(["g++", "--version"], capture_output=True, check=True)
            except (subprocess.CalledProcessError, FileNotFoundError):
                missing_deps.append("C++ compiler (GCC or Clang)")

    return missing_deps


def check_xcode_availability() -> bool:
    if platform.system() != "Darwin":
        return False
    try:
        result = subprocess.run(["xcodebuild", "-version"], capture_output=True, check=True, text=True)
        print_status(f"Found Xcode: {result.stdout.strip().split()[1]}", "INFO")
        return True
    except subprocess.CalledProcessError as e:
        stderr_output = e.stderr if isinstance(e.stderr, str) else (e.stderr.decode() if e.stderr else "")
        if "command line tools instance" in stderr_output:
            print_status("Xcode Command Line Tools found, but full Xcode is required for Xcode generator", "WARNING")
        else:
            print_status(f"Xcode check failed: {stderr_output.strip()}", "WARNING")
        return False
    except FileNotFoundError:
        print_status("xcodebuild not found - install Xcode or Xcode Command Line Tools", "WARNING")
        return False


def print_status(message: str, status: str = "INFO", end: str = "\n") -> None:
    status_colors = {"INFO": Fore.BLUE, "SUCCESS": Fore.GREEN, "ERROR": Fore.RED, "WARNING": Fore.YELLOW}
    color = status_colors.get(status, Fore.WHITE)
    print(f"{color}[{status}]{Style.RESET_ALL} {message}", end=end)


def debug_print(message):
    if DEBUG_FLAG:
        print(message)


class LoggingFlags:
    """Maps setup.py dotted tokens to Logging's LOGGING_* CMake cache variables.

    Scoped 1:1 to the options CMakeLists.txt actually defines — unlike XSigma's
    XSigmaFlags there is no multi-module fan-out (Logging is a single flat
    CMake target), no GPU/MKL/vectorization backends, and no --project.*
    scoping. Logging does own a real backend selector (LOGGING_BACKEND: NATIVE,
    LOGURU, GLOG, SPDLOG) — unlike Parallel, this one IS wired through.
    """

    OFF = "OFF"
    ON = "ON"

    def __init__(self, arg_list):
        self.__initialize_flags()
        if arg_list:
            self.__build_cmake_flag()
            self.__fill_option_flags(arg_list)
            self.__validate_flags()

    def __initialize_flags(self):
        self.__key = [
            "static",
            "test",
            "examples",
            "gtest",
            "benchmark",
            "magicenum",
            "cxademangle",
            "portablefloat",
            "stdformat",
            "exceptionmode",
            "backend",
            "icecc",
            "cache",
            "cache_type",
            "clangtidy",
            "fix",
            "iwyu",
            "sanitizer",
            "sanitizer_enum",
            "spell",
            "valgrind",
            "linker",
            "coverage",
            "cxxstd",
            "lto",
            "cppcheck",
        ]
        self.__description = [
            "build shared (default) or static libraries",
            "build the Logging test suite (LOGGING_ENABLE_TESTING; default ON)",
            "build Logging example programs",
            "enable GoogleTest for Logging (default ON; token disables it)",
            "enable Google Benchmark for Logging (default ON; token disables it)",
            "enable magic_enum static reflection (default ON; token disables it)",
            "force Itanium ABI demangling on (default: auto-detected by CMake)",
            "use the portable floating-point formatter instead of std::to_chars",
            "use C++20 std::format instead of fmt (requires cxx20+)",
            "default exception mode: throw (default) or logfatal",
            "logging backend: native, loguru (default), glog, or spdlog",
            "use Icecream (icecc) distributed compiler",
            "enable per-target compiler cache launcher (default ON; token disables it)",
            "compiler cache backend: none, ccache, sccache, or buildcache",
            "enable clang-tidy static analysis",
            "enable clang-tidy fix-errors and fix options",
            "enable include-what-you-use (iwyu) analysis",
            "build with sanitizer support (Clang/GCC only)",
            "sanitizer type: address, undefined, thread, memory, leak",
            "enable check-only spell checking",
            "execute the test suite under Valgrind",
            "linker: --linker.default, --linker.mold, --linker.lld, --linker.gold, --linker.lld-link",
            "enable code coverage instrumentation",
            "C++ standard: cxx11, cxx14, cxx17, cxx20, cxx23",
            "LTO mode: off | thin | full | ipo | auto (bare 'lto' = auto)",
            "enable cppcheck static analysis",
        ]

    def __build_cmake_flag(self):
        debug_print("Build cmake flag")
        self.__name = {
            "static": "BUILD_SHARED_LIBS",
            "test": "LOGGING_ENABLE_TESTING",
            "examples": "LOGGING_ENABLE_EXAMPLES",
            "gtest": "LOGGING_ENABLE_GTEST",
            "benchmark": "LOGGING_ENABLE_BENCHMARK",
            "magicenum": "LOGGING_ENABLE_MAGICENUM",
            "cxademangle": "LOGGING_ENABLE_CXA_DEMANGLE",
            "portablefloat": "LOGGING_ENABLE_PORTABLE_FLOAT_FORMAT",
            "stdformat": "LOGGING_FORMAT_USE_STD",
            "exceptionmode": "LOGGING_DEFAULT_EXCEPTION_MODE",
            "backend": "LOGGING_BACKEND",
            "icecc": "LOGGING_ENABLE_ICECC",
            "cache": "LOGGING_ENABLE_CACHE",
            "cache_type": "LOGGING_CACHE_BACKEND",
            "clangtidy": "LOGGING_ENABLE_CLANGTIDY",
            "fix": "LOGGING_ENABLE_FIX",
            "iwyu": "LOGGING_ENABLE_IWYU",
            "sanitizer": "LOGGING_ENABLE_SANITIZER",
            "sanitizer_enum": "LOGGING_SANITIZER_TYPE",
            "spell": "LOGGING_ENABLE_SPELL",
            "valgrind": "LOGGING_ENABLE_VALGRIND",
            "linker": "LOGGING_LINKER_CHOICE",
            "coverage": "LOGGING_ENABLE_COVERAGE",
            "cxxstd": "LOGGING_CXX_STANDARD",
            "lto": "LOGGING_LTO_MODE",
            # "cppcheck" runs via Scripts/helpers/cppcheck.py after the build; no
            # CMakeLists.txt option consumes it (avoid an unused-var warning).
        }

    def __fill_option_flags(self, arg_list):
        debug_print("Fill option flags")
        self.__set_default_flags()
        self.__process_arg_list(arg_list)

    def __set_default_flags(self):
        # Mirrors CMakeLists.txt's option()/set() defaults exactly.
        self.__value = dict.fromkeys(self.__key, self.OFF)
        self.__value.update(
            {
                "static": self.ON,  # BUILD_SHARED_LIBS default is ON, so static=ON means "shared"
                "test": self.ON,  # LOGGING_ENABLE_TESTING default ON
                "gtest": self.ON,  # LOGGING_ENABLE_GTEST default ON
                "benchmark": self.ON,  # LOGGING_ENABLE_BENCHMARK default ON
                "magicenum": self.ON,  # LOGGING_ENABLE_MAGICENUM default ON
                "cache": self.ON,  # LOGGING_ENABLE_CACHE default ON
                "cxademangle": "",  # empty = let CMake auto-detect <cxxabi.h> support
                "exceptionmode": "",  # empty = let CMake's default (THROW) apply
                "backend": "",  # empty = let CMake's default (LOGURU) apply
                "cache_type": "none",
                "linker": "default",
                "cxxstd": "",  # empty = let CMake use its own default (20)
                "lto": "",  # empty = let CMake compute the smart per-compiler default
                "sanitizer_enum": "address",
            }
        )

    def __process_arg_list(self, arg_list):
        sanitizer_list = ["address", "undefined", "thread", "memory", "leak"]
        cxx_std_list = ["cxx11", "cxx14", "cxx17", "cxx20", "cxx23"]
        cache_type_list = ["none", "ccache", "sccache", "buildcache"]
        backend_list = ["native", "loguru", "glog", "spdlog"]
        linker_list = ["default", "mold", "lld", "gold", "lld-link"]

        self.builder_suffix = ""
        for arg in arg_list:
            if arg == "static":
                self.__value["static"] = self.OFF
                self.builder_suffix += "_static"
            elif arg in sanitizer_list:
                self.__value["sanitizer"] = self.ON
                self.__value["sanitizer_enum"] = arg
                self.builder_suffix += f"_{arg}"
            elif arg in backend_list:
                self.__value["backend"] = arg.upper()
                self.builder_suffix += f"_{arg}"
                print_status(f"Selecting logging backend: {arg.upper()}", "INFO")
            elif arg == "throw":
                self.__value["exceptionmode"] = "THROW"
            elif arg == "logfatal":
                self.__value["exceptionmode"] = "LOG_FATAL"
                self.builder_suffix += "_logfatal"
            elif arg.startswith("lto."):
                lto_modes = ["off", "thin", "full", "ipo", "auto"]
                lto_mode = arg.split(".", 1)[1].lower()
                if lto_mode in lto_modes:
                    self.__value["lto"] = lto_mode
                    if lto_mode != "off":
                        self.builder_suffix += f"_lto_{lto_mode}"
                    print_status(f"LTO mode: {lto_mode}", "INFO")
                else:
                    print_status(f"Unknown LTO mode '{lto_mode}'. Valid options: {', '.join(lto_modes)}", "ERROR")
                    sys.exit(1)
            elif arg.startswith("linker."):
                linker_value = arg.split(".", 1)[1].lower()
                if linker_value in linker_list and linker_value != "default":
                    self.__value["linker"] = linker_value
                    self.builder_suffix += f"_linker_{linker_value}"
                    print_status(f"Setting linker to {linker_value}", "INFO")
                else:
                    print_status(
                        f"Unknown linker '{linker_value}'. Valid options: {', '.join(l for l in linker_list if l != 'default')}",
                        "ERROR",
                    )
                    sys.exit(1)
            elif arg in cache_type_list:
                self.__value["cache_type"] = arg
                self.builder_suffix += f"_{arg}"
                print_status(f"Setting cache type to {arg}", "INFO")
            elif any(arg.lower() == item.lower() for item in cxx_std_list):
                std_version = arg[3:]  # Remove "cxx" prefix
                self.__value["cxxstd"] = std_version
                print_status(f"Setting C++ standard to C++{std_version}", "INFO")
            elif re.match(r"^c\+\+(\d+)$", arg.lower()):
                std_version = re.match(r"^c\+\+(\d+)$", arg.lower()).group(1)
                self.__value["cxxstd"] = std_version
                print_status(f"Setting C++ standard to C++{std_version}", "INFO")
            elif arg in self.__key:
                if arg in ("gtest", "benchmark", "cache", "magicenum"):
                    # CMake default ON: providing the token turns it OFF.
                    self.__value[arg] = self.OFF
                elif arg == "lto":
                    self.__value["lto"] = "auto"
                    self.builder_suffix += "_lto_auto"
                else:
                    # CMake default OFF (or auto-detected): providing the token turns it ON.
                    self.__value[arg] = self.ON

                if arg not in ("test", "build", "benchmark"):
                    self.builder_suffix += f"_{arg}"

    def __validate_flags(self):
        if self.__value.get("sanitizer") == self.ON and self.__value.get("valgrind") == self.ON:
            print_status("Both sanitizer and valgrind enabled - consider using only one.", "WARNING")

        if self.__value.get("coverage") == self.ON and self.__value.get("test") != self.ON:
            print_status("Coverage enabled but testing is disabled - enabling tests automatically.", "WARNING")
            self.__value["test"] = self.ON

        if self.__value.get("coverage") == self.ON:
            try:
                import coverage_tool  # noqa: F401
            except ImportError:
                print_status(
                    "coverage-tool is not installed. Install with: pip install coverage-tool",
                    "ERROR",
                )
                sys.exit(1)

        if self.__value.get("spell") == self.ON:
            print_status("SPELL CHECKING ENABLED: Automatic spelling corrections will be applied during build!", "WARNING")
            print_status("Ensure you have committed your changes before building with this option.", "WARNING")

        backend = self.__value.get("backend", "")
        if backend and backend not in ["NATIVE", "LOGURU", "GLOG", "SPDLOG"]:
            print_status(f"Invalid logging backend '{backend}'. Valid options: native, loguru, glog, spdlog", "ERROR")
            sys.exit(1)

        if self.__value.get("stdformat") == self.ON:
            cxxstd = self.__value.get("cxxstd") or "20"
            if int(cxxstd) < 20:
                print_status(
                    f"stdformat requires C++20 or newer (got cxx{cxxstd}); LOGGING_FORMAT_USE_STD will fail to configure.",
                    "ERROR",
                )
                sys.exit(1)

    @staticmethod
    def find_case_insensitive(element, lst):
        element_lower = element.lower()
        return next((item for item in lst if element_lower == item.lower()), None)

    def create_cmake_flags(self, cmake_cmd_flags, build_enum, system):
        debug_print("Create cmake flags")
        del system  # unused; kept for parity with the CMake-driven build-type selection below
        if (
            self.__value.get("valgrind") == self.ON
            or self.__value.get("sanitizer") == self.ON
            or self.__value.get("coverage") == self.ON
        ):
            print_status("Enabling debug build for sanitizer, valgrind, or coverage analysis", "INFO")
            build_type = "DEBUG"
        else:
            build_type = str(build_enum).capitalize()

        for key, value in self.__value.items():
            if key in self.__name:
                flag_name = self.__name[key]
                flag_value = "ON" if isinstance(value, bool) and value else str(value)
                if flag_value != "":
                    cmake_cmd_flags.append(f"-D{flag_name}={flag_value}")

        cmake_cmd_flags.append("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON")
        return build_type

    def helper(self):
        for key, description in zip(self.__key, self.__description):
            print(f"{key:<20}{description}")

    def is_coverage(self):
        return self.__value["coverage"] == self.ON

    def is_valgrind(self):
        return self.__value["valgrind"] == self.ON

    def is_cppcheck(self):
        return self.__value["cppcheck"] == self.ON

    def get_sanitizer_type(self):
        if self.__value.get("sanitizer") == self.ON:
            return self.__value.get("sanitizer_enum")
        return None


class LoggingConfiguration:
    def __init__(self, args_list):
        missing_deps = check_dependencies()
        if missing_deps:
            print_status("Missing required dependencies:", "ERROR")
            for dep in missing_deps:
                print_status(f"  - {dep}", "ERROR")
            print_status("Please install missing dependencies and try again.", "ERROR")
            sys.exit(1)

        self.error_logger = ErrorLogger()
        self.summary_reporter = SummaryReporter()

        self.__initialize_values()
        self.__logging_flags = LoggingFlags(args_list)
        self.__fill_compilation_flags(args_list)

    def __initialize_values(self):
        default_cxx_compiler = "clang++"
        default_c_compiler = "clang"
        self.__value = {
            "system": platform.system(),
            "build_folder": "build_ninja",
            "builder": "ninja",
            "config": "",
            "build": "",
            "test": "",
            "build_enum": "Release",
            "cmake_generator": "Ninja",
            "cmake_cxx_compiler": f"-DCMAKE_CXX_COMPILER={default_cxx_compiler}",
            "cmake_c_compiler": f"-DCMAKE_C_COMPILER={default_c_compiler}",
            "verbosity": "",
            "arg_cmake_verbose": "--loglevel=NOTICE",
        }
        self.__compiler_user_specified = False
        print(f"================= {self.__value['system']} platform =================")

    def __fill_compilation_flags(self, args_list):
        for arg in args_list:
            self.__process_arg(arg)

    def __process_arg(self, arg):
        if arg == "ninja":
            self.__set_ninja_flags()
        elif arg == "xcode":
            self.__set_xcode_flags()
        elif self.__is_clang_compiler(arg):
            self.__set_clang_compiler(arg)
        elif arg == "clang-cl":
            self.__value["cmake_cxx_compiler"] = "-DCMAKE_GENERATOR_TOOLSET=ClangCL"
            self.__value["cmake_c_compiler"] = ""
            self.__compiler_user_specified = True
        elif self.__is_gcc_compiler(arg):
            self.__set_gcc_compiler(arg)
        elif self.__is_visual_studio(arg):
            self.__set_visual_studio(arg)
        elif arg in ["config", "build", "test"]:
            self.__value[arg] = arg
        elif arg in ["release", "debug", "relwithdebinfo"]:
            self.__value["build_enum"] = arg.capitalize()
        elif arg in ["vv", "v"]:
            self.__set_verbose_flags()

    def __set_ninja_flags(self):
        self.__value["cmake_generator"] = "Ninja"
        self.__value["builder"] = "ninja"
        self.__value["build_folder"] = f"build_ninja{self.__logging_flags.builder_suffix}"

    def __set_xcode_flags(self):
        if self.__value["system"] == "Darwin" and check_xcode_availability():
            self.__value["cmake_generator"] = "Xcode"
            self.__value["builder"] = "xcodebuild"
            self.__value["build_folder"] = f"build_xcode{self.__logging_flags.builder_suffix}"
            print_status("Using Xcode generator", "SUCCESS")
        else:
            if self.__value["system"] == "Darwin":
                print_status("Xcode not found, falling back to Ninja", "WARNING")
            else:
                print_status("Xcode generator is only available on macOS", "WARNING")
            self.__set_ninja_flags()

    def __is_clang_compiler(self, arg):
        return "clang" in arg and arg not in ["clang-cl", "clangtidy"]

    def __set_clang_compiler(self, arg):
        self.__value["cmake_c_compiler"] = f"-DCMAKE_C_COMPILER={arg}"
        self.__value["cmake_cxx_compiler"] = f"-DCMAKE_CXX_COMPILER={arg.replace('clang', 'clang++')}"
        self.__compiler_user_specified = True

    def __is_gcc_compiler(self, arg):
        return ("gcc" in arg or "g++" in arg) and arg not in ["cppcheck"]

    def __set_gcc_compiler(self, arg):
        if "g++" in arg:
            self.__value["cmake_cxx_compiler"] = f"-DCMAKE_CXX_COMPILER={arg}"
            self.__value["cmake_c_compiler"] = f"-DCMAKE_C_COMPILER={arg.replace('g++', 'gcc')}"
        else:
            self.__value["cmake_c_compiler"] = f"-DCMAKE_C_COMPILER={arg}"
            self.__value["cmake_cxx_compiler"] = f"-DCMAKE_CXX_COMPILER={arg.replace('gcc', 'g++')}"
        self.__compiler_user_specified = True

    def __is_visual_studio(self, arg):
        return arg in ["vs17", "vs19", "vs22", "vs26"] and self.__value["system"] == "Windows"

    def __set_visual_studio(self, arg):
        vs_versions = {
            "vs17": ("Visual Studio 15 2017 Win64", "build_vs17"),
            "vs19": ("Visual Studio 16 2019", "build_vs19"),
            "vs22": ("Visual Studio 17 2022", "build_vs22"),
            "vs26": ("Visual Studio 18 2026", "build_vs26"),
        }
        self.__value["cmake_generator"], base_build_folder = vs_versions[arg]
        self.__value["builder"] = "cmake"
        self.__value["build_folder"] = f"{base_build_folder}{self.__logging_flags.builder_suffix}"
        if not self.__compiler_user_specified:
            self.__value["cmake_cxx_compiler"] = ""
            self.__value["cmake_c_compiler"] = ""

    def __set_verbose_flags(self):
        self.__value["arg_cmake_verbose"] = "--loglevel=VERBOSE"
        self.__value["verbosity"] = "-VV"

    def config(self, source_path, build_path):
        if self.__value["config"] != "config":
            return 0
        print_status("Configuring build...", "INFO")
        try:
            cmake_flags = []
            self.__value["build_enum"] = self.__logging_flags.create_cmake_flags(
                cmake_flags, self.__value["build_enum"], self.__value["system"]
            )
            print(f"build enum: {self.__value['build_enum']}")
            cmake_flags.append(f"-DCMAKE_BUILD_TYPE={self.__value['build_enum']}")

            exit_code = config_helper.configure_build(
                source_path,
                build_path,
                self.__value["cmake_generator"],
                self.__value["cmake_cxx_compiler"],
                self.__value["cmake_c_compiler"],
                cmake_flags,
                self.__value["arg_cmake_verbose"],
                self.__shell_flag(),
            )
            if exit_code == 0:
                print_status("Build configured successfully", "SUCCESS")
                if self.__value["cmake_generator"] == "Xcode":
                    config_helper.handle_xcode_project_opening()
            else:
                print_status("Configuration failed", "ERROR")
                sys.exit(1)
        except subprocess.CalledProcessError as e:
            self.error_logger.log_error("cmake", str(e), "Configuring the build system")
            print_status(f"Configuration failed: {e}", "ERROR")
            sys.exit(1)

    def build(self):
        if self.__value["build"] != "build":
            return 0
        print_status("Building project...", "INFO")
        try:
            exit_code = build_helper.build_project(
                self.__value["builder"], self.__value["build_enum"], self.__value["system"], self.__shell_flag()
            )
            if exit_code == 0:
                print_status("Build completed successfully", "SUCCESS")
            else:
                print_status("Build failed", "ERROR")
                sys.exit(1)
        except subprocess.CalledProcessError as e:
            self.error_logger.log_error("build", str(e), "Building the project")
            print_status(f"Build failed: {e}", "ERROR")
            sys.exit(1)

    def cppcheck(self, source_path, build_path):
        if self.__value["build"] != "build" or not self.__logging_flags.is_cppcheck():
            return 0
        print_status("Starting static code analysis with cppcheck...", "INFO")
        try:
            version_result = subprocess.run(["cppcheck", "--version"], capture_output=True, check=True, text=True)
            print_status(f"Found cppcheck: {version_result.stdout.strip()}", "SUCCESS")
        except (subprocess.CalledProcessError, FileNotFoundError):
            print_status("cppcheck not found. Install it (e.g. 'brew install cppcheck').", "ERROR")
            return 1

        os.makedirs(build_path, exist_ok=True)
        output_file = os.path.join(build_path, "cppcheck_output.log")
        cppcheck_cmd = cppcheck_helper.build_cppcheck_command(source_path, output_file)

        try:
            original_dir = os.getcwd()
            os.chdir(source_path)
            result = subprocess.run(cppcheck_cmd, capture_output=True, text=True, check=False)
            os.chdir(original_dir)
            exit_code = cppcheck_helper.process_cppcheck_results(result, output_file)
            self.summary_reporter.add_cppcheck_report(output_file, exit_code)
            return exit_code
        except Exception as e:
            self.error_logger.log_error(" ".join(cppcheck_cmd), str(e), "Running cppcheck static analysis")
            print_status(f"Unexpected error during cppcheck execution: {e}", "ERROR")
            return 1

    def test(self, source_path, build_path):
        if self.__value["test"] != "test":
            return 0
        if self.__logging_flags.is_valgrind():
            exit_code = test_helper.run_valgrind_test(source_path, build_path, self.__shell_flag())
            self.summary_reporter.add_valgrind_report(build_path, exit_code)
            return exit_code
        return test_helper.run_ctest(
            self.__value["builder"],
            self.__value["build_enum"],
            self.__value["system"],
            self.__value["verbosity"],
            self.__shell_flag(),
            sanitizer_type=self.__logging_flags.get_sanitizer_type(),
            source_path=source_path,
        )

    def coverage(self, source_path, build_path):
        if self.__value["build"] != "build" or not self.__logging_flags.is_coverage():
            return 0
        print_status("Starting code coverage collection and report generation...", "INFO")
        try:
            from coverage_tool import get_coverage
        except ImportError:
            print_status("coverage-tool is not installed. Install with: pip install coverage-tool", "ERROR")
            return 1

        coverage_result = get_coverage(
            compiler="auto",
            build_folder=build_path,
            source_folder=source_path,
            output_folder=os.path.join(build_path, "coverage_report"),
            summary=True,
            project_root=source_path,
        )
        if coverage_result == 0:
            print_status("Coverage collection completed successfully", "SUCCESS")
            self.summary_reporter.add_coverage_report(build_path, 0)
            return 0
        print_status("Coverage collection failed", "ERROR")
        return 1

    def __shell_flag(self):
        return self.__value["system"] == "Windows"

    def move_to_build_folder(self):
        os.chdir("..")
        build_folder = self.__value["build_folder"]
        if os.path.isdir(build_folder) and self.__value.get("config") == "config":
            shutil.rmtree(build_folder, ignore_errors=True)
        if not os.path.isdir(build_folder):
            os.mkdir(build_folder)
        os.chdir(build_folder)
        return os.getcwd()


def parse_args(args):
    """Parse command line arguments, handling dotted-flag notation first."""
    processed_args = []
    for arg in args:
        if arg.startswith("--sanitizer."):
            sanitizer_type = arg.split(".", 1)[1].lower()
            valid_sanitizers = ["address", "undefined", "thread", "memory", "leak"]
            if sanitizer_type in valid_sanitizers:
                processed_args.extend(["sanitizer", sanitizer_type])
            else:
                print_status(f"Invalid sanitizer type: {sanitizer_type}. Valid options: {', '.join(valid_sanitizers)}", "ERROR")
                sys.exit(1)
        elif arg.startswith("--lto."):
            lto_mode = arg.split(".", 1)[1].lower()
            valid_lto_modes = ["off", "thin", "full", "ipo", "auto"]
            if lto_mode in valid_lto_modes:
                processed_args.append(f"lto.{lto_mode}")
            else:
                print_status(f"Invalid LTO mode '{lto_mode}'. Valid options: {', '.join(valid_lto_modes)}", "ERROR")
                sys.exit(1)
        elif arg.startswith("--backend."):
            backend_value = arg.split(".", 1)[1].lower()
            valid_backends = ["native", "loguru", "glog", "spdlog"]
            if backend_value in valid_backends:
                processed_args.append(backend_value)
            else:
                print_status(f"Invalid logging backend: {backend_value}. Valid options: {', '.join(valid_backends)}", "ERROR")
                sys.exit(1)
        elif arg.startswith("--linker."):
            processed_args.append(f"linker.{arg.split('.', 1)[1].lower()}")
        elif re.search(r"[/\\]", arg) and re.search(r"[Cc]lang|[Gg][Cc][Cc]|[Gg]\+\+", arg):
            # Compiler path argument (e.g. C:/msys64/mingw64/bin/clang.exe) — pass through verbatim.
            processed_args.append(arg)
        else:
            for part in re.split(r"\.|\ ", arg.lower()):
                processed_args.extend(re.split(r"_", part))
    return processed_args


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--help":
        print_status("Logging Build Configuration Helper", "INFO")
        print("\n" + "=" * 80)
        print("DEFAULT CONFIGURATION:")
        print("  Build System: Ninja (fast, cross-platform)")
        print("  Compiler:     Clang (clang/clang++)")
        print("  Backend:      Loguru (LOGGING_BACKEND default)")
        print("=" * 80)
        print("\nUsage examples:")
        print("  1. Default build (Ninja + Clang):")
        print("     setup.py config.build.test")
        print("  2. Release build with GCC:")
        print("     setup.py config.build.test.gcc.release")
        print("  3. macOS build with Xcode:")
        print("     setup.py config.build.test.xcode")
        print("  4. Build with coverage (analysis runs automatically):")
        print("     setup.py config.build.test.coverage")
        print("  5. Build against a specific logging backend:")
        print("     setup.py config.build.test.glog")
        print("     setup.py config.build.test.spdlog")
        print("     setup.py config.build.test --backend.native")
        print("\nBuild commands:")
        print("  config    - Configure the build system")
        print("  build     - Build the project")
        print("  test      - Run tests")
        print("  coverage  - Enable coverage (automatically displays summary)")
        print("\nSanitizer flags:")
        print("  --sanitizer.address | .undefined | .thread | .memory | .leak")
        print("\nAvailable options:")
        LoggingFlags([]).helper()
        return

    try:
        arg_list = parse_args(sys.argv[1:])
        if not arg_list:
            print_status("No build configuration specified. Use --help for usage information.", "ERROR")
            sys.exit(1)

        print_status(f"Starting build configuration for {platform.system()}", "INFO")
        compilation_calc = LoggingConfiguration(arg_list)

        source_path = os.path.dirname(os.getcwd())
        build_path = compilation_calc.move_to_build_folder()
        print_status(f"Build directory: {build_path}", "INFO")

        try:
            start = time.perf_counter()
            compilation_calc.config(source_path, build_path)
            config_end = time.perf_counter()

            compilation_calc.build()
            build_end = time.perf_counter()

            compilation_calc.cppcheck(source_path, build_path)
            cppcheck_end = time.perf_counter()

            compilation_calc.test(source_path, build_path)
            test_end = time.perf_counter()

            compilation_calc.coverage(source_path, build_path)
            end = time.perf_counter()

            print_status(f"Config time: {config_end - start:.4f} seconds", "INFO")
            print_status(f"Build time: {build_end - config_end:.4f} seconds", "INFO")
            print_status(f"Cppcheck time: {cppcheck_end - build_end:.4f} seconds", "INFO")
            print_status(f"Test time: {test_end - cppcheck_end:.4f} seconds", "INFO")
            print_status(f"Coverage time: {end - test_end:.4f} seconds", "INFO")
            print_status(f"Total time: {end - start:.4f} seconds", "INFO")
            print_status("Build process completed successfully!", "SUCCESS")

            compilation_calc.summary_reporter.display_summary()
            if compilation_calc.error_logger.has_errors():
                print_status(f"Error log available at: {compilation_calc.error_logger.get_log_file_path()}", "INFO")

        except SystemExit:
            compilation_calc.summary_reporter.display_summary()
            raise

    except KeyboardInterrupt:
        print_status("\nBuild process interrupted by user", "WARNING")
        sys.exit(1)
    except Exception as e:
        print_status(f"An unexpected error occurred: {e}", "ERROR")
        if DEBUG_FLAG:
            raise
        sys.exit(1)


if __name__ == "__main__":
    main()
