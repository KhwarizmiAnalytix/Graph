"""Check MSVC debug flags without requiring a Windows host."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


@unittest.skipUnless(shutil.which("cmake"), "CMake is required")
class PlatformTests(unittest.TestCase):
    def test_msvc_debug_information_does_not_share_a_compiler_pdb(self):
        source = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            probe = Path(directory)
            (probe / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.16)\n"
                "project(MsvcFlags NONE)\n"
                "set(MSVC TRUE)\n"
                "set(CMAKE_CXX_COMPILER_ID MSVC)\n"
                "set(UNIX FALSE)\n"
                "set(APPLE FALSE)\n"
                "set(CMAKE_CONFIGURATION_TYPES Debug Release RelWithDebInfo MinSizeRel Custom)\n"
                "foreach(language C CXX)\n"
                "foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL CUSTOM)\n"
                'set(CMAKE_${language}_FLAGS_${config} "/Zi /DKEEP_${config}")\n'
                "endforeach()\n"
                "endforeach()\n"
                f'include("{source.as_posix()}/Cmake/flags/platform.cmake")\n'
                'file(WRITE "${CMAKE_BINARY_DIR}/debug-format.txt" '
                '"${CMAKE_MSVC_DEBUG_INFORMATION_FORMAT}")\n'
                "foreach(language C CXX)\n"
                "foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL CUSTOM)\n"
                'file(WRITE "${CMAKE_BINARY_DIR}/${language}-${config}.txt" '
                '"${CMAKE_${language}_FLAGS} ${CMAKE_${language}_FLAGS_${config}}")\n'
                "endforeach()\n"
                "endforeach()\n",
                encoding="utf-8",
            )
            build = probe / "build"
            result = subprocess.run(
                ["cmake", "-S", str(probe), "-B", str(build)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((build / "debug-format.txt").read_text(), "Embedded")
            for language in ("C", "CXX"):
                for config in (
                    "DEBUG",
                    "RELEASE",
                    "RELWITHDEBINFO",
                    "MINSIZEREL",
                    "CUSTOM",
                ):
                    with self.subTest(language=language, config=config):
                        flags = (build / f"{language}-{config}.txt").read_text()
                        self.assertIn("/Z7", flags)
                        self.assertNotRegex(flags, r"[-/]Z[iI]\b")
                        self.assertIn(f"/DKEEP_{config}", flags)


if __name__ == "__main__":
    unittest.main()
