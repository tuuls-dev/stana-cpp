# Stana

**A retained desktop UI toolkit for C++23.**

Describe your interface with typed components, connect it to reactive state, and let
Stana keep the mounted document in sync.

Stana combines Signals, declarative composition, explicit resource lifetimes, and incremental
layout and rendering.

[Quick start](#quick-start) · [Example](#a-reactive-counter) ·
[CMake integration](#use-stana-in-your-project) · [Development](#development)

## Status and platforms

Stana is under development at version **0.1.0**. Treat the API as evolving.
Native desktop support is currently macOS-only; portable engine support does not
mean a native window backend exists on every platform.

| Platform    | Current scope                                                                    | Platform prerequisites                                                          |
| ----------- | -------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| macOS 26+   | Native windows, input, text, and Metal rendering; portable engine and tests      | Xcode / Command Line Tools with a macOS 26+ SDK; SDK libc++                     |
| Linux       | Portable engine and headless tests; no native windows or native text measurement | GCC 14+ libstdc++ development files; Unix build tools including Make            |
| Windows x64 | Portable engine and headless tests; no native windows or native text measurement | Visual Studio C++ Build Tools, Windows SDK, and MSVC STL; use a developer shell |

Controls are drawn by Stana, not wrappers around native controls.

## What is here

- **Typed UI composition:** text, buttons, toggles, stacks,
  conditional branches, keyed collections, and custom components.
- **Fine-grained reactivity:** signals, computed values, writable bindings, and
  transactions update existing mounted occurrences.
- **Explicit lifetimes:** mounted scopes own subscriptions, cleanup, and cancellable
  coroutine tasks.
- **Retained layout and rendering:** incremental document updates, immutable scene
  projections, and presentation tracking.
- **Custom drawing:** CPU drawing and GPU canvases with WGSL shaders, compute and
  render passes, and retained GPU state. Tint is a private compiler dependency;
  consumers do not need native shader APIs.
- **Unicode-aware text model:** `stana::text::TextBuffer` provides ICU-backed grapheme
  boundaries, selection, composition, and undo history. It is not a mounted text-field
  control.
- **Headless testing:** `stana::testing::TestHost` exercises mounted UI, input, and
  deterministic task timing without opening a window.

## A reactive counter

The signal is a shared state
handle: capture it by value in callbacks, and pass the mapped value to `Text` so the
label stays reactive.

```cpp
#include <exception>
#include <iostream>
#include <string>

import stana;

using namespace stana::application;
using namespace stana::literals;
using namespace stana::ui;

auto main() -> int {
	try {
		const stana::reactive::Signal count{0};

		const auto window = Window{
			.content = VStack{
				.spacing = 16_pt,
				.layout = {.padding = stana::Insets::all(28_pt)},
				.children = children(
					Text{
						.text = count.map([](int value) -> std::string {
							return "Count: " + std::to_string(value);
						}),
						.textStyle = {.font = stana::Font::system(24_pt)}
					},
					Button{
						.text = "Increment",
						.accessibility = {.label = "Increment counter"},
						.events = {on(Event::click, [count] -> void { ++count; })}
					}
				)
			},
			.options =
				{.geometry = {.contentSize = {.width = 480_pt, .height = 320_pt}},
					.appearance = {.title = "Stana Counter"}}
		};
		return Application::run(window);
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
```

The component description mounts once. The window session owns its document and
native resources; mounted scopes own component-local resources. Native types stay
behind the adapter boundary. See the [documentation conventions](readme/doxygen.md)
for module, namespace, and ownership rules.

## Quick start

### 1. Install the build prerequisites

| Tool / dependency                                               | Required version         |
| --------------------------------------------------------------- | ------------------------ |
| Upstream LLVM: Clang, clang-tidy, clang-format, clang-scan-deps | **22.1.8**, all matching |
| ICU4C (`uc`, `i18n`, `data`)                                    | **78.3**, exact          |
| CMake                                                           | **4.4+**                 |
| Ninja                                                           | **1.11+**                |
| Python                                                          | **3.11+**                |
| Just                                                            | Optional command runner  |

Use the platform prerequisites above as well. Apple Clang is not a substitute for
the pinned upstream LLVM toolchain. Exceptions and RTTI must remain enabled.

### 2. Clone and provision dependencies

The following commands use a macOS/Linux shell and install dependencies locally,
without replacing system tools:

```sh
git clone https://github.com/tuuls-dev/stana-cpp.git
cd stana-cpp

python3 tools/bootstrap_llvm.py
export LLVM_ROOT="$PWD/.tools/llvm"
export PATH="$LLVM_ROOT/bin:$PATH"

python3 tools/bootstrap_icu.py
export ICU_ROOT="$PWD/.tools/icu"

python3 tools/bootstrap_tint.py --compiler "$LLVM_ROOT/bin/clang++"
```

The bootstrap scripts download SHA-256-pinned archives. ICU and Tint are built from
source on Unix; LLVM is also built from source on Intel macOS. Allow time and disk
space for these initial builds. Normal Stana CMake configuration does not fetch
dependencies.

If you already have matching LLVM and ICU installations, set `LLVM_ROOT` and
`ICU_ROOT` to their installation prefixes and skip those two bootstrap scripts.
Tint must still be provisioned before configuring Stana.

<details>
<summary>Windows setup (PowerShell in a Visual Studio developer shell)</summary>

Install CMake, Ninja, Python, and the platform prerequisites first, then run from
the cloned repository:

```powershell
python tools/bootstrap_llvm.py
$env:LLVM_ROOT = "$PWD/.tools/llvm"
$env:PATH = "$env:LLVM_ROOT/bin;$env:PATH"

python tools/bootstrap_icu.py
$env:ICU_ROOT = "$PWD/.tools/icu"
$env:PATH = "$env:ICU_ROOT/bin64;$env:PATH"

python tools/bootstrap_tint.py --compiler "$env:LLVM_ROOT/bin/clang-cl.exe" --config Debug
```

On Windows, Tint's configuration and CRT must match Stana's: provision with
`--config Debug` for `debug`, or `--config Release` for `release`. Use separate Tint
prefixes with `--prefix` and select them using `-DSTANA_TINT_ROOT=...` when keeping
both configurations available.

</details>

### 3. Build and test

```sh
just build
just test
```

Without Just, the equivalent commands are:

```sh
cmake --preset debug
cmake --build --preset debug --parallel 3
ctest --preset debug
```

Builds run source and namespace policy checks plus clang-tidy; configuration fails
if the required analysis or hardening features are unavailable. Each preset has
its own build directory under `build/`. Do not share module caches across presets
or toolchains. Keep personal settings in an untracked `CMakeUserPresets.json`.

### 4. Run an example on macOS

```sh
just example counter
```

Native rendering traces are enabled by default; use `just example counter --no-trace`
to disable them. Without Just, run `python3 tools/run_example.py counter`.

The [canvas example](examples/canvas/main.cpp) demonstrates CPU and GPU drawing,
WGSL compute/render passes, and extended color:

```sh
cmake --build --preset debug --target stana-canvas --parallel 3
open build/debug/stana-canvas.app
```

## Use Stana in your project

Stana builds a static library and exports the CMake target `stana::stana`.
Use the same pinned compiler and compatible standard-library ABI as the library.
Module partitions organize the implementation; consumers only use `import stana;`.

After provisioning the source checkout's dependencies, a source consumer can use:

```cmake
cmake_minimum_required(VERSION 4.4)
project(my_app LANGUAGES CXX)

add_subdirectory(path/to/stana-cpp stana-build)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE stana::stana)
set_target_properties(my_app PROPERTIES
  CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
```

Configure with Ninja, upstream Clang 22.1.8, and `CMAKE_BUILD_TYPE=Debug` or `Release`.
On macOS, also set `CMAKE_OSX_DEPLOYMENT_TARGET=26.0` or newer; the supplied
[`cmake/clang.cmake`](cmake/clang.cmake) toolchain handles the compiler and SDK setup.

An unsanitized build can also be installed:

```sh
cmake --install build/debug --prefix "$PWD/dist/stana"
```

For an installed consumer, replace `add_subdirectory(...)` with
`find_package(stana 0.1 CONFIG REQUIRED)` and add the installation prefix to
`CMAKE_PREFIX_PATH`. ICU 78.3 must remain discoverable. The installed package includes
the private Tint archive; consumer builds do not need Tint headers or its source tree.
See the [consumer fixture](tests/consumer/CMakeLists.txt) and
[integration check](tests/consumer_test.py) for source and relocated-install examples.

## Development

| Command                | Purpose                                                     |
| ---------------------- | ----------------------------------------------------------- |
| `just build`           | Configure and build Debug with mandatory analysis           |
| `just test`            | Build Debug and run CTest                                   |
| `just test release`    | Build and test Release                                      |
| `just format`          | Format first-party C++ sources                              |
| `just format-check`    | Check formatting without modifying files                    |
| `just test asan`       | Build and test with AddressSanitizer                        |
| `just test asan-ubsan` | Build and test with AddressSanitizer + UBSan on Linux/macOS |

Before using a sanitizer preset, provision a matching Tint build. For example:

```sh
python3 tools/bootstrap_tint.py --compiler "$LLVM_ROOT/bin/clang++" --sanitizer address-undefined
just test asan-ubsan
```

For `asan`, use `--sanitizer address` instead. On Windows, also use `clang-cl.exe`
and `--config Debug`. The default Tint prefixes keep sanitizer builds separate.

Native window and Metal tests are opt-in and require a graphical macOS session:

```sh
cmake --preset debug -DSTANA_NATIVE_TESTS=ON
cmake --build --preset debug --parallel 3
ctest --preset debug -L native
```

For Metal canvas offscreen tests without a window server, enable
`STANA_NATIVE_OFFSCREEN_TESTS` instead; a Metal device is still required.

### Repository map

| Path                             | Contents                                              |
| -------------------------------- | ----------------------------------------------------- |
| [`modules/`](modules/)           | Public module and subsystem interface partitions      |
| [`src/`](src/)                   | Mirrored subsystem implementations                    |
| [`src/platform/`](src/platform/) | Native adapters and platform integration              |
| [`examples/`](examples/)         | Counter and canvas applications                       |
| [`tests/`](tests/)               | Contracts, policy checks, and consumer integration    |
| [`cmake/`](cmake/)               | Toolchain, safety policy, dependencies, and packaging |
| [`tools/`](tools/)               | Bootstrap, formatting, policy, and developer scripts  |
| [`readme/`](readme/)             | Documentation conventions                             |

### Contributing and help

Read [AGENTS.md](AGENTS.md) and the [documentation guide](readme/doxygen.md) before
changing code. Follow the configured formatting, document public contracts, and add
regressions under the relevant subsystem. Fix diagnostics rather than suppressing
them. Keep pull requests focused and include verification commands; visible UI
changes should include screenshots, and breaking APIs need migration notes.

Use [GitHub issues](https://github.com/tuuls-dev/stana-cpp/issues) for bug reports and
questions. Include your OS/SDK, compiler version, preset, and a minimal reproduction.

## License

No project license file is currently included in this repository. Third-party
dependencies have their own licenses; the Tint bootstrap preserves its dependency
license notices in the provisioned prefix and installed package.
