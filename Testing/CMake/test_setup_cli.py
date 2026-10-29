"""Regression tests for reproducible CI commands and setup failure handling."""

import contextlib
import importlib.util
import io
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "Scripts"
SPEC = importlib.util.spec_from_file_location("ci_setup", SCRIPTS / "setup.py")
setup = importlib.util.module_from_spec(SPEC)
with patch.object(sys, "path", [str(SCRIPTS), *sys.path]):
    SPEC.loader.exec_module(setup)
Configuration = next(
    value
    for name, value in vars(setup).items()
    if name.endswith("Configuration") and isinstance(value, type)
)
Flags = next(
    value
    for name, value in vars(setup).items()
    if name.endswith("Flags") and isinstance(value, type)
)
cli = setup.cli_helper


class SetupCliTests(unittest.TestCase):
    def setUp(self):
        self.output = contextlib.redirect_stdout(io.StringIO())
        self.output.__enter__()
        self.addCleanup(self.output.__exit__, None, None, None)

    def flags(self, *arguments):
        with patch.object(setup.importlib.util, "find_spec", return_value=object()):
            flags = Flags(setup.parse_args(list(arguments)))
        command = []
        build_type = flags.create_cmake_flags(
            command, flags.requested_build_type or "Release", "Windows"
        )
        return dict(argument[2:].split("=", 1) for argument in command), build_type

    def configuration(self, *arguments):
        with (
            patch.object(setup, "check_dependencies", return_value=[]),
            patch.object(setup, "ErrorLogger"),
        ):
            return Configuration(setup.parse_args(list(arguments)))

    def test_help_does_not_require_optional_packages(self):
        result = subprocess.run(
            [sys.executable, "-B", "-S", str(SCRIPTS / "setup.py"), "--help"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Exact CI commands", result.stdout)

    def test_native_commands_preserve_arguments_and_exit_status(self):
        arguments = [
            "cmake",
            "-S",
            "source with spaces",
            "-B",
            "build directory",
            "-DPaths=a;b",
        ]
        with patch.object(
            cli.subprocess, "run", return_value=Mock(returncode=0)
        ) as run:
            self.assertEqual(cli.dispatch(arguments), 0)
        run.assert_called_once_with(arguments, check=True)
        with patch.object(
            cli.subprocess, "run", side_effect=subprocess.CalledProcessError(7, "ctest")
        ):
            self.assertEqual(cli.dispatch(["ctest", "--test-dir", "build"]), 7)

    def test_main_native_mode_does_not_construct_legacy_pipeline(self):
        with (
            patch.object(setup, Configuration.__name__) as constructor,
            patch.object(cli.subprocess, "run", return_value=Mock(returncode=5)),
            patch.object(sys, "argv", ["setup.py", "cmake", "--install", "build"]),
            self.assertRaises(SystemExit) as error,
        ):
            setup.main()
        self.assertEqual(error.exception.code, 5)
        constructor.assert_not_called()

    def test_raw_overrides_preserve_case_spaces_and_lists(self):
        arguments = [
            "config.build",
            "--",
            "-DCMAKE_PREFIX_PATH=C:/SDK Folder;D:/Other",
            "-DCMAKE_CUDA_ARCHITECTURES=75",
            "-DCMAKE_BUILD_TYPE=Release",
        ]
        tokens, overrides = cli.split_cmake_args(arguments)
        self.assertEqual(tokens, ["config.build"])
        self.assertEqual(overrides, arguments[2:])
        flags = ["-DCMAKE_BUILD_TYPE=Debug"]
        self.assertEqual(cli.apply_cmake_args(flags, overrides, "Debug"), "Release")
        self.assertEqual(flags[-3:], overrides)

    def test_configure_applies_raw_overrides_after_defaults(self):
        config = self.configuration("config")
        config.extra_cmake_args = [
            "-DCMAKE_BUILD_TYPE=Release",
            "-DREPRODUCIBLE_OPTION=OFF",
        ]
        with patch.object(
            setup.config_helper, "configure_build", return_value=0
        ) as configure:
            config.config(str(ROOT), "/tmp/build directory")
        self.assertEqual(configure.call_args.args[5][-2:], config.extra_cmake_args)

    def test_requested_phase_failure_stops_pipeline(self):
        for stage in ("test", "cppcheck", "coverage"):
            with self.subTest(stage=stage):
                config = Mock(source_path=ROOT)
                for method in (
                    "config",
                    "build",
                    "test",
                    "cppcheck",
                    "coverage",
                    "spell",
                    "benchmark",
                ):
                    getattr(config, method).return_value = 0
                config.error_logger.has_errors.return_value = False
                getattr(config, stage).return_value = 9
                with (
                    patch.object(setup, Configuration.__name__, return_value=config),
                    patch.object(sys, "argv", ["setup.py", "config.build.test"]),
                    self.assertRaises(SystemExit) as error,
                ):
                    setup.main()
                self.assertNotEqual(error.exception.code, 0)
                if stage == "cppcheck":
                    config.test.assert_not_called()

    def test_ninja_clang_cl_selects_compiler_without_vs_toolset(self):
        with patch.object(setup.platform, "system", return_value="Windows"):
            config = self.configuration("config.ninja.clang-cl")
        values = getattr(config, "_" + Configuration.__name__ + "__value")
        self.assertEqual(values["cmake_generator"], "Ninja")
        self.assertEqual(values["cmake_c_compiler"], "-DCMAKE_C_COMPILER=clang-cl")
        self.assertEqual(values["cmake_cxx_compiler"], "-DCMAKE_CXX_COMPILER=clang-cl")

    def test_configure_separates_paths_and_uses_no_shell(self):
        with patch.object(setup.config_helper.subprocess, "check_call") as run:
            self.assertEqual(
                setup.config_helper.configure_build(
                    "source directory",
                    "build directory",
                    "Ninja",
                    "-DCMAKE_CXX_COMPILER=clang-cl",
                    "-DCMAKE_C_COMPILER=clang-cl",
                    [],
                    "--loglevel=NOTICE",
                    True,
                ),
                0,
            )
        command = run.call_args.args[0]
        self.assertEqual(
            command[1:5], ["-S", "source directory", "-B", "build directory"]
        )
        self.assertFalse(run.call_args.kwargs["shell"])
        self.assertNotIn("-DCMAKE_GENERATOR_TOOLSET=ClangCL", command)

    def test_vs_clang_cl_toolset_is_independent_of_token_order(self):
        for tokens in ("config.vs22.clang-cl", "config.clang-cl.vs22"):
            with (
                self.subTest(tokens=tokens),
                patch.object(setup.platform, "system", return_value="Windows"),
            ):
                config = self.configuration(tokens)
                values = getattr(config, "_" + Configuration.__name__ + "__value")
                with patch.object(setup.config_helper.subprocess, "check_call") as run:
                    setup.config_helper.configure_build(
                        "source",
                        "build",
                        values["cmake_generator"],
                        values["cmake_cxx_compiler"],
                        values["cmake_c_compiler"],
                        [],
                        "--loglevel=NOTICE",
                        False,
                    )
                command = run.call_args.args[0]
                self.assertEqual(command[command.index("-T") + 1], "ClangCL")

    def test_build_directory_uses_script_root_and_preserves_existing_files(self):
        original = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "project with spaces"
            (root / "Scripts").mkdir(parents=True)
            try:
                os.chdir(directory)
                with patch.object(setup, "__file__", str(root / "Scripts/setup.py")):
                    config = self.configuration("config.static")
                self.assertEqual(config.source_path, root.resolve())
                values = getattr(config, "_" + Configuration.__name__ + "__value")
                build = root / values["build_folder"]
                build.mkdir()
                (build / "keep.txt").write_text("keep", encoding="utf-8")
                self.assertEqual(Path(config.move_to_build_folder()), build.resolve())
                self.assertEqual(
                    (build / "keep.txt").read_text(encoding="utf-8"), "keep"
                )
            finally:
                os.chdir(original)

    def test_explicit_release_is_respected_with_instrumentation(self):
        _, build_type = self.flags("config.release.coverage")
        self.assertEqual(build_type, "Release")

    def test_relwithdebinfo_retains_cmake_spelling(self):
        _, build_type = self.flags("config.relwithdebinfo")
        self.assertEqual(build_type, "RelWithDebInfo")

    def test_cache_backend_when_ci_requires_it(self):
        if ROOT.name not in ("Numericals", "Profiler", "Solvers"):
            self.skipTest("Additional cache options apply to these three helpers")
        values, _ = self.flags("config.buildcache.icecc")
        prefix = ROOT.name.upper()
        self.assertEqual(values[prefix + "_CACHE_BACKEND"], "buildcache")
        self.assertEqual(values[prefix + "_ENABLE_CACHE"], "ON")
        self.assertEqual(values[prefix + "_ENABLE_ICECC"], "ON")

    def test_combined_sanitizers_are_not_lost(self):
        if ROOT.name in ("Numericals", "Solvers"):
            self.skipTest(
                "These projects have a single combined-sanitizer CMake switch"
            )
        values, _ = self.flags("config", "--sanitizer.address", "--sanitizer.undefined")
        name = (
            "PROFILER_SANITIZER"
            if ROOT.name == "Profiler"
            else (
                {"random": "RANDOM", "LinearAlgebra": "LINALG"}.get(
                    ROOT.name, ROOT.name.upper()
                )
            )
            + "_SANITIZER_TYPE"
        )
        self.assertEqual(values[name], "address,undefined")

    def test_cache_verification_requires_real_hits(self):
        with tempfile.TemporaryDirectory() as directory:
            results = [
                Mock(stdout="buildcache clang -c test.cpp"),
                Mock(),
                Mock(),
                Mock(stdout="Direct hits: 0\nLocal hits: 0\nRemote hits: 0\n"),
            ]
            with patch.object(cli.subprocess, "run", side_effect=results):
                self.assertEqual(
                    cli.dispatch(
                        [
                            "verify-cache",
                            "--build-dir",
                            directory,
                            "--target",
                            "Library",
                        ]
                    ),
                    1,
                )
            results[-1] = Mock(stdout="Direct hits: 2\n")
            with patch.object(cli.subprocess, "run", side_effect=results):
                self.assertEqual(
                    cli.dispatch(
                        [
                            "verify-cache",
                            "--build-dir",
                            directory,
                            "--target",
                            "Library",
                        ]
                    ),
                    0,
                )

    def test_toolchain_verification_rejects_a_mislabeled_compiler(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "CMakeCache.txt").write_text(
                "CMAKE_GENERATOR:INTERNAL=Ninja\n", encoding="utf-8"
            )
            compiler = build / "CMakeFiles/4.1/CMakeCXXCompiler.cmake"
            compiler.parent.mkdir(parents=True)
            compiler.write_text('set(CMAKE_CXX_COMPILER_ID "MSVC")\n', encoding="utf-8")
            self.assertEqual(
                cli.dispatch(
                    [
                        "verify-toolchain",
                        "--build-dir",
                        directory,
                        "--compiler",
                        "clang",
                    ]
                ),
                1,
            )
            self.assertEqual(
                cli.dispatch(
                    ["verify-toolchain", "--build-dir", directory, "--compiler", "msvc"]
                ),
                0,
            )

    def test_exact_bazel_commands_preserve_target_filters_and_failures(self):
        with (
            patch.object(cli.shutil, "which", return_value="bazel"),
            patch.object(cli.subprocess, "run", return_value=Mock(returncode=0)) as run,
        ):
            self.assertEqual(
                cli.dispatch_bazel(
                    ["--", "test", "//Testing/...", "--test_output=errors"]
                ),
                0,
            )
        run.assert_called_once_with(
            ["bazel", "test", "//Testing/...", "--test_output=errors"], check=True
        )
        with (
            patch.object(cli.shutil, "which", return_value="bazel"),
            patch.object(
                cli.subprocess,
                "run",
                side_effect=subprocess.CalledProcessError(4, "bazel"),
            ),
        ):
            self.assertEqual(cli.dispatch_bazel(["--", "test", "//..."]), 4)


if __name__ == "__main__":
    unittest.main()
