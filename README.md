# Stana

A **C++23 retained desktop UI toolkit**, consumed with **`import stana;`**.
The portable engine uses typed aggregate descriptions, tuple composition and
fine-grained signals. The initial native backend is macOS 26+ (AppKit, Core Text,
Core Graphics and Metal), with capability-selected unified or discrete resource
profiles and no Swift runtime dependency. Native qualification currently covers
Apple M1 Pro; Intel/AMD output remains unqualified.

**Port in progress:** the native Counter, reactive runtime, structural components,
headless testing and Unicode editing buffer are implemented. Full upstream desktop
and renderer parity is tracked in [the task backlog](.tasks/00_omnibus.md), not claimed complete.
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
window, with native rendering traces on by default (`just example counter --no-trace`
disables them). `just build`, `just test`, and `just format-check` are also available.
Run `just` to list recipes. Direct CMake commands remain supported; after building,
`open build/debug/stana-counter.app` launches the same macOS example. Public modules
include `stana.primitives`, `stana.reactive`, `stana.graphics`, `stana.text`,
`stana.ui`, `stana.application` and the separately imported `stana.testing`.
There is no public header API, header-unit or `import std` requirement.

Background work submits results with `Signal<T>::setThreadSafe(T)`. The call is
always deferred onto the signal's creating executor, including when invoked on
that owner thread. Returning means the owning value was admitted to a bounded
mailbox (4096 pending writes per executor); it is not application, observer
delivery, or display completion. `set()` stays synchronous and owner-thread-only.
`get()`, computed evaluation, and graph traversal stay owner-thread-only.

Queued admission requires an active native `Application::run()` session or a live
`TestHost` on the owner. Signals created before that session are adopted when it
starts; dormant or closed sessions throw `std::logic_error` rather than stacking
work. A full mailbox throws `std::overflow_error` without blocking, dropping, or
replacing earlier commands. Commands hold the payload and a weak signal reference:
destroying the last handle discards pending writes without resurrecting UI. After a
session closes, existing signals still support same-thread `get()`/`set()`, but
queued admission stays closed for that epoch. Freshness is not FIFO completion;
generation checks remain `Scope::isCurrent(token)` on the owner after a callback,
not a property of `setThreadSafe`.

```cpp
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
import stana;

using namespace stana;

auto main() -> int {
	const Signal result{std::string{"Ready"}};
	std::vector<std::jthread> workers;
	return Application::run(Window{
		.title = "Worker result",
		.size = {.width = 480, .height = 320},
		.content = VStack{
			.children = children(
				Text{.text = result},
				Button{
					.text = "Run",
					.onClick = [result, &workers] -> void {
						result.set("Working...");
						workers.emplace_back([result] -> void {
							try {
								result.setThreadSafe(std::string{"Done"});
							} catch (const std::exception &error) {
								static_cast<void>(error);
							}
						});
					}
				}
			)
		}
	});
}
```

Keep workers owned by the application for the duration of `run()`. Do not join a
local thread inside the click handler. `std::jthread` is shown for ownership;
some libc++ named-module builds still need `std::async(std::launch::async, ...)`
instead.

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

## Incremental rendering

**Qualification in progress:** see [measured evidence](docs/rendering-evidence.md).
CoreGraphics tile clipping can produce bounded antialiasing differences; the
approved reference gate keeps alpha/uniform regions exact and limits edge-only
RGB error and affected pixel count. Incremental stale-pixel checks remain exact.

Retained mutations classify measurement, placement, paint, and inspection work.
Complete immutable scene branches and an immutable geometry index share unchanged
payloads across revisions. Public `TestHost::scene()`/`nodes()` still work; flat
inspection views are built lazily and are not part of native frame conversion.
`Metrics::layoutPasses` counts commits that actually compute measurement or placement;
paint-only commits do not increase it. The additive per-operation counters distinguish
local work, ancestor path copies, and explicit flat materialization.

On supported Macs, CoreGraphics/Core Text rasterize on the UI executor directly
into reusable shared Metal buffers. Physical 512×512 tiles (smaller at edges) own
persistent Metal layers. Only dirty tiles are cleared/replayed and copied into
fresh, completely initialized drawables; unchanged layers are untouched. There is
no intermediate scene texture or bitmap upload in the CPU-only unified path.
Mixed CPU/GPU scenes use retained offscreen composition, not an old renderer fallback.
There **is** a tile-sized GPU buffer-to-drawable copy and normal
Core Animation compositor traffic; this is not a claim of zero total copies.

One active composite per window joins GPU success for every copied tile, an issued
Core Animation batch transaction, and a positive native timestamp from the first
submitted tile. Other tile timestamps are diagnostic, not per-tile display proof. Changed interaction regions remain gated until that join;
paint-equivalent revisions use a real tile-zero evidence anchor. Recovery, drawable
retry, and GPU drain have bounded deadlines. Preparation and drawable acquisition
remain on the UI executor; `nextDrawable` is non-blocking and a nil drawable retries on the next frame.
A transaction groups tiles but does not prove per-tile display or scanout-atomic presentation.

`Application::renderingDiagnostics().renderingWork` reports actual raster/copy work,
shared-buffer allocations, and separate estimated drawable payload. The 256 MiB
per-window shared-raster budget includes retired GPU owners, **not** Core Text,
driver allocations, compositor surfaces, or total process memory. Many tile layers
have measurable overhead. Native support requires macOS 26+ and compatible Metal
formats/usages; unsupported capabilities fail explicitly. Discrete CPU writes are
notified and accounted, rather than described as zero-upload.

## CPU and GPU canvases

`Canvas` is now **`CPUCanvas`** (no alias). Both canvases accept reactive drawings,
use padded local coordinates, and clip by default; `.appearance = {.clip = false}`
opts out of the outer clip. GPU output still has a finite content extent.

[`examples/canvas/main.cpp`](examples/canvas/main.cpp) is a complete reactive CPU
chart plus portable WGSL compute → render/state, mesh and round-line demo. Its
application-owned shader is embedded at build time, not read from the working
directory. Run `build/debug/stana-canvas.app/Contents/MacOS/stana-canvas` on macOS;
pass `--validate` to validate the complete shader graph without a window/device.
Other hosts build/validate the same source but have no native window adapter.

See [canvas contracts and migration](docs/canvas/README.md) for colors, resources,
state, errors, limits and compiler provisioning; [implementation evidence](docs/canvas/implementation-evidence.md)
distinguishes verified gates from pending qualification.

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
binary is built on macOS as `build/debug/tests/stana-native-smoke.app`. Run it in a
logged-in graphical session, or configure `-DSTANA_NATIVE_TESTS=ON` to include it
in CTest. A sandbox may need explicit access to the window server and Metal device.
Unsupported native hardware is an error, not a silent CPU-renderer fallback.
Default Apple tests include CPU-only full/tile byte parity without `NSApplication`;
`-DSTANA_NATIVE_TESTS=ON` additionally registers shared-buffer GPU readback and real
tile-window evidence gates. See [rendering evidence](docs/rendering-evidence.md).

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
