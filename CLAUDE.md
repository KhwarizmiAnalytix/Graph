# Graph

Standalone C++ graph algorithms and data structures library. Source lives in `include/`; tests live in `Testing/Cxx/`. Use namespace `graph` and `GRAPH_*` export macros. Dependencies are under `ThirdParty/`.

## Shared agent guidance

Adapted from the public [XSigma rules and skills](https://github.com/KhwarizmiAnalytix/XSigma/tree/89848c54492abef57fd0d0dc53b9da96b7cd1d5d)
at revision `89848c54492abef57fd0d0dc53b9da96b7cd1d5d`. Local API, dependency, language, and build
conventions below specialize that guidance for this standalone repository.

Read the applicable rules before editing. They apply to Claude as well as
Augment; C++ rules apply only when working on C++:

- [C++ coding](.augment/rules/coding.md) and [builders](.augment/rules/builder.md)
- [Python](.augment/rules/python.md)
- [Testing](.augment/rules/testing.md) and [builds](.augment/rules/build%20rule.md)
- [Dependencies](.augment/rules/ThirdParty.md)
- [Portability](.augment/rules/must-have.md) and [documentation](.augment/rules/markdown.md)

Use these task-specific skills as needed:

- [project-build](.claude/skills/project-build/SKILL.md): configure, build, and test
- [new-test](.claude/skills/new-test/SKILL.md): add tests using local conventions
- [clang-tidy](.claude/skills/clang-tidy/SKILL.md): analyze first-party C++ when applicable
- [session-checklist](.claude/skills/session-checklist/SKILL.md): verify completed work

## Build and test

Use the setup helper from `Scripts/`; inspect its help before adding
feature flags:

```sh
cd Scripts
python3 setup.py --help
python3 setup.py config.build.test
```

For compiler or generator requirements, follow `README.md` and CI.
The repository also documents direct CMake commands for integration and CI.

For Bazel, also run from `Scripts/`:

```sh
python3 setup_bazel.py config.build.test
```

## Test conventions

Match adjacent test cases and testing framework conventions in the repository.
Tests use `Test*.cpp` or `Test*.cxx` under `Testing/Cxx/`; CMake uses a recursive glob
while Bazel uses a package-local glob. Check exclusions and register new subdirectories
in both systems when adding tests.

## Verification and scope

For non-trivial source or build changes, run affected tests, review the diff,
and run configured lint/static-analysis checks relevant to touched files.
Check both build systems where provided. Follow the session checklist and
report checks run, failures, and unavailable tools explicitly. Guidance-only
changes need frontmatter/link/whitespace validation, not compilation.

Keep unrelated user edits and dependency sources intact. Share review
findings in the response or pull request; do not create unsolicited status
documents. Follow this repository's existing license and contribution policy.

## CMake Configuration Message Alignment

All `message("  LABEL : value")` configuration summary messages in CMakeLists.txt
and `Cmake/lto.cmake` must align colons at exactly **24 characters from the opening
quote** (inclusive).

Format: `message("  LABEL{PADDING}: VALUE")`
- Opening `"`: position 1
- Two spaces + label + padding: positions 2-23 (22 chars total)
- Colon `:`: position 24

Example:
```cmake
message("  Icecc               : ${GRAPH}_ENABLE_ICECC}")
```

This ensures all colons in configuration output form a vertical line for readability.
