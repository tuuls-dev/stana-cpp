# Stana

A **C++23 retained desktop UI toolkit**, consumed with **`import stana;`**.
The portable engine uses typed aggregate descriptions, tuple composition and
fine-grained signals. The initial native backend is macOS 26+ (AppKit, Core Text,
Core Graphics and Metal), with no Swift runtime dependency.

**Port in progress:** the native Counter, reactive runtime, structural components,
headless testing and Unicode editing buffer are implemented. Full upstream desktop
and renderer parity is tracked in [.todos](.todos/README.md), not claimed complete.
Symbols and Movexa are explicitly out of scope for this pass.

```cpp
#include <string>
import stana;

using namespace stana;

auto main() -> int {
    const Signal count{0};
    return Application::run(Window{
        .title = "Counter",
        .size = {.width = 480, .height = 320},
        .content = VStack{
            .spacing = 16,
            .layout = {.padding = Insets::all(28)},
            .children = children(
                Text{.text = count.map([](int value) -> std::string {
                    return "Count: " + std::to_string(value);
                })},
                Button{.text = "Increment", .onClick = [count] -> void { ++count; }}
            )
        }
    });
}
```

With [Just](https://just.systems/) installed:

```sh
just format
just example counter
```

`just example counter` configures and builds Debug before launching the native
window. `just build`, `just test`, and `just format-check` are also available.
Run `just` to list recipes. Direct CMake commands remain supported; after building,
`open build/debug/stana-counter.app` launches the same macOS example. Public modules
include `stana.primitives`, `stana.reactive`, `stana.graphics`, `stana.text`,
`stana.ui`, `stana.application` and the separately imported `stana.testing`.
There is no public header API, header-unit or `import std` requirement.

See [architecture](docs/architecture/README.md) and
[naming/documentation conventions](docs/coding-conventions.md). Exported types,
properties and runtime contracts use Doxygen comments for IDE hover documentation.

## Prerequisites

- Upstream **LLVM 22.1.8**, including `clang++`/`clang-cl`, `clang-tidy`,
  `clang-format`, and `clang-scan-deps`, all from the same distribution.
- **ICU4C 78.3**, including `uc`, `i18n` and `data` development libraries.
  Set `ICU_ROOT` to a matching installation; macOS detects Homebrew `icu4c@78`.
  Alternatively run `python3 tools/bootstrap_icu.py` and set `ICU_ROOT="$PWD/.tools/icu"`.
  The bootstrap verifies the official archive SHA-256 before building/unpacking it.
- **CMake 4.4+**, **Ninja 1.11+**, and **Python 3.11+** for build policy/tests.
- macOS: Xcode command-line tools/SDK; system libc++, macOS 26+ deployment target.
- Linux: glibc-based Linux, GCC 14 development headers/runtime (`libstdc++`);
  official binary packages target Ubuntu 24.04 x64 (glibc 2.39).
- Windows: Visual Studio 2022 build tools with the C++ workload, Windows SDK,
  MSVC STL with `_MSVC_STL_HARDENING` support, and a **Developer PowerShell** or
  **x64 Native Tools Command Prompt**. The compiler is **clang-cl**, not MSVC.
  Runtime baseline: Windows 11 x64 and the matching Visual C++ redistributable.

Use the installer script for a project-local LLVM installation. Downloads are
SHA-256 pinned to the [official LLVM 22.1.8 release](https://github.com/llvm/llvm-project/releases/tag/llvmorg-22.1.8):

```sh
python3 tools/bootstrap_llvm.py
export LLVM_ROOT="$PWD/.tools/llvm"
```

On Windows use `python` and `$env:LLVM_ROOT = "$PWD/.tools/llvm"` in Developer
PowerShell. LLVM_ROOT selects tools without changing or replacing system compilers.
On Intel macOS the official release has no binary: the script builds the pinned
LLVM source, tools and sanitizer runtimes (the first run takes considerably
longer). CI caches this toolchain.

Alternatively, on Apple Silicon, `brew install llvm@22 cmake ninja` supplies the
matching tools while that formula remains at 22.1.8. The presets detect the
Homebrew LLVM path automatically; `/usr/bin/clang++` is deliberately not supported.
Keep CMake/Ninja/Python up to date, for example `python3 -m pip install 'cmake>=4.4' ninja`.

## Build and test

```sh
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug
cmake --build build/debug --target format-check

cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
```

The `asan` preset works on all three platforms. `asan-ubsan` combines AddressSanitizer
and UndefinedBehaviorSanitizer on Linux/macOS. Use the corresponding configure,
build and test preset names. Sanitized builds cannot be installed or packaged.
Each preset owns its build directory and BMIs; never share BMI caches across
configurations, toolchains, architectures or sanitizer modes.

Code indentation uses **tabs** (width 4), enforced by clang-format and described
by `.editorconfig`. YAML uses spaces because YAML forbids tab indentation.

### Native verification

Headless tests never create `NSApplication`. A real window-server/Metal smoke
binary is built on macOS as `build/debug/tests/stana-native-smoke`. Run it in a
logged-in graphical session, or configure `-DSTANA_NATIVE_TESTS=ON` to include it
in CTest. A sandbox may need explicit access to the window server and Metal device.
Missing Metal is an error, not a silent CPU-renderer fallback.

### VS Code

Workspace settings connect the C/C++ extension to **CMake Tools**, set the
IntelliSense fallback to **C++23**, and recognize `.cppm` files as C++ with tabs.
Use **CMake: Select Configure Preset** to choose `debug` (or another supplied
preset), then **CMake: Configure**. CMake supplies the selected compiler, standard,
includes and definitions instead of maintaining a separate editor-only build
configuration. If the folder was already open before these settings were added,
run **Developer: Reload Window**, then configure the preset.

## Consume from source

Configure the parent with the same Clang toolchain, C++23 and Ninja.

On macOS, configure the parent with `-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0` (or newer).
This must be set for source and installed consumers; an unset or older cached target
is rejected. Installed consumers must meet the package's recorded deployment target.

```cmake
cmake_minimum_required(VERSION 4.4)
project(my_app LANGUAGES CXX)
add_subdirectory(path/to/stana)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE stana::stana)
set_target_properties(my_app PROPERTIES CXX_STANDARD 23 CXX_EXTENSIONS NO)
```

Source builds require Stana's analysis tools and Python; warnings and analysis
apply to Stana targets, not arbitrary parent-project sources. Examples and tests
are enabled only when Stana is the top-level project.

## Consume a release package

Download the package matching your **OS, architecture, configuration, standard
library and runtime ABI**, verify its SHA-256, and extract it to a prefix:

```cmake
cmake_minimum_required(VERSION 4.4)
project(my_app LANGUAGES CXX)
find_package(stana 0.1 CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE stana::stana)
set_target_properties(my_app PROPERTIES CXX_STANDARD 23 CXX_EXTENSIONS NO)
```

Configure with `-DCMAKE_PREFIX_PATH=/path/to/extracted/package`, the same Clang
version, and the package's Debug/Release configuration. Packages contain the
static archive, module interface source and CMake module metadata. **CMake builds
the BMI locally**; `.pcm` files are not shipped as portable artifacts. Consumers
still need the compiler, SDK and module scanner, but not clang-tidy/Python simply
to use a prebuilt package.

Read `share/stana/build-info.json` for the exact build environment. C++23 is a
minimum propagated requirement; module consumers must actually select C++23,
not a different language mode. Keep exceptions/RTTI enabled. Windows uses `/MD`
in Release and `/MDd` in Debug with the corresponding iterator ABI. Do not mix
Debug and Release packages. Linux uses libstdc++, not libc++; macOS uses system
libc++. Static Stana does not mean a statically linked C++ runtime. Cross-platform
means rebuilding the application per target, not one executable for every OS.

## Ruthless source policy

- Warnings are errors, extensions are errors, and clang-tidy runs during normal
  compilation of interfaces, implementations and tests with the actual module maps.
- Analyzer, bug-prone, CERT, concurrency, Core Guidelines, modernization,
  performance and portability diagnostics are mandatory. LLVM 22 custom AST
  checks close unconditional raw-allocation, C-cast and unsafe-buffer-API gaps.
- No explicit `new`/`delete`, C allocation, C arrays, pointer arithmetic,
  `const_cast`/`reinterpret_cast`/C casts, unsafe downcasts, union type-punning,
  C varargs or legacy buffer APIs. Prefer RAII factories, containers, ranges,
  checked access, checked conversions, `std::format` and `std::expected`.
- No `NOLINT`, diagnostic-suppression pragmas or sanitizer-suppression attributes
  in first-party code. There are no warning/analysis-disable options or per-file
  exemptions. The source-policy scan runs even on an otherwise up-to-date build.
- Production builds retain stack protection and standard-library hardening:
  libc++ extensive mode, libstdc++ assertions, or MSVC STL hardening. Exceptions
  and RTTI remain enabled. Standard-library internals are not linted as Stana code.

These checks enforce specific rules; neither modules nor static analysis prove
arbitrary C++ memory-safe. The repository is not a sandbox against someone
deliberately changing the build policy itself.

## Releases

The GitHub Actions workflow validates all four targets in Debug, Release and
sanitizer configurations. Push a tag matching `v<project version>` (initially
`v0.1.0`) to publish only after the full matrix passes. Publishing privileges
exist only in the final release job; PR jobs are read-only.

Assets include separate Debug/Release packages, explicit source archives and
`SHA256SUMS`. Packages contain module **sources**, not portable compiled BMIs.
To package a local nonsanitized build:

```sh
python3 tools/release.py package --build build/release --output dist
```

No license has been selected yet; choose one before distributing this project
under open-source terms.
