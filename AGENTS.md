# Repository Guidelines

## Project Structure & Module Organization

Stana is a C++23 codebase. Writing safe code is as paramount as performant code.

This is a retained desktop UI toolkit with one public module: `import stana;`.
Interfaces live in `modules/<function>/`; implementations mirror them in `src/<function>/`.
Native adapters in `src/platform/*`.

`tests/` contains subsystem contracts, policy checks, and consumer integration tests.

Build configuration lives in `cmake/`; developer scripts live in `tools/`.
Start with [readme folder](readme/), especially `documentation conventions`

## Build, Test, and Development Commands

Use LLVM 22.1.8, ICU4C 78.3, CMake 4.4+, Ninja 1.11+, and Python 3.11+.
See README.md for platform prerequisites and `LLVM_ROOT`/`ICU_ROOT` configuration.

- `just build` — configure and build Debug with mandatory source analysis.
- `just test` — build Debug and run CTest.
- `just test release` — build and test Release.
- `just example counter` — build and launch the native macOS example.
- `just format` / `just format-check` — apply or verify first-party C++ formatting.

Without Just, run `cmake --preset debug`, `cmake --build --preset debug`, then
`ctest --preset debug`. Keep each preset's build directory and module cache separate.
Put personal overrides in untracked `CMakeUserPresets.json`, following
[CMake's preset guidance](https://cmake.org/cmake/help/v4.4/manual/cmake-presets.7.html).

## Coding Style & Naming Conventions

Use tabs (width 4), LF endings, and the configured 100-column LLVM-based
[clang-format style](https://clang.llvm.org/docs/ClangFormatStyleOptions.html).
Markdown/YAML use two-space indentation. Follow existing `PascalCase` types and
`camelCase` functions/properties. Keep private helpers in subsystem `detail`
namespaces and native types out of exported APIs. Document public contracts with
Doxygen comments per `readme/documentation.md`. Fix clang-tidy diagnostics; do not
add suppression comments, raw `new`/`delete`, or C-style casts.

## Testing Guidelines

CTest runs standalone C++ contract executables and Python checks; no numeric
coverage threshold is configured. Add regressions under the relevant subsystem,
using names such as `contracts.cpp` or descriptive snake_case filenames, and
register tests in `tests/CMakeLists.txt` as `stana.<feature>`.
Use `just test asan` or, on Linux/macOS, `just test asan-ubsan` for sanitizer checks.
Native window/Metal gates require `-DSTANA_NATIVE_TESTS=ON` and a graphical macOS session.

## Commit & Pull Request Guidelines

Prefer [Conventional Commits format](https://www.conventionalcommits.org/en/v1.0.0/).
Keep PRs focused; describe behavior changes, linked issues, and verification commands.
Include screenshots for visible UI changes and migration notes for breaking APIs.

DO NOT add any new examples.

DO NOT edit README or any other markdown file.
