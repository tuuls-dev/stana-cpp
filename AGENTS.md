# Repository Guidelines

## Project Structure & Module Organization

Stana is a C++23 codebase. Writing safe code is as paramount as performant code.

This is a retained desktop UI toolkit with one public module: `import stana;`.
Interfaces live in `modules/<function>/`; implementations mirror them in `src/<function>/`.
Native adapters in `src/platform/*`.

`tests/` contains subsystem contracts and policy checks.

The Zig build starts in `build.zig` and uses helpers in `build_support/`.
Developer scripts live in `tools/`.
Start with [readme folder](readme/), especially `documentation conventions`

## Build, Test, and Development Commands

Use Zig 0.17.1, LLVM 22.1.8, ICU4C 78.3, and Python 3.11+.
See README.md for platform prerequisites and dependency provisioning.

- `just build` — build Debug with mandatory source analysis.
- `just test` — build and run the Debug contracts.
- `just test release` — build and test Release.
- `just example counter` — build and launch the native macOS example.
- `just format` / `just format-check` — apply or verify first-party C++ formatting.

Without Just, run `zig build -j3` and `zig build test -j3`. Select Release
with `--release=fast`. Run `zig build compdb` to refresh clangd's database.

## Coding Style & Naming Conventions

Read `./.clang-tidy` for lint rules and code you should avoid.

Use tabs (width 4), LF endings, and the configured 100-column LLVM-based
[clang-format style](https://clang.llvm.org/docs/ClangFormatStyleOptions.html).
Markdown/YAML use two-space indentation.

Follow existing `PascalCase` types and `camelCase` functions/properties.

Keep private helpers in subsystem `detail` namespaces and native types out of exported APIs.

Document public contracts with Doxygen comments per `readme/doxygen.md`. Fix clang-tidy diagnostics; do not
add suppression comments, raw `new`/`delete`, or C-style casts.

## Testing Guidelines

`zig build test` runs standalone C++ contract executables and Python checks. No numeric
coverage threshold is configured. Add regressions under the relevant subsystem,
using names such as `contracts.cpp` or descriptive snake_case filenames, and
register tests in `build_support/project.zig` as `stana.<feature>`.
Use `just test asan` or, on Linux/macOS, `just test asan-ubsan` for sanitizer checks.
Run window tests with `zig build test-native -Dnative-tests=true` in a graphical macOS session.
Run offscreen contracts with `-Dnative-offscreen-tests=true` on a host with a Metal device.

## Commit & Pull Request Guidelines

Prefer [Conventional Commits format](https://www.conventionalcommits.org/en/v1.0.0/).
Keep PRs focused; describe behavior changes, linked issues, and verification commands.
Include screenshots for visible UI changes and migration notes for breaking APIs.

DO NOT add any new examples.

DO NOT edit README or any other markdown file.
