#!/usr/bin/env python3
"""Install clang-format and write .lintbin/clang-format as a pip wrapper.

The clangformat adapter requires Path(binary).exists(), so a bare
``clang-format`` on PATH is not enough. Copying Homebrew LLVM's binary also
fails (rpath looks for libclang-cpp.dylib next to .lintbin). The PyPI
``clang-format`` wheel is a console script; wrap that module instead.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

_WRAPPER = """\
#!/usr/bin/env python3
import sys
from clang_format import clang_format

if __name__ == "__main__":
    raise SystemExit(clang_format())
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", default="0")
    args = parser.parse_args()

    subprocess.check_call(
        [
            sys.executable,
            "-m",
            "lint_tool.adapters.pip_init",
            f"--dry-run={args.dry_run}",
            "clang-format==19.1.4",
        ]
    )
    dest = pathlib.Path(".lintbin") / "clang-format"
    dest.parent.mkdir(exist_ok=True)
    if dest.exists() or dest.is_symlink():
        dest.unlink()
    dest.write_text(_WRAPPER, encoding="utf-8")
    dest.chmod(0o755)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
