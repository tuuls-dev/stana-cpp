# Stana

**A retained desktop UI toolkit for C++23.**

Describe your interface with typed components, connect it to reactive state, and let
Stana keep the mounted document in sync.

Stana combines Signals, declarative composition, explicit resource lifetimes, and incremental
layout and rendering.

[Quick start](#quick-start) · [Example](#a-reactive-counter) ·
[Zig integration](#use-stana-in-your-project) · [Development](#development)

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
| Zig                                                             | **0.17.1**              |
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

The bootstrap scripts download SHA-256-pinned archives. The Tint bootstrap and the
LLVM source bootstrap on Intel macOS require CMake and Ninja to build their upstream
projects. ICU builds from source on Unix with Make. Once the dependencies are
provisioned, Stana builds and tests offline with Zig.

Set `LLVM_ROOT`, `ICU_ROOT`, and `STANA_TINT_ROOT` to use matching provisioned
dependencies. The defaults use the repository's `.tools/` directory and look
for matching LLVM and ICU installations from Homebrew on macOS.

<details>
<summary>Windows setup (PowerShell in a Visual Studio developer shell)</summary>

Install Zig, Python, and the platform prerequisites. The Tint bootstrap also
requires CMake and Ninja. Run these commands from the cloned repository.

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
prefixes with `--prefix` and select them using the `STANA_TINT_ROOT` environment variable when keeping
both configurations available.

</details>

### 3. Build and test

```sh
zig build -j3
zig build test -j3
```

`just build` and `just test` wrap these commands. Builds run source policy and
clang-tidy by default. Use `zig build test --release=fast -j3` for Release.
Zig caches objects and module interfaces by toolchain and configuration. Run
`zig build compdb -j3` to generate `compile_commands.json` for clangd.

### 4. Run an example on macOS

```sh
zig build run-counter -j3
zig build run-symbols -j3
zig build run-canvas -j3
```

`just example counter` is a shortcut for launching the counter. Native rendering
traces are enabled by default. Set `-Dtrace=false` or use
`just example counter --no-trace` to disable them.

The [canvas example](examples/canvas/main.cpp) demonstrates CPU and GPU drawing,
WGSL compute and render passes, and extended color. Use `zig build examples` to
build all three apps into `zig-out/bin`.

## Use Stana in your project

Stana provides a static C++23 library and the public `import stana;` module.
Use the pinned Clang version and a compatible platform C++ standard library.

For a source build, provision Stana's dependencies as described above. For an
installed SDK, stage the library with `zig build --prefix "$PWD/dist/stana"`.
Add Stana as a path dependency in your project's `build.zig.zon`. Point the
`path` at either the source checkout or the installed SDK root.

```zig
.dependencies = .{
    .stana = .{ .path = "../stana-cpp" },
},
```

In your project's `build.zig`, import Stana's build helper and pass it your C++
sources.

```zig
const std = @import("std");
const stana = @import("stana");

pub fn build(b: *std.Build) void {
    const optimize = b.standardOptimizeOption(.{});
    const dependency = b.dependency("stana", .{ .optimize = optimize });
    const app = stana.addCppExecutable(b, dependency, .{
        .name = "my-app",
        .sources = &.{b.path("main.cpp")},
    });
    b.getInstallStep().dependOn(&b.addInstallFileWithDir(app, .bin,
        if (@import("builtin").os.tag == .windows) "my-app.exe" else "my-app").step);
}
```

Match the installed SDK's Debug or Release configuration. On macOS the default
deployment target is 26.0. Pass a higher target as the
`.@"macos-deployment-target"` dependency option when required by an SDK.
Installed consumers need LLVM and ICU. The SDK includes the private Tint archive
and rebuilds the compiler-specific module interfaces in the consumer's cache.
See the [consumer contract](tests/consumer/build.zig) and
[relocation test](tests/consumer_test.py).

## Development

| Command                | Purpose                                                     |
| ---------------------- | ----------------------------------------------------------- |
| `just build`           | Build Debug with mandatory analysis                          |
| `just test`            | Build and run the Debug contracts                            |
| `just test release`    | Build and run the Release contracts                          |
| `just format`          | Format first-party C++ and Objective-C++                     |
| `just format-check`    | Check formatting                                             |
| `just test asan`       | Build and test with AddressSanitizer                          |
| `just test asan-ubsan` | Build and test with AddressSanitizer and UBSan on Linux/macOS |
| `zig build package`    | Test and package a relocatable SDK into `zig-out/dist`       |

Provision a matching Tint build before enabling a sanitizer.

```sh
python3 tools/bootstrap_tint.py --compiler "$LLVM_ROOT/bin/clang++" --sanitizer address-undefined
zig build test -Dsanitizer=address-undefined -j3
```

For AddressSanitizer alone, select `address` in both commands. On Windows,
bootstrap with `clang-cl.exe` and the matching `--config`. The default Tint
prefixes keep sanitizer builds separate.

Run the window and Metal contracts from a graphical macOS session.

```sh
zig build test-native -Dnative-tests=true -j3
```

For offscreen Metal tests, run `zig build test-native -Dnative-offscreen-tests=true -j3`
on a host with a Metal device. Use the same options with `zig build test` to
run those contracts alongside the headless suite.

### Repository map

| Path                             | Contents                                              |
| -------------------------------- | ----------------------------------------------------- |
| [`modules/`](modules/)           | Public module and subsystem interface partitions      |
| [`src/`](src/)                   | Mirrored subsystem implementations                    |
| [`src/platform/`](src/platform/) | Native adapters and platform integration              |
| [`examples/`](examples/)         | Counter, symbols, and canvas applications                       |
| [`tests/`](tests/)               | Contracts, policy checks, and consumer integration    |
| [`build_support/`](build_support/) | Zig compile graph, contracts, and packaging |
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
