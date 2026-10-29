"""Documented setup actions and validated options shared by CI and local builds."""

import argparse
import contextlib
import io
import os
import re
import shutil
import subprocess
from pathlib import Path
from typing import Optional

from .options import OPTIONS

ACTIONS = {"config", "build", "test", "install", "clean", "cleanbuild", "verifycache", "verifytoolchain"}
GENERATORS = ("default", "Ninja", "Ninja Multi-Config", "Xcode", "Visual Studio 16 2019", "Visual Studio 17 2022", "Visual Studio 18 2026")
COMPILERS = ("default", "msvc", "clang", "clang-cl", "gcc")


def canonical_build_type(value: str) -> str:
    """Return CMake's canonical configuration spelling."""
    return {"debug": "Debug", "release": "Release", "relwithdebinfo": "RelWithDebInfo", "minsizerel": "MinSizeRel"}.get(value.lower(), value)


def boolean(value: str) -> str:
    """Normalize documented Boolean values without accepting arbitrary definitions."""
    if value.lower() in ("on", "true", "1", "yes"):
        return "ON"
    if value.lower() in ("off", "false", "0", "no"):
        return "OFF"
    raise argparse.ArgumentTypeError("expected on or off")


def positive(value: str) -> int:
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("expected a positive integer")
    return number


def argument_parser(bazel: bool = False) -> argparse.ArgumentParser:
    """Build the public option registry used by parsing, help, and CI validation."""
    parser = argparse.ArgumentParser(add_help=False, allow_abbrev=False)
    parser.add_argument("--compiler", choices=COMPILERS, help="Compiler selection; default respects the build system/environment")
    parser.add_argument("--target", action="append", default=[], help="Build/test target; repeat for multiple targets")
    if bazel:
        parser.add_argument("--compilation-mode", choices=("fastbuild", "dbg", "opt"), help="Bazel compilation mode")
        parser.add_argument("--test-output", choices=("errors", "all", "summary", "streamed"), default="errors", help="Test output policy")
        parser.add_argument("--batch", action="store_true", help="Run Bazel without a persistent server")
        parser.add_argument("--output-root", help="Directory for Bazel's output/cache state")
        parser.add_argument("--cxx-standard", choices=("17", "20", "23"), help="C++ language standard")
        project = Path(__file__).resolve().parents[2].name
        if project in ("Vectorization",):
            parser.add_argument("--cpu-backend", choices=("no", "sse", "avx", "avx2", "avx512", "neon", "sve"), help="CPU SIMD backend")
        if project in ("Vectorization", "Memory"):
            parser.add_argument("--gpu-backend", choices=("none", "cuda", "hip", "metal"), help="GPU backend")
        if project == "Logging":
            parser.add_argument("--backend", choices=("native", "loguru", "glog", "spdlog"), type=str.lower, help="Logging backend")
        if project == "Parallel":
            parser.add_argument("--backend", choices=("sequential", "stdthread", "openmp", "tbb"), help="Parallel backend")
        return parser
    parser.add_argument("--source-dir", help="Project or installed-package consumer source directory")
    parser.add_argument("--build-dir", help="Build directory; relative paths use the invoking directory")
    parser.add_argument("--build-type", type=canonical_build_type, choices=("Debug", "Release", "RelWithDebInfo", "MinSizeRel"), help="Build and test configuration")
    parser.add_argument("--generator", choices=GENERATORS, help="Build generator; default respects CMAKE_GENERATOR")
    parser.add_argument("--c-compiler", help="C compiler executable")
    parser.add_argument("--cxx-compiler", help="C++ compiler executable")
    parser.add_argument("--architecture", help="Visual Studio target architecture")
    parser.add_argument("--preload-file", help="Initial project configuration file")
    parser.add_argument("--parallel", type=positive, default=4, help="Number of build jobs (default: 4)")
    parser.add_argument("--install-prefix", help="Installation prefix for config/install actions")
    parser.add_argument("--test-filter", help="Only run tests matching this regular expression")
    parser.add_argument("--test-label", help="Only run tests with a matching label")
    parser.add_argument("--test-timeout", type=positive, default=300, help="Per-test timeout in seconds (default: 300)")
    parser.add_argument("--memcheck", action="store_true", help="Run the CTest memory-check dashboard")
    parser.add_argument("--verbose", action="store_true", help="Show verbose build/test output")
    for option in OPTIONS:
        kind = option["kind"]
        converter = boolean if kind in ("bool", "cuda-compat") else (str.upper if kind == "upper-enum" else str.lower if kind == "enum" else str)
        aliases = ["--" + option["name"]]
        if "-" in option["name"]:
            aliases.append("--" + option["name"].replace("-", "_"))
        parser.add_argument(*aliases, dest=option["name"].replace("-", "_"), type=converter, choices=option.get("choices"), help=f"Set {option['name']} ({'on/off' if kind in ('bool','cuda-compat') else 'project setting'})")
    return parser


def print_help() -> None:
    """Print the supported public actions and named options."""
    print("\nSupported setup actions (combine with dots):")
    print("  config.build.test, install, cleanbuild, verifycache, verifytoolchain")
    print("  python Scripts/setup.py config.build.test.release.ninja.clang --build-dir build")
    print("  python Scripts/setup.py install.release --build-dir build --install-prefix install")
    print("\nOfficial named options:")
    print(argument_parser().format_help())


def print_bazel_help() -> None:
    print("\nSupported Bazel actions: config.build.test, clean, coverage")
    print("  python Scripts/setup_bazel.py build.test --target //Testing/... --compiler default")
    print(argument_parser(bazel=True).format_help())


def _reject_raw(arguments: list[str], parser: argparse.ArgumentParser) -> None:
    for argument in arguments:
        if argument in ("--", "cmake", "ctest", "bazel") or argument.startswith(("-D", "--cmake", "--copt", "--define", "--repo_env", "--test_output")):
            parser.error(f"unsupported argument {argument!r}; use documented setup actions and options")


def parse_options(arguments: list[str], bazel: bool = False):
    """Parse only declared options, retaining documented dotted legacy flags."""
    parser = argument_parser(bazel)
    _reject_raw(arguments, parser)
    options, remaining = parser.parse_known_args(arguments)
    legacy_prefixes = ("--backend.", "--gpu.", "--gpu_backend.", "--cpu_backend.", "--sanitizer.", "--lto.", "--linker.", "--packet-size=")
    for argument in remaining:
        if argument.startswith("-") and not argument.startswith(legacy_prefixes):
            parser.error(f"unrecognized option {argument!r}; see --help")
    return options, remaining


def validate_legacy(tokens, flags_factory, parser):
    """Reject misspelled actions rather than silently ignoring them."""
    flags = flags_factory([])
    allowed = set(next(value for name, value in vars(flags).items() if name.endswith("__key")))
    allowed.update(ACTIONS | {"release", "debug", "relwithdebinfo", "minsizerel", "ninja", "xcode", "vs17", "vs19", "vs22", "vs26", "clang", "clang-cl", "gcc", "msvc", "shared", "address", "undefined", "thread", "memory", "leak", "ccache", "sccache", "buildcache", "none", "no", "sse", "avx", "avx2", "avx512", "neon", "sve", "cuda", "hip", "metal", "native", "loguru", "glog", "spdlog", "sequential", "stdthread", "openmp", "tbb", "v", "vv", "torch", "noinstall", "tao", "logfatal", "throw", "nocxademangle", "nomimalloc", "batch", "clean"})
    for token in tokens:
        if token in allowed or token.startswith(("lto.", "linker.", "backend.", "gpu.", "gpu_backend.", "cpu_backend.", "parallel_backend.", "packet_size.")) or re.fullmatch(r"(?:cxx|c\+\+)\d+|psize\d+|sve(?:128|256|512)", token):
            continue
        if re.search(r"(?:clang|gcc|g\+\+)", token) and token not in ("cmake", "ctest"):
            continue
        parser.error(f"unrecognized action or flag {token!r}; see --help")


def _run(command: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(command, check=True, **kwargs)


def _definitions(options) -> list[str]:
    definitions = []
    for option in OPTIONS:
        value = getattr(options, option["name"].replace("-", "_"))
        if value is None:
            continue
        if option["kind"] == "cuda-compat":
            value = "-allow-unsupported-compiler" if value == "ON" else ""
        if option["kind"] == "linker-flags":
            value = "" if value == "default" else "-fuse-ld=" + value
        definitions.append(f"-D{option['cache']}={value}")
    if options.install_prefix:
        definitions.append(f"-DCMAKE_INSTALL_PREFIX={options.install_prefix}")
    return definitions


def _explicit_legacy_definitions(tokens, flags_factory):
    """Translate explicit dotted feature choices without overriding project defaults."""
    with contextlib.redirect_stdout(io.StringIO()):
        baseline = flags_factory(["config"])
        selected = flags_factory(tokens or ["config"])
        baseline_flags, selected_flags = [], []
        baseline.create_cmake_flags(baseline_flags, "Release", os.name)
        selected.create_cmake_flags(selected_flags, "Release", os.name)
    defaults = dict(flag.split("=", 1) for flag in baseline_flags)
    return [flag for flag in selected_flags if defaults.get(flag.split("=", 1)[0]) != flag.split("=", 1)[1]]


def _configuration(build, requested):
    if requested:
        return requested
    cache = build / "CMakeCache.txt"
    if cache.is_file():
        match = re.search(r"^CMAKE_BUILD_TYPE:\w+=(.+)$", cache.read_text(encoding="utf-8"), re.M)
        if match:
            return canonical_build_type(match.group(1))
    return "Release"


def verify_cache(build: Path, target: str, parallel: int) -> None:
    """Require a real cache launcher and hits from a clean rebuild."""
    if not target:
        raise ValueError("verifycache requires --target")
    commands = _run(["cmake", "--build", str(build), "--target", target, "--", "-t", "commands"], capture_output=True, text=True).stdout
    (build / "compile-commands.txt").write_text(commands, encoding="utf-8")
    if not re.search(r'buildcache(?:\.exe)?"? .* (?:-c |/c |/Fo)', commands, re.I):
        raise RuntimeError("BuildCache is missing from the generated compiler commands")
    _run(["cmake", "--build", str(build), "--parallel", str(parallel)])
    _run(["cmake", "--build", str(build), "--clean-first", "--parallel", str(parallel)])
    statistics = _run(["buildcache", "--show-stats"], capture_output=True, text=True).stdout
    print(statistics, end="")
    (build / "cache-stats.txt").write_text(statistics, encoding="utf-8")
    if not re.search(r"(?:Direct|Local|Remote) hits: +[1-9]", statistics):
        raise RuntimeError("The clean rebuild did not produce any BuildCache hits")


def verify_toolchain(build: Path, expected: str, generator: str = "Ninja") -> None:
    """Verify the realized generator and compiler rather than trusting a matrix label."""
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    if not re.search(rf"^CMAKE_GENERATOR:INTERNAL={re.escape(generator)}$", cache, re.M):
        raise RuntimeError(f"Expected the {generator} generator")
    files = list((build / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    if len(files) != 1:
        raise RuntimeError("Expected one CMake C++ compiler description")
    compiler = files[0].read_text(encoding="utf-8")
    expected_ids = {"msvc": ("MSVC",), "clang": ("Clang", "AppleClang"), "clang-cl": ("Clang",), "gcc": ("GNU",)}.get(expected, ())
    if not any(f'set(CMAKE_CXX_COMPILER_ID "{expected_id}")' in compiler for expected_id in expected_ids):
        raise RuntimeError(f"CMake did not select the requested compiler {expected}")
    frontend = {"clang": "GNU", "clang-cl": "MSVC"}.get(expected)
    if frontend and f'set(CMAKE_CXX_COMPILER_FRONTEND_VARIANT "{frontend}")' not in compiler:
        raise RuntimeError(f"Expected the {frontend} frontend")
    print(f"Verified {expected} with {generator}")


def dispatch(arguments, root, flags_factory, configuration_factory, parse_legacy) -> Optional[int]:
    """Execute documented named options, leaving existing dotted usage compatible."""
    if arguments == ["--help"]:
        return None
    options, remaining = parse_options(arguments)
    tokens = parse_legacy(remaining)
    parser = argument_parser()
    validate_legacy(tokens, flags_factory, parser)
    modern = "msvc" in tokens or len(remaining) != len(arguments) or bool(set(tokens) & {"install", "cleanbuild", "clean", "verifycache", "verifytoolchain"})
    if not modern:
        return None
    phases = set(tokens) & ACTIONS
    if not phases:
        parser.error("specify an action such as config.build.test")
    root = Path(root).resolve()
    source = Path(options.source_dir).resolve() if options.source_dir else root
    build = Path(options.build_dir).resolve() if options.build_dir else root / "build_ninja"
    requested_type = options.build_type or next((canonical_build_type(token) for token in tokens if token in ("debug", "release", "relwithdebinfo", "minsizerel")), None)
    compiler = options.compiler or next((token for token in tokens if token in COMPILERS), "clang")
    generator = options.generator or ("Xcode" if "xcode" in tokens else "Ninja")
    definitions = _explicit_legacy_definitions(tokens, flags_factory) + _definitions(options)
    try:
        if "config" in phases:
            command = ["cmake", "-S", str(source), "-B", str(build)]
            if generator != "default":
                command.extend(["-G", generator])
            if options.architecture:
                command.extend(["-A", options.architecture])
            if options.preload_file:
                command.extend(["-C", str(Path(options.preload_file).resolve())])
            if requested_type or not options.preload_file:
                command.append("-DCMAKE_BUILD_TYPE=" + (requested_type or "Release"))
            compilers = {"clang": ("clang", "clang++"), "gcc": ("gcc", "g++"), "clang-cl": ("clang-cl", "clang-cl"), "msvc": ("cl", "cl")}
            cc, cxx = compilers.get(compiler, (None, None))
            cc, cxx = options.c_compiler or cc, options.cxx_compiler or cxx
            if generator.startswith("Visual Studio") and compiler == "clang-cl":
                command.extend(["-T", "ClangCL"])
                cc, cxx = None, None
            if cc:
                command.append("-DCMAKE_C_COMPILER=" + cc)
            if cxx:
                command.append("-DCMAKE_CXX_COMPILER=" + cxx)
            _run(command + definitions)
        build_type = _configuration(build, requested_type)
        if "verifytoolchain" in phases:
            verify_toolchain(build, compiler, generator)
        if "clean" in phases:
            _run(["cmake", "--build", str(build), "--target", "clean", "--config", build_type])
        if "build" in phases or "cleanbuild" in phases:
            command = ["cmake", "--build", str(build), "--config", build_type, "--parallel", str(options.parallel)]
            if "cleanbuild" in phases:
                command.append("--clean-first")
            if options.target:
                command.extend(["--target", *options.target])
            if options.verbose:
                command.append("--verbose")
            _run(command)
        analysis = set(tokens) & {"cppcheck", "coverage", "spell", "benchmark"}
        configuration = None
        if analysis:
            configuration = configuration_factory(tokens)
            values = getattr(configuration, "_" + type(configuration).__name__ + "__value")
            values["build_enum"] = build_type
        original_dir = Path.cwd()
        try:
            if analysis:
                os.chdir(build)
            if "cppcheck" in analysis:
                result = configuration.cppcheck(str(source), str(build))
                if result:
                    return result
            if "verifycache" in phases:
                verify_cache(build, options.target[0] if options.target else None, options.parallel)
            if "test" in phases:
                command = ["ctest", "--test-dir", str(build), "-C", build_type, "--output-on-failure", "--no-tests=error", "--timeout", str(options.test_timeout)]
                if options.test_filter:
                    command.extend(["-R", options.test_filter])
                if options.test_label:
                    command.extend(["-L", options.test_label])
                if options.memcheck or "valgrind" in tokens:
                    command.extend(["-T", "memcheck"])
                if options.verbose:
                    command.append("-VV")
                _run(command)
            if "install" in phases:
                command = ["cmake", "--install", str(build), "--config", build_type]
                if options.install_prefix:
                    command.extend(["--prefix", options.install_prefix])
                _run(command)
            for action in ("coverage", "spell", "benchmark"):
                if action in analysis:
                    method = getattr(configuration, action, None)
                    if method is None:
                        continue
                    result = method(str(build)) if action == "benchmark" else method(str(source)) if action == "spell" else method(str(source), str(build))
                    if result:
                        return result
        finally:
            os.chdir(original_dir)
        return 0
    except subprocess.CalledProcessError as error:
        return error.returncode
    except (OSError, RuntimeError, ValueError) as error:
        print(error)
        return 1


def dispatch_bazel(arguments, root=None, force=False) -> Optional[int]:
    """Run the documented Bazel actions without accepting raw Bazel arguments."""
    if arguments == ["--help"]:
        print_bazel_help()
        return 0 if force else None
    options, remaining = parse_options(arguments, bazel=True)
    if not force and len(arguments) == len(remaining):
        return None
    tokens = set(".".join(remaining).lower().split("."))
    allowed = {"config", "build", "test", "clean", "coverage", "debug", "release", "clang", "gcc", "clang-cl", "msvc", "batch"}
    if not tokens or not tokens <= allowed:
        argument_parser(True).error("use config.build.test[.debug|.release] and documented named options")
    executable = shutil.which("bazelisk") or shutil.which("bazel")
    if not executable:
        print("Install bazelisk or bazel")
        return 1
    root = Path(root or Path(__file__).resolve().parents[2]).resolve()
    prefix = [executable]
    if executable.lower().endswith((".cmd", ".bat")):
        prefix = ["cmd.exe", "/c", *prefix]
    if options.batch or "batch" in tokens:
        prefix.append("--batch")
    if options.output_root:
        prefix.append("--output_user_root=" + str(Path(options.output_root).resolve()))
    settings = []
    mode = options.compilation_mode or ("dbg" if "debug" in tokens else "opt" if "release" in tokens else None)
    if mode:
        settings.extend(["-c", mode])
    compiler = options.compiler or next((token for token in tokens if token in COMPILERS), "default")
    if compiler != "default":
        cc, cxx = {"clang": ("clang", "clang++"), "gcc": ("gcc", "g++"), "clang-cl": ("clang-cl", "clang-cl"), "msvc": ("cl", "cl")}[compiler]
        settings.extend(["--repo_env=CC=" + cc, "--repo_env=CXX=" + cxx])
    if options.cxx_standard:
        standard = ("/std:c++" if os.name == "nt" else "-std=c++") + options.cxx_standard
        settings.extend(["--cxxopt=" + standard, "--host_cxxopt=" + standard])
    for name in ("cpu_backend", "gpu_backend", "backend"):
        value = getattr(options, name, None)
        if value is not None:
            project = {"Vectorization": "vectorization", "Memory": "memory", "Logging": "logging", "Parallel": "parallel"}[root.name]
            setting = project + ("_backend" if name == "backend" else "_" + name)
            settings.append(f"--define={setting}={value}")
            if root.name == "Vectorization" and name == "gpu_backend":
                settings.append("--define=memory_gpu_backend=" + value)
    targets = options.target or ["//..."]
    try:
        if "clean" in tokens:
            _run(prefix + ["clean"], cwd=root)
        if tokens == {"config"}:
            _run(prefix + ["build", *settings, "--nobuild", "--", *targets], cwd=root)
        for action in ("build", "test", "coverage"):
            if action in tokens:
                output = ["--test_output=" + options.test_output] if action != "build" else []
                _run(prefix + [action, *settings, *output, "--", *targets], cwd=root)
        return 0
    except subprocess.CalledProcessError as error:
        return error.returncode
    except OSError as error:
        print(error)
        return 1
