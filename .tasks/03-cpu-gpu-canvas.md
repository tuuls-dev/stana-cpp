---
status: in-progress
plan: final
researched: 2026-11-04
---
# CPUCanvas and GPUCanvas — complete implementation specification

This is the final, decision-complete plan for renaming `Canvas` to `CPUCanvas`
and implementing the behavior of upstream `STNCanvas` and `STNGPUCanvas` in
modern C++23. It specifies target contracts, not capabilities already implemented.
Implement the public values, reactive mounting, ordered composition, resource
validation, execution, failure handling, and verification together.

**Execution status:** implementation in progress on `feat/03-cpu-gpu-canvas`.
See [implementation evidence](../docs/canvas/implementation-evidence.md) for verified
checkpoints and outstanding qualification. Unchecked gates are not complete.
There are no product decisions awaiting user clarification.

Reading order: scope and evidence (§1–2), public API and validated values (§3–6),
retained/native integration (§7–10), execution and color (§11–12), then file map,
milestones, verification, and sources (§13–16).

## 1. Intent, scope, and committed decisions

### 1.1 Required outcome

- `CPUCanvas` replaces the public `Canvas` name. It accepts constant, signal, and
  computed `Drawing` values, supports shared properties including interaction,
  and updates paint without remounting or unnecessary layout.
- `GPUCanvas` mirrors the upstream GPU component: ordered shape drawing, colored
  indexed meshes, round polylines, trusted application render/compute programs,
  immutable resources, temporal texture state, diagnostics, and extended color.
- Ordinary UI, CPU canvases, and GPU canvases compose in retained painter order.
  GPU content is not an always-on-top native child view or independent swapchain.
- The engine, authoring API, descriptors, validation, and headless tests remain
  portable across the repository's macOS, Windows, and Linux build matrix.
- The first executable native adapter remains macOS 26+ using Objective-C++ and
  Metal. Preserve Intel/AMD-capable macOS as well as Apple Silicon; do not narrow
  the existing build matrix to unified-memory Apple GPUs.
- Use named modules, C++23, bounded immutable ownership, checked arithmetic,
  strong validation boundaries, and the repository's existing safety policy.

### 1.2 Explicit platform meaning

**The entire public Stana interface must be platform-neutral.** This includes
exported module declarations, types, properties, enums, factories, shader inputs,
binding coordinates, configuration, and consumer examples—not merely avoiding
native object pointers. No backend names, native shader payloads, native register
indices, or platform-selected overloads are exposed, including through transitively
exported `stana.graphics` declarations. There is no public native escape hatch.

Application programs use one owned WGSL source contract:
`GPUShader::source(std::string)`. Logical resource bindings use WGSL group/binding
coordinates. Stana privately validates/translates that source with Tint and the
macOS adapter privately compiles the resulting MSL. No public `metalSource`,
`metalLibrary`, MSL, metallib, HLSL, DXIL, SPIR-V, native compiler options, or
backend-selection API exists. Future adapters consume the same public WGSL and
logical descriptors; applications do not supply per-platform shader variants.
The [WGSL specification](https://www.w3.org/TR/WGSL/) and
[Dawn architecture](https://github.com/google/dawn/blob/main/docs/dawn/overview.md)
provide the language and translation foundation; this does not introduce a
browser, JavaScript runtime, WebGPU public API, or Dawn graphics runtime into Stana.

Windows/Linux currently support the portable engine and deterministic headless
host, not native application windows. Preserve that contract. Implementing Win32,
Wayland/X11, D3D12, or Vulkan hosts is not an implicit part of this canvas task.
Portable shader validation/translation **is** included. Do not create empty backend
directories. Unsupported native execution returns a capability/unsupported-platform
failure, never fake pixels or silent CPU fallback. A portable shader interface
does not imply that all native hosts have already been implemented.
[Vulkan synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html) and
[D3D12 resource barriers](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)
support keeping read/write dependencies explicit rather than encoding Metal's
implicit ordering into the portable model.

### 1.3 Decisions that are not implementation options

| Concern | Decision |
| --- | --- |
| Rename | Hard rename to `CPUCanvas`; no `using Canvas = CPUCanvas`, deprecated alias, or duplicate component. |
| CPU builder | Keep `Drawing`; add upstream spellings `fillRect`/`strokeRect`, retaining current `fill`/`stroke` as forwarding conveniences. |
| CPU equality | Structural command-value equality, matching `STNDrawing`. |
| GPU equality | Immutable version identity for drawings, meshes, buffers, textures, programs; copies preserve identity. |
| Canvas geometry | Local top-left logical coordinates relative to the direction-resolved padded content origin. |
| Default clipping | Both canvases clip to their rounded outer bounds by default; explicit `.clip = false` overrides it. |
| CPU execution | CoreGraphics/Core Text on the UI executor, preserving task 01; no raster-worker project. |
| GPU execution | Offscreen textures followed by ordered composition into complete dirty final tiles. |
| Public interface | Platform-neutral throughout; no native names, payload variants, slot conventions, or escape hatches. |
| Custom shaders | Owned WGSL source, logical bindings, private Tint translation and private native compilation; no native shader ingress. |
| Native API | Classic `MTLCommandQueue`/`MTLCommandBuffer`, directly bound tracked resources; not Metal 4 queues. |
| Compilation | One bounded serial compiler queue per device; never compile application shaders synchronously in UI paint. |
| Animation | Drawing/resource changes request work. No implicit time uniform, display-link loop, or state step on presentation retry. |
| Error replacement | Keep the previous valid output on compile/binding/capability replacement failure; initially unavailable output is transparent. |
| GPU execution failure | Invalidate the renderer epoch, retain in-flight owners, bounded recovery, then explicit terminal failure. |
| Extended color | Scene-linear extended sRGB, half-float composition when demanded; one final display mapping. |
| Tests | Portable production-contract tests plus mandatory native offscreen and real-presentation gates, separately reported. |

### 1.4 Non-goals

Do not port unrelated `STNLayer`, `STNBackdrop`, symbols, image components, frame
callbacks, window capture APIs, arbitrary vector paths, glyph atlases, custom font
engines, or an entire cross-platform graphics library. They are not properties
of either canvas. Keep `GPUProgram::samplingRadius` metadata for parity, but reject
backdrop-only reserved bindings in canvas programs until a separate backdrop
component supplies those inputs. Do not add fake backdrop textures.

GPU output conformance is a trusted-shader contract, not a claim of shader
sandboxing, termination enforcement, or complete static bounds proof. Native
reflection and validation improve safety but cannot prove arbitrary shader code.

## 2. Evidence and exact starting point

### 2.1 Repository baselines

Planning baseline: `stana-cpp` commit
`df6b6ae` (clean working tree before this plan). Upstream reference:
`/Users/alloysmila/Github/tuuls-dev/stana-ui`, commit
`a31555c779c3bfd4f8222606996e4a5d6eadb919`.
Read upstream source locally; its APIs were not discoverable by public web search.
The implementation must not introduce a runtime or build dependency on that Swift
checkout. The commit and repository-relative paths below identify the reference
independently of the local checkout location.

| Upstream source, relative to `stana-ui` | Required reference behavior |
| --- | --- |
| `Sources/Stana/Components/Drawing/STNCanvas.swift` | Structural CPU drawing, reactive property installation, default clipping, all shared groups. |
| `Sources/Stana/Components/Drawing/STNGPUCanvas.swift` | Reactive GPU drawing, retained error handler, shared properties, paint-only updates. |
| `Sources/Stana/Engine/Primitives/Rendering/GPUDrawing.swift` | Extended colors, immutable meshes, five command kinds, version equality, extended demand. |
| `Sources/Stana/Engine/Primitives/Rendering/GPUProgram.swift` | Resource/pass descriptors, validation, stable dependency order, resource replacement, state identity. |
| `Sources/Stana/Engine/Primitives/Appearance/Color.swift` | Encoded SDR and semantic color resolution. |
| `Sources/Stana/Engine/Scene/SceneBuilder.swift` | Padded content coordinates and ordered CPU/GPU leaves, full/patched equivalence. |
| `Sources/Platform/MacOS/Rendering/MetalPrograms.swift` | Reflection on cache hits, execution order, fallback, previous/next state. |
| `Sources/Platform/MacOS/Rendering/MetalScene.swift` | Mixed runs, shape/polyline/mesh rendering, offscreen outputs. |
| `Sources/Platform/MacOS/Rendering/Shaders/Stana.metal` | Premultiplication, transfer functions, shape coverage, final tone mapping. |
| `Sources/Stana/Documentation.docc/HybridRendering.md` | Consumer contracts, limitations, color and state semantics. |
| `Tests/Engine/Graphics/ProgramContractTests.swift` | Versions, graph cycles, state history, overflow regressions. |
| `Tests/Engine/Scene/DirectionalContentTests.swift` | LTR/RTL padded CPU/GPU origin agreement. |
| `Tests/Platform/MacOS/Rendering/NativeMetalTests.swift` | Mixed order, HDR readback, 1,000 temporal steps, failed resource replacement. |
| `Fixtures/API/Valid/HybridRendering.swift` | Public consumer shape, mesh, reactive and program use. |

### 2.2 Existing C++ responsibilities

| Existing file | Actual behavior now | Work required |
| --- | --- | --- |
| `modules/graphics/graphics.cppm` | `Drawing` publicly stores `PaintCommand` values; only `fill`/`stroke` helpers. Flat `SceneSnapshot`. | Validated CPU equality/helpers; GPU value interfaces; typed ordered projection. |
| `modules/ui/ui.cppm` | `Canvas::drawing` is a plain `Drawing`; no interaction group. `AppearanceProperties::clip` is a non-optional false boolean. | Rename, reactive input, interaction, explicit clip/default distinction, GPU component. |
| `src/ui/mount.cpp` | Copies drawing once into `NodeKind::canvas`. | Scoped binding and callback installation for both components. |
| `modules/core/core.cppm` | One canvas node kind and mutable drawing; general `bind` broadly invalidates. | Two node kinds, immutable GPU storage, diagnostics routing; task-01 classified paint invalidation. |
| `src/layout/layout.cpp` | Canvas intrinsic size comes from no children; explicit frame required. | Preserve sizing; common content rectangle, no geometry-derived intrinsic size. |
| `src/scene/scene.cpp` | Translates CPU commands by outer node origin; optional outer clip; flat paint. | Padded origin, default clip, GPU leaves, persistent ordered branches. |
| `src/application/application.cpp` | Deep-copies flat commands to module-free native transport. | Owning typed CPU/GPU transport and exact diagnostic identities. |
| `src/platform/native_contracts.hpp` | No GPU descriptors, mixed scene entries, or canvas diagnostics. | Shared immutable module-free payloads, explicit conversion, no borrowed handles. |
| `src/platform/macos/native_host.mm` | Full-window CPU bitmap, Metal texture upload and blit. | Use task-01 renderer/presenter extraction; add hybrid offscreen executor/compositor. |
| `src/platform/unsupported.cpp` | Unavailable native services fail explicitly. | Preserve behavior; no native calls from validation/headless mounting. |
| `modules/testing/testing.cppm` | Production document, snapshots, input, synthetic presentation evidence. | Deliberate typed canvas inspections and injected diagnostics, not a second renderer. |
| `CMakeLists.txt`, `tests/CMakeLists.txt` | C++23 modules, strict safety, Apple-only frameworks, portable CI. | Register partitions/sources/tests and package built-in shader resources. |

### 2.3 Relationship to existing tasks

Use [task 00](00_omnibus.md) for ownership, public API, shared properties, input,
and verification style; [task 01](01-incremental-shared-rendering.md) for immutable
projections, damage, tile ownership, and joined presentation evidence; and
[task 02](02-thread-safe-signal-writes.md) for executor/lifetime boundaries.
All are specifications, not already-implemented dependencies.

Task 01 is the rendering foundation for final completion here. Public rename and
value/validation work can land first. Implement its required foundations before
the hybrid native milestones; do not temporarily ship a second flat-window GPU
renderer that later needs replacement. Task 02 is not a prerequisite: ordinary
property writes remain owner-thread-only; immutable GPU values may be built on
workers and delivered by already-supported UI posting.

This task deliberately supersedes these narrow task-01 decisions:

1. Its prohibition on shader rendering applies to ordinary UI/CPU primitives,
   not the new GPU canvas leaf or required texture compositing/display mapping.
2. Its Apple-family-only native restriction becomes a capability-selected shared
   or managed/staging path, retaining Intel/AMD correctness and explicit copy
   accounting. Zero-upload targets remain specific to supported unified profiles.
3. Its four-byte final-tile assumption remains valid for SDR, not half-float final
   composition. CPU bitmap tiles stay BGRA8; composition allocation is format-aware.
4. Its canvas overflow rules describe the actual active clip. Default canvas clips
   now exist; explicit unclipped CPU drawings still require actual-ink damage.

Implementers must update those sections and their affected tests in the same
implementation change, retaining the original CPU-only performance gates. This
plan does not mark task 01 complete or silently change its unrelated scheduling,
input, recovery, or layout contracts.

The user's public-interface constraint takes precedence over upstream's Metal
shader escape hatch and the earlier revision of this plan. Replace it rather than
retaining it in an optional public extension module. Native implementation details
may appear in this implementation specification, never in the consumer contract.

### 2.4 Deliberate adaptations, not accidental omissions

This is behavioral parity with the two components and their GPU value/program
dependencies, not a line-for-line Swift port. C++ factory failures use `expected`
instead of Swift throwing initializers; builder programmer errors use diagnosed
exceptions instead of trapping preconditions. Descriptor validation is stricter
about missing transient producers, binding aliases, and aggregate resource limits.
CPU raster execution follows C++ task 01, not upstream's newer worker architecture.
The existing extra CPU command-stream operations remain supported. Color palette
defaults retain C++'s established accent. Native window capture, isolated layers,
backdrops, and frame callbacks are independent features, not silently omitted
GPUCanvas commands. These differences must be stated in implementation docs.
Shader authoring deliberately differs from upstream: WGSL replaces Metal-specific
source/library inputs, logical bindings replace native slots, and compute dispatch
uses explicit workgroup counts. Preserve render/compute/state capabilities through
private lowering; do not weaken the public portability requirement to copy Swift
signatures verbatim.

## 3. Public authoring and rename contract

### 3.1 Canonical component declarations

These are the chosen declaration shapes; add includes, module prologues, Doxygen,
and implementation details during implementation. Field order is normative for
C++ designated-initializer examples.

```cpp
struct CPUCanvas {
	Property<Drawing> drawing{};
	LayoutProperties layout{};
	AppearanceProperties appearance{};
	InteractionProperties interaction{};
	AccessibilityProperties accessibility{};
	LifecycleProperties lifecycle{};
};

struct GPUCanvas {
	Property<GPUDrawing> drawing{};
	LayoutProperties layout{};
	AppearanceProperties appearance{};
	InteractionProperties interaction{};
	AccessibilityProperties accessibility{};
	LifecycleProperties lifecycle{};
	std::function<void(const GPUDiagnostic &)> onError{};
};
```

Provide the normal `mount(const CPUCanvas &, MountContext &)` and GPU overload.
Both remain aggregates. No imperative `draw(context)` callback, native texture
parameter, raw pointer escape hatch, or per-canvas window is added.

`Property` already accepts values, `Signal<T>`, and `Computed<T>`. Use that path
instead of adding an independent observer system. The default drawing is empty.
An empty drawing is a valid transparent content contribution; background/border
and accessibility metadata still behave normally.

### 3.2 Example consumers

```cpp
#include <string>
import stana;
using namespace stana;

Drawing drawing;
drawing.fillRect({.size = {.width = 120, .height = 60}}, Color::accent(), 8);
const Signal cpuDrawing{drawing};
CPUCanvas{
	.drawing = cpuDrawing,
	.layout = {.frame = {.width = Length::points(160), .height = Length::points(90)}}
};

GPUDrawing gpuDrawing;
gpuDrawing.fillEllipse(
	{.origin = {.x = 12, .y = 12}, .size = {.width = 48, .height = 48}},
	ExtendedColor{0.2, 2.0, 0.8}
);
const Signal gpuModel{gpuDrawing};
GPUCanvas{
	.drawing = gpuModel,
	.layout = {.frame = {.width = Length::points(160), .height = Length::points(90)}},
	.onError = [](const GPUDiagnostic &diagnostic) -> void {
		// Application-owned logging; not a Stana reporting service.
	}
};
```

Compile complete examples during implementation; do not preserve unused-parameter
warnings in production examples. Provide accessible labels/equivalents in the
complete chart demonstration. Drawing geometry does not create semantic children.

### 3.3 Rename and migration inventory

- Rename the public aggregate, mount declaration/definition, Doxygen references,
  consumer fixtures, examples, and documentation. `Drawing` stays named `Drawing`.
- Change internal `NodeKind::canvas` to `cpuCanvas`; add `gpuCanvas`. Diagnostic
  `NodeSnapshot::kind` values become `"cpuCanvas"` and `"gpuCanvas"`.
- Update exhaustive switches, node inspection selectors, source policy fixtures,
  and renderer documentation. Do not weaken `-Wswitch-enum` with default cases.
- Add a negative consumer fixture proving `Canvas{}` no longer compiles and valid
  source/install/relocation fixtures for both new types.
- Keep historical text in existing plans identifiable as the old name; update
  active cross-task contracts at implementation time rather than mass-replacing
  every English occurrence of “canvas.” There is no retained `Canvas` ABI promise.

### 3.4 Shared properties and clipping

Change `AppearanceProperties::clip` to `std::optional<bool>`, default `nullopt`.
Existing `.clip = true/false` initialization remains valid. Resolve omission at
mount: true for both canvases and existing inherently clipped components, false
for ordinary components. Audit all readers and shared-property copy paths; do not
accidentally treat engaged `false` as true by testing only optional engagement.
This is necessary because the current false default cannot distinguish omission
from an explicit request to disable clipping.

Both canvases support enabled/inert policy, capture/bubble pointer handling,
accessibility, and lifecycle through `createConfigured` and existing input routes.
They have no implicit action, keyboard focus, or per-pixel hit testing. A drawing
change alone never changes node identity, bounds, interaction region, or focus.
Opacity zero and hidden/collapsed states do not dispose retained bindings/state;
removal and document close do.

Use existing subtree opacity/disabled rules. Do not introduce group-isolated
opacity as a hidden canvas-only change: effective opacity modulates each ordered
CPU operation or GPU command contribution, including a program's premultiplied
output. Overlap semantics match current per-operation painting. Borders remain
in the existing node suffix and do not consume layout space.

## 4. Drawing values, versions, and safety boundaries

### 4.1 CPU drawing

Add `Drawing::fillRect(Rect, Color, double cornerRadius = 0)` and
`strokeRect(Rect, Color, double width = 1, double cornerRadius = 0)`.
Retain `fill` and `stroke` as small forwarding methods with identical validation.
Keep the existing public `commands` vector for source compatibility with authored
line/text/clip commands; those additional operations are not required by Swift's
two-helper CPU API but must not regress.

Add semantic equality to `PaintCommand` and `Drawing`. Compare fields meaningful
for each opcode, including text/font only for text and no ignored geometry for
`popClip`; finite values use the repository's ordered-comparison style. Do not
compare native pointers, padded object bytes, or only vector addresses. Mount
stores an owning validated copy; later builder edits do not mutate it.

All helpers validate before append; publication validates the entire stream because
callers can edit `commands` directly. Reject non-finite geometry/color, invalid
SDR components, negative sizes/radii/widths, invalid text font parameters, clip
underflow, and leftover pushes. Validate derived endpoints too, since finite
operands may overflow on addition. Lines retain signed deltas. Empty rectangles,
empty strings, and zero-width strokes produce no ink; zero-size clips are empty.
Canonicalize radii to half the smaller rectangle extent for rendering/ink.

Throw `std::invalid_argument` for programmer-authored invalid drawing input, as
other property contracts do. Prepare validation/copy before replacing node state.
Failure keeps the prior retained drawing and cannot publish a partial stream.
Allocation exceptions retain the same strong guarantee.

### 4.2 GPU drawing surface

```cpp
class GPUDrawing {
public:
	GPUDrawing();
	explicit GPUDrawing(GPUProgram program);
	void fillRect(Rect, Color, double cornerRadius = 0);
	void fillRect(Rect, ExtendedColor, double cornerRadius = 0);
	void fillEllipse(Rect, Color);
	void fillEllipse(Rect, ExtendedColor);
	void drawMesh(GPUMesh);
	void strokePolyline(std::span<const Point>, Color, double width = 1, bool closed = false);
	void strokePolyline(std::span<const Point>, ExtendedColor, double width = 1, bool closed = false);
	[[nodiscard]] auto operator==(const GPUDrawing &) const noexcept -> bool;
};
```

Privately store a tagged variant of rectangle, ellipse, mesh, polyline, and
program commands. `GPUDrawing(program)` starts with one program operation;
subsequent primitive appends paint after it, matching the upstream construction
surface. Do not invent an unrestricted callback operation. Polyline input is
copied; mesh/program arguments retain immutable storage.

Drawings use copy-on-write immutable command storage: copying is O(1), appending
detaches only when shared and assigns a fresh version after successful mutation.
Copies compare equal; independently authored identical drawings compare unequal.
No mutation or reference-count operation establishes cross-thread publication:
the application still uses a synchronized handoff. Concurrent writes to the
same builder object are not supported.

Use the existing checked atomic `detail::nextIdentity()` through a typed internal
`RenderVersion`, not a second wrapping counter or a scene revision. Exhaustion is
diagnosed before identity reuse. Default construction produces a valid empty
drawing with its own version. Move-from drawing is a valid empty reusable builder;
it must not accidentally compare equal to an unrelated nonempty drawing.

### 4.3 Extended colors and meshes

- `ExtendedColor(double red, double green, double blue, double alpha = 1)` owns
  straight, scene-linear extended-sRGB values. RGB must be finite with absolute
  value at most 65,504; alpha is finite in `[0,1]`. Negative linear components are
  allowed. Invalid authoring throws before a command changes.
- `GPUVertex` holds `Point position` and `ExtendedColor color`.
- `GPUMesh::create(std::span<const GPUVertex>, std::span<const std::uint32_t>)`
  returns `std::expected<GPUMesh, GPUError>`, copies validated arrays once, and
  exposes read-only spans. Index count is divisible by three; every index is
  smaller than vertex count; positions are finite and safely float-convertible.
  Empty mesh is valid; incomplete triangles and invalid indices are rejected.
- Immutable mesh equality is version equality, not O(vertex count) comparison.
  No writable vector or borrowed span is stored in any published GPU value.

Built-in rectangle/ellipse inputs require finite nonnegative extents and bounded
conversion; validate `origin + size` and expanded stroke ink before narrowing.
Reject rather than silently converting infinity to a float. Polyline points are
finite, width is finite/nonnegative, empty input and width zero are no-ops.
One point is a round disk of radius width/2; duplicate consecutive points are
coalesced for geometry, with all-duplicate input treated as that disk. Open paths
have round caps and joins; closed paths join last to first without cap duplication.

### 4.4 Color tokens needed for actual parity

The current C++ `Color` is concrete encoded sRGB; Swift's ordinary drawing colors
can also be semantic. Add a trailing `Color::Token` field defaulted to `explicit`,
and named constructors for `surface`, `background`, `text`, `secondaryText`, and
`border`; make `accent()` a semantic token retaining its current numeric fallback.
Existing explicit aggregate/RGB construction remains explicit and source-valid.
Do not make arbitrary numeric colors theme-dependent.

Use one portable resolver for CPU and GPU values with `Environment::theme` and
`highContrast`. Keep C++'s current accent RGB as its default palette; port upstream
light/dark surface/background/text/secondary/border tables for the new tokens.
Resolve tokens before native transport. Include resolved environment identity in
affected paint/prepared keys; theme/contrast changes repaint semantic drawings
without mutating their authoring versions. Existing explicit colors remain stable.
No user-defined theme package is required for this task.

## 5. GPU resources and public program contracts

### 5.1 Value and error organization

Define in `stana.graphics`, re-exported by `import stana`:

- `GPUError { Category category; std::string message; }`, with categories
  `invalidDescriptor`, `compiler`, `binding`, `capability`, `execution`.
- `GPUDiagnostic` with category (the four runtime categories), owning message,
  typed `NodeId instance`, and scalar `programVersion`. Native transport includes
  the full occurrence and renderer/request generations; a string description is
  for display only, not identity matching.
- `GPUColorEncoding { encodedSRGB, extendedLinear }`.
- `GPUTextureFormat { bgra8Unorm, rgba16Float, r8Unorm }`, respectively 4, 8, 1
  bytes per pixel. `r8Unorm` is scalar data, not an RGB/alpha color encoding.
- `GPUShader`, an immutable value created by `source(std::string)` containing owned
  WGSL source. No native-language tag, compiled-native-library variant, include
  path, native compiler option, or device ownership exists in its public API.
- `GPUFunctionConstant = std::variant<bool, std::int32_t, float>`; floats finite.

Fallible descriptor factories return `std::expected<Handle, GPUError>`.
`std::bad_alloc` and identity exhaustion remain exceptions; do not label allocation
failure “invalid descriptor.” Native adapters use `expected` internally and
convert owning error values to queued diagnostics. C++23 `expected` is the chosen
value/error mechanism, supported by its [WG21 specification proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0323r12.html).

Immutable handles expose const metadata accessors only. Default construction is
not available where an invalid resource would result. Builders/descriptors are
ordinary aggregates; successful factories produce self-contained immutable
storage with fresh version identity. No caller must keep a descriptor alive.

### 5.2 Resource declarations

| Type/factory | Descriptor and semantics |
| --- | --- |
| `GPUBuffer::create(span<const byte>)` | Immutable owned bytes; at most 256 MiB. Empty bytes allowed until a binding requires a minimum size. |
| `GPUTexture::create(GPUTextureDescriptor)` | Positive width/height, format, owned tightly packed top-down bytes, encoding `encodedSRGB`, `premultiplied = true`, `sourcePeak = 16`. |
| `GPUStateSlot::create(GPUStateSlotDescriptor)` | Positive width/height, format `rgba16Float`, optional owned initial bytes, `resetKey` string. Previous/next temporal texture pair. |
| `GPUSampler` | `Filter { nearest, linear }`, default linear; sampling beyond extent yields transparent zero. |
| `GPUStorage` | `length` bytes, positive and at most 256 MiB; zero-initialized transient per offscreen step. |
| `GPUTarget` | Width/height independently zero or positive, default zero/zero; format `rgba16Float`. Zero axis resolves to current canvas backing extent on that axis. |
| `GPUResource` | `variant<GPUBuffer, GPUStorage, GPUTexture, GPUTarget, GPUStateSlot, GPUSampler>`. |

Texture/state axes are at most 16,384; per-resource payload at most 256 MiB.
Validate using division/checks **before** multiplying width × height × bpp, then
require exact data length. Revalidate resolved automatic targets after scale/size
changes and against actual device limits. Zero backing extent suspends execution;
do not allocate a fake 1×1 target or advance temporal state.

Texture source peak is finite in `(0, 65504]`. BGRA8 and RGBA16Float carry declared
encoded/linear color and alpha interpretation; validate finite half values and
alpha range at ingestion, and zero RGB for premultiplied alpha-zero pixels.
Straight-alpha zero-color payloads may contain RGB, which ingress canonicalizes
to transparent zero. R8 uses its red scalar without transfer/premultiplication;
reject nondefault color metadata on R8 rather than pretending it has RGB channels.
Initial state bytes are raw data in their declared format, checked for length;
state used as final color output must honor the program output contract.

Texture payloads are read-only views of owned storage. RGBA16Float bytes are
IEEE-754 binary16 components in R,G,B,A order; BGRA8 is B,G,R,A. Implement portable
checked conversion helpers, including byte order handling, not a nonexistent
assumption that every selected STL exports `std::float16_t`.

### 5.3 Bindings and pass descriptors

```cpp
struct GPUBinding {
	std::string resource;
	std::uint32_t group = 0;
	std::uint32_t binding = 0;
	enum class Access { read, write } access = Access::read;
	enum class Stage { vertex, fragment, compute } stage = Stage::fragment;
	bool previous = false;
};

struct GPURenderPass {
	std::string vertex;
	std::string fragment;
	std::string target = "output";
	std::uint32_t vertexCount = 3; // Index count when indexBuffer is present.
	std::uint32_t instanceCount = 1;
	std::optional<std::string> indexBuffer{};
	std::vector<GPUBinding> bindings{};
	bool blends = false;
};

struct GPUComputePass {
	std::string function;
	std::array<std::uint32_t, 3> workgroupCount{1, 1, 1};
	std::vector<GPUBinding> bindings{};
};
using GPUPass = std::variant<GPURenderPass, GPUComputePass>;

struct GPUProgramDescriptor {
	GPUShader shader;
	std::map<std::string, GPUResource, std::less<>> resources;
	std::vector<GPUPass> passes;
	std::string output = "output";
	GPUColorEncoding outputColor = GPUColorEncoding::extendedLinear;
	double sourcePeak = 16;
	std::map<std::string, GPUFunctionConstant, std::less<>> constants;
	std::optional<double> samplingRadius{};
};
```

Default program descriptor resources contain `"output": GPUTarget{}` through
an inline member initializer, preserving aggregate designated initialization.
`GPUProgram::create(GPUProgramDescriptor)` validates and takes ownership.
`GPUProgram::updatingResources(map<string, GPUResource, less<>>)` returns a new
`expected<GPUProgram, GPUError>` and leaves the old program untouched.
Render topology is triangles, no depth/stencil, one color attachment, sample count
one for application programs. Vertex inputs use WGSL `@builtin(vertex_index)`
and explicitly bound buffers with WGSL host-shareable layout.
There is no injected native vertex layout, reserved hidden buffer, time, viewport,
or uniform binding. Applications explicitly provide uniform bytes and bindings.

The examples must show checked factory results, a complete valid WGSL render
shader, compute-to-render dependencies, a mesh, and WGSL loaded into an owned
string from an application resource. Consumer code has no platform conditionals.
Do not ship the Swift fixture's placeholder shader string as an executable demo.

### 5.4 Portable shader execution convention

The public shader contract follows WGSL semantics and the portable
[WebGPU coordinate convention](https://www.w3.org/TR/webgpu/#coordinate-systems),
not the current native API's ABI. Vertex output is homogeneous clip position;
after division by W, visible NDC X/Y lie in `[-1,1]` and depth in `[0,1]`.
Framebuffer and 2D texture coordinates have a top-left origin. Applications receive
only their declared inputs and WGSL builtins. Conversion from logical canvas
points is explicit in app-owned uniform data/source; Stana does not inject a
native viewport struct. Native lowering handles coordinate/entry-point adaptation
and helper arguments privately. Test asymmetric geometry and texture orientation
so a native-only flip cannot become the portable behavior accidentally.

Compute uses `workgroupCount` and WGSL `@workgroup_size`; program constants provide
WGSL overrides, not native specialization IDs. Buffer contents follow WGSL
host-shareable alignment/stride; documented C++ packing helpers must produce that
layout rather than mirror a native struct. Sampler/format enums describe logical
behavior, never native numeric enum values. Changes in adapter or compiler must
not require an application source, binding, or resource-packing variant.

## 6. Descriptor validation and dependency graph

### 6.1 Atomic validation algorithm

Complete this algorithm before assigning a new immutable program identity:

1. Require 1–128 passes, 1–128 resources, valid output name and produced texture,
   finite valid source peak and optional nonnegative sampling radius. Resource,
   entry-point, and constant names are nonempty valid UTF-8 with no embedded NUL;
   resource names starting with `$` are reserved and rejected in canvas mode.
2. Validate every resource and all aggregate byte/count budgets. Validate target
   partial-zero axes without unchecked multiplication. Descriptor correctness is
   portable; device-specific limits are checked again at native preparation.
3. Validate stages, resource existence, and logical `(group, binding)` coordinates,
   with `group < 4`, `binding < 128`, and at most 128 distinct bindings per pass.
   There is one logical namespace, not native buffer/texture/sampler namespaces.
   Repeated coordinates across vertex/fragment stage declarations may share the
   same resource/access/previous tuple; conflicting declarations or duplicate
   entries for the same stage are rejected. Device limits are checked privately.
4. Compute each pass's write set, including render attachment. Only storage,
   target, and state may be written. Samplers and immutable resources are read-only.
   `previous=true` is permitted only for read-only state binding.
5. Reject same-pass current read/write aliasing, attachment sampling, and multiple
   logical writers. Multiple write aliases to the same resource within one pass
   are also rejected; a render attachment cannot also be a writable binding.
6. Build one producer per transient output resource. A current-step read of
   target/storage requires a producer; immutable input reads do not. Reading
   previous state needs no current producer. Unwritten state remains initialized
   or retains its committed history; reading its current version without a writer
   is rejected. Output must have one current-step producer.
7. Add producer → consumer edges for current reads, and the index buffer's
   implicit read when applicable. Exclude previous-state reads from cycle edges.
8. Stable topological sort with input pass index breaking ties. Detect cycles,
   including self-dependencies. Validate every pass, not just output-reachable
   passes: upstream executes all declared passes, including state writers.
9. Store validated execution order and normalized read/write plan in the immutable
   program. No repeated string graph rebuilding on each frame.

Render entry names and counts must be nonempty/positive. An index buffer must be
an immutable buffer, have a byte length divisible by four and enough UInt32
indices for `vertexCount`. Application vertex fetching is shader-defined; do not
claim descriptor validation can prove its arbitrary buffer access safety.
Compute workgroup counts are positive; combine them with the WGSL entry point's
resolved `@workgroup_size` using checked arithmetic before encoding. Bindings with
compute stage belong only to compute passes and vice versa. No descriptor can
read or write the window drawable. Public bytes for uniforms/storage use WGSL
host-shareable layout, not `sizeof` a native compiler's generated argument struct.

### 6.2 Portable shader validation and private native reflection

Use Tint's WGSL reader/validator and entry-point inspection before native lowering.
Check entry stage, logical bindings, buffer address space/layout/minimum size,
texture dimension/sample/format/access, overrides and workgroup size. Resolve
`GPUFunctionConstant` names as WGSL `override` identifiers with exact types.
The initial portable profile accepts f32/i32/u32/bool, 2D non-array textures and
samplers, uniform/storage buffers, render/compute entry points and bounded
workgroup memory. Reject optional extensions, resource binding arrays, external
textures, subgroup requirements and native-only features; half-float image storage
does not require WGSL f16 arithmetic. Reject unsupported resources before lowering.

Only compute passes may write bound storage; render passes write their declared
attachment. A `.write` storage-buffer binding grants exclusive access to its
zero-initialized same-step allocation, including shader-local read/modify/write
with correct WGSL synchronization. It does not grant a second aliased binding or
access to previous-step contents. Read-only bindings cannot resolve to writable
shader declarations. Storage texture writes initially use `rgba16Float`; other
formats can be sampled or used as supported render attachments, but no driver-
specific storage extension becomes a public requirement.

Privately lower logical bindings to dense per-stage native slots and retain the
mapping with the executable. Request native pipeline reflection and check it
against that generated mapping, not against public numeric slots. Native helper
arguments and translated names remain private. Reconcile uniform layout and
generated helper buffer sizes before submission. Unused declared bindings remain
structurally validated. Compiler/native reflection disagreement is an error, not
permission to partially bind a shader.

Re-run binding compatibility and minimum-size checks for **every new resource
version**, even when executable/pipeline cache hits. A cached 16-byte-uniform
pipeline cannot accept a replacement 4-byte buffer. Texture format/type changes
also invalidate compatibility. Reflection is not an execution-time bounds proof;
run [Metal Shader Validation](https://developer.apple.com/documentation/xcode/validating-your-apps-metal-shader-usage)
for native verification. Public compiler diagnostics identify the original WGSL
entry/source locations and logical bindings; generated source, native register
identifiers, and Tint/native reflection types are not public API fields.

### 6.3 Three separate program identities

| Identity | Changes when | Preserves |
| --- | --- | --- |
| Input/program version | Any valid replacement or new program | Nothing about rendered output freshness. |
| Pipeline version | WGSL source, entry functions, overrides, logical pass/bindings, target formats, or host-shareable layout changes | Resource-only content updates with compatible layout reuse executable pipelines. |
| State version | State descriptor/resetKey, incompatible executable/resource schema, renderer reset, or backing configuration changes | Compatible immutable-input replacements keep previous successful state. |

Compute replacement compatibility from the complete resource key set, resource
kind, binding-facing texture format/type/encoding conversion plan, and executable
signature. Resource contents, source peak, fixed target sizes, and byte lengths
may change without recompilation if the binding ABI remains compatible, but must
be revalidated. State compatibility compares state descriptors **by value**,
including initial bytes/resetKey; immutable input changes alone do not reset it.
The renderer owns additional backing/device epoch keys, not public program values.
Private compiled-cache keys also include Tint revision, lowering options, native
slot map, device/compiler profile and generated entry points. These never appear
as shader variants or backend switches in public handles.

Do not retain a pipeline merely because only target formats match, and do not
reset simulation whenever an application updates its uniform buffer. Compiling
specialized variants can be expensive; Apple's [function specialization guidance](https://developer.apple.com/documentation/metal/using-function-specialization-to-build-pipeline-variants)
supports preparing and caching them outside the render loop.

## 7. Retained integration, coordinates, and invalidation

### 7.1 Mounted ownership

Install each drawing property through the existing node scope. Validate/prepare
an incoming CPU value before assignment; GPU handles are already validated by
their factories, with cheap command invariants checked at append. Equal CPU
drawings or identical GPU versions suppress paint publication. Delivered-binding
metrics retain their existing meaning; do not confuse delivered assignments with
actual drawing changes.

Bindings own values/subscriptions; node error handler storage belongs to the
mounted occurrence. Descriptions are destroyed after mount. Snapshots never copy
application callbacks into worker/GPU payloads. Removal immediately disconnects
bindings/diagnostic delivery while in-flight resource owners retire separately.

### 7.2 Layout and local coordinates

Both canvases have zero intrinsic content size, plus normal padding/constraints.
Meshes/rectangles do not resize the component. Define one content-rectangle helper
shared by scene full-build, patched projection, GPU bounds, and text where relevant:

```text
physicalLeftPadding = direction == LTR ? leading : trailing
contentOrigin = outerOrigin + (physicalLeftPadding, top)
contentSize = max(0, outerSize - physical horizontal/vertical padding)
```

Commands remain y-down in logical points. CPU ink may overflow the content box
into padding, constrained by active outer/ancestor clips. A GPU canvas renders to
its content-sized offscreen output; geometry outside that output is cropped even
when outer clipping is disabled. Default outer clipping additionally applies the
node's rounded bounds. This is an intentional documented difference between CPU
unbounded commands and GPU texture-backed output, not an accidental scissor bug.

Backing dimensions are checked `ceil(contentSize * backingScale)` integers.
Perform finite/range checks before conversion. Preserve fractional logical origins
when sampling; do not move vertices by independently rounded layout coordinates.
Viewport, ancestor clip, content output, and tile scissors are distinct regions.

### 7.3 Invalidation matrix

| Change | Layout | Content preparation/execution | Composition/damage |
| --- | --- | --- | --- |
| Equal drawing assignment | None | None | None. |
| New CPU drawing | None | Changed CPU segment | Old/new actual ink under clips. |
| New GPU primitives | None | Changed GPU leaf | Old/new content output. |
| Compatible program input replacement | None | Revalidate bindings, execute once | Program output plus affected final tiles. |
| Program executable/state change | None | Prepare replacement/reset candidate | Output; retain old valid content until successful replacement. |
| Bounds/scale change | Normal task-01 dependency rules | Reallocate/re-execute at new backing extent, reset size-dependent state | Old/new coverage; full new surface when required. |
| Pure movement with unchanged backing extent | No extra measurement | Reuse local primitive/program output and temporal step | Old/new positions, updated clip/scissor. |
| Opacity/enabled | None | Reuse local GPU command surfaces | Changed contribution/damage; no state step. |
| Semantic theme/contrast | None | Affected CPU and built-in GPU color preparation | Resolved color changes; program bytes unchanged. |
| Clip/corner radius | None | Reuse unchanged content | Old/new clipped coverage and borders. |
| Hidden/collapsed | Existing layout semantics | No admitted hidden execution | Erase prior visible contribution; retain state. |
| Reorder/removal | Existing structure rules | Keep surviving content cache | Reconstruct affected overlap in new painter order. |
| Display headroom only | None | No CPU raster/program step | Remap final output; invalidate presentation mapping generation. |

Visible return reuses valid output if extent/profile/epoch did not change. New
drawings submitted while hidden coalesce to latest desired input; they do not
create unseen simulation steps. Scope removal/reinsertion creates a new instance.

## 8. Ordered scene and native transport

### 8.1 One complete mixed paint tree

Extend task-01 immutable scene branches with typed CPU segments and GPU leaves,
not an unrelated `vector<GPUCanvas>` rendered after all CPU content. The observable
order for a node remains background, push clip, local content, children, pop clip,
border/focus suffix. A GPU leaf occupies the local-content position.

Each GPU leaf owns node identity, content bounds, drawing version/value,
resolved color environment, effective opacity, active clip context, and relevant
generation. Keep drawing-local geometry unchanged; translate at composition.
Changing one leaf path-copies its ancestor route and reuses unrelated branches.

CPU-only `SceneSnapshot::commands` remains the lazy compatibility/reference view
from task 01. It does not pretend to contain GPU pixels. Add a typed mixed-paint
inspection view in `stana.testing`; GPU-aware full-reference reconstruction uses
the same mixed tree and execution outputs, not the CPU-only flattened vector.
Explicitly document this inspection limitation to prevent false parity tests.

### 8.2 Native transport boundary

`native_contracts.hpp` remains module-free and private. Factor its owning GPU
descriptors/paint entries into `src/platform/gpu_contracts.hpp` only when needed
for focused compilation; include it from the native boundary. Convert public enums
and variants explicitly in the C++ application bridge with exhaustive visitors.
Do not `reinterpret_cast` public class layouts or include module internals in `.mm`.

Use immutable shared blob/array owners whose bytes can be retained by both
portable and module-free records without repeatedly copying textures/meshes.
Put their module-free storage definitions in a private internal header available
to the graphics implementation and bridge. Exported accessors still expose only
standard const spans. Cache converted immutable native resources by render version
and prune weak/tombstoned entries incrementally. Native code never owns mutable
nodes, `Signal`, `Property`, or user callbacks.

Conversion cache equality includes profile-sensitive resolved metadata and schema;
a weak entry cannot return resources whose leases have expired. Published native
scenes own everything needed after the document publishes a later scene or closes.

## 9. Native renderer, composition, and shader packaging

### 9.1 Responsibility split

Build on task 01's `renderer.mm` and `presentation.mm` extraction. Add real
responsibility-focused Apple-only units:

- `gpu_resources.hpp/.mm`: device capability profile, immutable uploads, checked
  allocations, leases, LRU accounting, sampler and pipeline ownership.
- `gpu_programs.hpp/.mm`: compiler queue, reflection, validated pass encoding,
  temporal candidate/commit, diagnostics.
- `compositor.hpp/.mm`: ordered CPU/GPU runs, shape/mesh/polyline encoding,
  complete final tiles and display mapping.
- `shaders/stana.metal`: built-in shape, mesh, texture composition, ingress, and
  display functions, with a small checked private CPU/MSL packing contract.

Do not put all of this into `native_host.mm`. Native host owns AppKit/layer/event
lifetimes and asks the renderer for complete ready packets. Portable descriptor
validation belongs in `src/graphics/gpu_program.cpp`, not Objective-C++.

### 9.2 Mixed composition algorithm

1. Traverse the immutable paint tree in order, preserving clip contexts. Combine
   adjacent compatible CPU entries into raster runs; flush a run at a GPU leaf.
2. Rasterize only dirty CPU run tiles to transparent BGRA8 buffers, using task-01
   direct shared storage on capable devices. Do not bake the window background
   into every run. Reuse unchanged prepared commands and tiles.
   Create a sampleable buffer-backed texture only when the device supports the
   format/usage and queried linear-texture offset/row alignment. Task 01's 256-byte
   raster row pitch alone does not prove that a texture view is legal. Otherwise
   use a GPU buffer-to-private-texture blit with checked row pitch and account for
   that transfer; this is not a second CPU bitmap upload on the unified profile.
3. Prepare/reuse each GPU drawing's ordered command surfaces. Execute each custom
   program once per admitted input step into its own full output, then compose
   that surface at its command position. Do not rerun a program per final tile.
4. Clear each dirty final composition tile with the viewport background and replay
   all overlapping CPU/GPU contributions in original order, honoring current clips,
   premultiplied source-over, and effective opacity. All later CPU labels/borders
   must remain above earlier GPU content where the tree says so.
5. Store complete offscreen final tiles; reconstruct every acquired tile drawable
   completely. Reuse unchanged final tiles; never sample or assume preserved
   drawable pixels. Presentation mapping may render into the fresh drawable rather
   than blit when color conversion is needed.
6. Hand the complete packet to task-01 tiled presentation/evidence. Keep offscreen
   success and drawable confirmation as separate events/identities.

CPU-only SDR windows keep task-01's direct raster-buffer-to-drawable fast path.
Only windows/regions requiring mixed content incur run textures/composition.
Changing one GPU leaf must not rerasterize unrelated unchanged CPU content.
Dirty final tiles may replay cached unchanged overlapping inputs for transparency;
that is necessary work, not failed incremental behavior.

Preserve the chosen per-operation opacity semantics: a program output is one
command contribution; built-in commands can reuse cached geometry or individual
command surfaces. Do not flatten several overlapping commands at opacity one and
apply canvas opacity to the flattened result, which would introduce group opacity.
Opacity changes may require recompositing that canvas's ordered commands but must
not recompile programs, upload unchanged meshes, or advance temporal state.

### 9.3 Shape quality and native vertex ABI

Use antialiased analytic coverage for rounded rectangles, ellipses, and round
polylines, with a one-physical-pixel conservative coverage fringe. Ellipse coverage
uses both radii; non-square bounds must not become circles. Clamp radius as CPU
does. Meshes use indexed triangles and interpolated premultiplied linear color.
Preserve authoring order; batch only adjacent compatible commands without moving
translucent operations across each other.

Use four-sample MSAA for mesh edges when supported and scratch allocation fits
the budget; a selected one-sample profile is explicit in diagnostics/reference
test settings, never a different topology or missing draw. Query support as Apple
requires in [sample-count documentation](https://developer.apple.com/documentation/metal/mtltexturedescriptor/samplecount).
Custom programs retain their specified sample-one contract.

Define private packed GPU vertex/uniform records with fixed-width scalar fields,
explicit padding, `alignas`, and `static_assert` on size/offsets. Do not upload
`GPUVertex`, `Point`, `Color`, `std::variant`, or a public `double` struct by memory
copy and assume it matches MSL. Pack through checked conversions into spans;
ownership remains alive until completion. No aliasing a byte vector as a live
typed object without alignment/lifetime guarantees.

### 9.4 Shader delivery

Ship built-in shader source as an embedded generated byte/string resource inside
the static library, generated deterministically from the tracked `.metal` file at
build time. Compile it once per device on renderer initialization/compiler queue;
do not load it from a checkout-relative path or require the consumer's working
directory. This avoids adding Xcode's `metal` executable to portable builds and
keeps installed/relocated packages complete. Failure of a required built-in shader
is a native startup failure, not an application `onError` or fallback.

Application shader ingress is WGSL source only. The private compiler service
validates it, resolves overrides and logical resources, lowers to backend source,
and invokes the native compiler on the bounded compiler queue. Preserve Tint's
robustness and workgroup-memory initialization transformations; do not discard
them as a speed optimization. Compilation errors report original source locations
where available. No app-provided native source or binary bypass is accepted.

Do not write arbitrary application source or compiled assets to disk. No persistent
cross-driver pipeline cache is required. Pin compiler/lowering options for the
private device profile; do not use native fast-math assumptions as input validation
or enable global C++ `-ffast-math`. Native built-in `.metal` resources are strictly
private and do not imply that application shaders may use that language.

### 9.5 Private Tint dependency and distribution

Use Tint's C++ libraries from the official Dawn repository, not a new shader
compiler, full Dawn rendering runtime, or command-line subprocess per frame.
Its [CMake build definitions](https://github.com/google/dawn/blob/main/CMakeLists.txt)
provide separate WGSL reader and MSL writer options. Configure only the required
reader, validator/inspector, IR and MSL writer plus their necessary support targets;
disable Dawn native backends, samples, tools and unrelated shader writers. The MSL
writer is pure translation and is built/tested on all supported host platforms,
without requiring Metal SDKs on Windows/Linux. No Tint type or header is exported.

During M0, select and record a tested immutable upstream commit plus its exact
dependency revisions/archive hashes in `cmake/ShaderCompiler.cmake` and the
bootstrap manifest; never track floating `main` during builds. Baseline language
support is the core WGSL accepted by that pinned compiler and Stana's documented
portable profile, not every future draft feature. Build integration must pass the
existing Clang/STL/sanitizer matrix before native program work proceeds.

Add `tools/bootstrap_tint.py` using the repository's verified-dependency pattern.
Fetch only as an explicit bootstrap step, verify source/dependency hashes, cache
by revision/toolchain/configuration, and support already-provisioned offline builds.
Do not run an unpinned dependency downloader during normal consumer configuration.
Keep third-party build policy isolated from first-party warnings while retaining
the latter unchanged; instrument supported compiler-library builds with the same
sanitizer as Stana. Record licenses and source provenance in release packages.

Package required private Tint/support static archives and exported private CMake
link targets alongside Stana, with relocatable paths and no Tint header requirement
for consumers. Source/install/relocation tests must link and run portable validation
offline. Account for compiler artifacts in package metadata; GPU allocation budgets
do not claim to include compiler-process CPU memory. The compiler service remains
bounded in admission and input sizes even though compilation cannot be forcibly
interrupted. These private dependencies do not change `import stana` or C++23.

## 10. Device profiles, hazards, and resource lifetime

### 10.1 Portable dependency plan, native capability checks

A pass graph stores stage-specific reads/writes independent of API. The initial
Metal executor uses one classic command queue per device and serial pass encoding
with directly bound, hazard-tracked resources allocated from the device. Do not
use untracked heaps, argument buffers, concurrent compute dispatch, or multiple
queues in the first implementation. If these are later introduced, explicit
barriers/fences/events are required by the stored dependencies.

Apple documents different synchronization guarantees for classic tracked queues
and Metal 4 queues; selecting the classic path is deliberate, not an assumption
that queue order always solves hazards. See [resource synchronization](https://developer.apple.com/documentation/metal/resource-synchronization).

Check texture dimensions, pixel-format usage, writable-stage capabilities,
buffer lengths, threadgroup limits, and dispatch dimensions on the actual device.
Use the WGSL entry point's resolved workgroup size and the public workgroup counts;
validate each axis, total threads/group, workgroup memory, and checked total
invocations against portable and device limits. Do not silently choose a different
group size, reinterpret workgroup counts as invocation counts, or add unsafe tail
guards around shader barriers. No backend may bypass validation simply because a
public descriptor was already valid.

### 10.2 Unified and discrete memory

- Apple/unified profile: CPU raster writes into shared buffers; GPU reads only
  after CPU writes finish. Immutable texture uploads use supported staging/private
  resources; share mesh/buffer uploads across unchanged draws.
- Intel/AMD profile: select managed CPU-written buffers with `didModifyRange`,
  or a checked staging-to-private transfer when required by format/device. GPU
  readback of managed resources requires synchronization before CPU reads.
- Both profiles prohibit overwriting resources still referenced by a submitted
  command buffer. CPU/CoreGraphics ownership and GPU read leases are distinct.
- Report upload/staging bytes honestly. Zero CPU-upload is not promised on a
  discrete GPU and cannot be inferred from using the same public resource type.

These profiles follow Apple's [Intel/AMD storage-mode guidance](https://developer.apple.com/documentation/metal/choosing-a-resource-storage-mode-for-intel-and-amd-gpus).
Allocation failure remains possible even within arithmetic/device budgets.

### 10.3 Accounting and bounded caches

Use these named internal defaults, not public tuning properties:

| Limit | Default |
| --- | --- |
| Texture axis | 16,384 pixels, further limited by device. |
| Single CPU payload/native resource | 256 MiB, including checked state-pair individual allocations. |
| Resources / passes per program | 128 / 128. |
| Commands / points / mesh vertices / mesh indices | 65,536 / 1,048,576 / 1,048,576 / 3,145,728. |
| WGSL source bytes | 16 MiB per shader. |
| Function constants / name bytes | 128 / 1,024 UTF-8 bytes per name. |
| Bindings per pass | 128 distinct logical coordinates; at most 256 stage declarations. |
| Compute workgroup size / memory | At most 256 invocations/group and 16 KiB workgroup memory; lower device limits reject before submission. |
| Compute invocation product | 268,435,456 per pass. |
| Total owned descriptor input bytes | 256 MiB per program/drawing, shared blobs counted once per value. |
| Device canvas allocation budget | 512 MiB across windows, including live, cached, and retired tracked allocations. |
| Window canvas allocation budget | 256 MiB, within the device budget. |
| Resident application pipeline entries | 128 per device, LRU and strong in-flight leases. |
| Outstanding compilation | One active job and latest queued request per window; 16 pending windows/device. |
| Diagnostics | 256/window; message 8 KiB; deliver at most once per failure identity. |
| Timing history | 256 revision-tagged samples/window. |

Task-01 raster ownership remains within its 256 MiB/window cap **and** the combined
window/device cap here; do not allocate independent unaccounted budgets per run.
Count transient targets, resolved outputs, previous/next state, MSAA scratch,
staging, CPU raster buffers, cached uploads, and retired command-buffer leases.
Charge shared allocations once to the device and conservatively to each window
pinning them; release charges only when final ownership ends. Include private
device allocation sizes where available, with checked estimates otherwise.
Report drawable/compositor/driver memory separately: these caps are not a promise
of bounded total process memory or bounded app-held immutable resource values.

Before admitting a frame, calculate its complete peak candidate/retirement demand,
evict unpinned LRU entries, and reserve capacity atomically. Do not allocate half
the frame then discover its state pair cannot fit. Failure preserves prior output,
produces a bounded capability diagnostic, and stops repeated allocation retries
until a relevant input/epoch/budget availability changes. Descriptor limits bound
bookkeeping; trusted shader execution time is not bounded by these numbers.

### 10.4 C++23 implementation discipline

Use `unique_ptr`/RAII for exclusive native owners, ARC for Objective-C objects,
explicit CF deleters, immutable shared owners only where snapshots/in-flight work
really share lifetime, and `span<const T>` for borrowed synchronous inputs.
Keep non-generic graph/math/upload work in compiled units. Reserve known vector
sizes; reuse prepared immutable versions; avoid per-frame deep copies and global
mutexes on the UI paint path. Do not introduce a general service locator or RHI
class hierarchy to represent the one implemented backend.

Use `variant` visitors with exhaustive handling, checked `size_t` arithmetic,
`std::in_range` before narrowing where applicable, and diagnosed overflow. Preserve
RTTI/exceptions, hardening, `-Wunsafe-buffer-usage`, clang-tidy, and sanitizer gates.
The [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-raii)
inform RAII and bounds-aware interfaces; performance shortcuts do not permit raw
owning allocations, detached workers, dangling spans, or blanket warning disables.

## 11. Programs, temporal state, scheduling, and diagnostics

### 11.1 Per-instance temporal ownership

Program instance identity is `(document/node identity, command ordinal)` plus
validated state version and renderer/backing epoch. Sharing a `GPUProgram` across
two canvases shares immutable inputs/pipelines, **not** simulation textures or
step counters. New node identity or changed command ordinal starts a new instance.

Maintain a committed state bundle and a distinct candidate bundle. Initialize
previous state from exact initial bytes or zero; zero the next bundle before each
step. `previous=true` reads only committed textures; writes target distinct next
textures. Unwritten slots carry forward their committed value without becoming
uninitialized. No pass reads bytes from an incomplete candidate as previous state.

Execute all passes once in stable topological order. On successful completion of
the entire offscreen command buffer, atomically publish all candidate state slots,
step count, program output, and valid replacement program for that instance.
Before success they are not reusable cache entries. Partial success or a cancelled
old epoch publishes none of them. State/step overflow is diagnosed, never wrapped.

An admitted input version advances an instance once. Cache identity for execution
includes instance, program/input version, content backing extent, scale, device,
state epoch, and working color profile; **document-space translation is not an
execution key**. Explicit input replacement advances the step even if its bytes
happen to compare equal; copying the same input version does not. Multiple public
versions coalesced before admission do not all execute.

### 11.2 What does and does not advance state

| Event | State behavior |
| --- | --- |
| New compatible immutable input version | Execute once against prior committed state. |
| Unchanged scene replay/exposure | Reuse output; zero additional steps. |
| Presentation-only retry or missing/zero timestamp | Reuse offscreen success; zero additional steps. |
| Dirty final tile intersects same program several times | Sample one completed output; one step total. |
| Pure position, opacity, clip, or display-headroom change | Recompose/remap; no simulation step. |
| Hidden/minimized/zero backing extent | Suspend new steps; latest desired input retained. |
| Initializer/resetKey/state schema/executable incompatibility | Fresh initialized state epoch. |
| Backing extent/scale/device or renderer recovery change | Fresh backing-dependent state epoch. |
| GPU success but final presentation later fails | Offscreen state remains committed unless renderer recovery invalidates it. |
| GPU execution failure | No candidate commit; invalidate epoch and recover as below. |

Retain the latest completed offscreen output until replaced or released. Cache
eviction may not silently replay an already committed temporal step: pin the
current state/output bundle, or suspend the instance with an explicit capability
failure. Optional old-generation outputs can be evicted after their leases retire.

### 11.3 Bounded execution pipeline

```text
UI commit → latest desired immutable scene
  → prepare/resolve native inputs (UI; existing CPU raster confinement)
  → compiler queue if needed (owning immutable descriptors)
  → offscreen GPU submission
  → GPU completion → UI commit of successful output/state
  → ready complete packet → acquire drawables → final mapping/presentation
  → joined presentation evidence → confirmed geometry
```

Keep at most one admitted offscreen candidate, one completed ready packet, and one
latest desired scene reference per window. Do not step a stateful instance
concurrently. Compile before acquiring drawables; nil/starved drawables must not
trigger more compilation/state steps.
The task-01 presenter continues to bound active presentation batches and histories.
Only one newest desired scene is retained beyond admitted work.

While a replacement compiles, continue showing the last complete output; first-use
program content stays transparent. A completed stale compilation may populate a
device pipeline cache only under its exact immutable executable key, never replace
the active program or trigger a state step. When compiler capacity becomes free,
notify waiting live windows to reconsider their latest desired request; no busy
polling or unbounded queue of obsolete scene versions.

Compiler queue jobs own descriptors, device identity, request identity, and a weak
session token. Shader compilation cannot be forcibly cancelled; discard stale
results and release owners. Native callbacks dispatch owning values back to the
UI executor and revalidate session/node/request/epoch before changing state.
No compiler/GPU callback accesses a retained node or invokes application code.
Shutdown invalidates delivery tokens, stops admission, and retires outstanding
owners safely; never join a blocked compiler/GPU operation on the UI executor.

### 11.4 Failure table

| Failure | Defined outcome |
| --- | --- |
| Invalid public descriptor | Factory returns `invalidDescriptor`; no mount/native work; old value untouched. |
| Invalid mutable CPU drawing | Diagnosed input exception before replacement/publication. |
| WGSL validation/lowering/override/native compilation | `compiler` diagnostic using public source provenance; failed replacement not committed. |
| Reflection/buffer/texture/index mismatch | `binding` diagnostic, including cached-pipeline replacements. |
| Unsupported shader/device/profile/format or budget | `capability` diagnostic; no partial submission. |
| Initially unavailable program | Transparent program contribution; background/border/other commands still paint. |
| Invalid program replacement | Reuse last successful output at the current bounds if compatible; otherwise prepare last valid program at new bounds. Never rerun it solely because replacement keeps failing. |
| GPU command-buffer failure/device loss | `execution` diagnostic; invalidate renderer epoch and affected evidence; never claim old failed surfaces remain current. |
| Drawable unavailable/presentation unconfirmed | Existing bounded presentation retry/unknown-region gates; no program re-execution. |

At most one renderer recovery cycle between successful confirmations, reusing
task-01 deadline/drain policy. Recreate resources, reset temporal epoch, and
reconstruct a full current composite. If recovery fails or cannot safely retire
owners within existing bounds, stop that native window with explicit failure.
Never free a resource just because a diagnostic was delivered or a timer expired.

`onError` runs on the UI executor after coherent settlement, outside binding
evaluation and native encoding. Deduplicate by instance/request/epoch/category,
not message text alone. Re-resolve node identity before each callback; one handler
may remove nodes or close the window. Ignore stale callbacks, contain handler
exceptions at the application boundary under existing error policy, and continue
independent queued deliveries only to still-live occurrences. Never synchronously
reenter publication from an encoder callback.

### 11.5 Presentation evidence remains exact

Offscreen success permits state/output commit, not input-geometry confirmation.
Continue joining GPU success with positive native timestamp for the same
epoch/attempt/scene and all task-01 required tiles. Apple specifies that dropped
or unpresented drawables can have zero [presentedTime](https://developer.apple.com/documentation/metal/mtldrawable/presentedtime).
Late, duplicate, zero, and old-epoch callbacks cannot resurrect removed nodes or
clear newer ambiguity. Neither readback nor a correct screenshot proves display.

## 12. Working color space, texture ingress, and final mapping

### 12.1 Window configuration

Add `RenderingColorMode { automatic, sdr, extendedLinear }` to graphics and a
`Window::renderingColorMode` field **after `content`**, default automatic, to avoid
breaking existing three-field positional aggregates. Propagate it through
`WindowSession` and private native window configuration. Source and installed
consumer fixtures verify this field order.

Automatic uses the existing encoded SDR BGRA8 path for all-SDR content; any
extended-color primitive, mesh, or extended-output program promotes the complete
window composition to linear RGBA16Float. SDR mode accepts extended inputs and
uses linear intermediate composition when required, then maps once to SDR.
ExtendedLinear forces linear half-float composition even without HDR content.
Promotion/demotion changes surface configuration/epoch and damages the full
viewport. Never demote halfway through a composite or mix profiles among tiles.

### 12.2 Exact conversion rules

Ordinary `Color` is straight encoded sRGB. Resolve semantics before transfer
conversion. Decode straight RGB with the standard piecewise sRGB transfer function
when the destination working space is linear, then premultiply. `ExtendedColor`
is already straight linear; premultiply once. Ingress of premultiplied textures
unpremultiplies only for transfer conversion when alpha is positive, then
repremultiplies; alpha zero becomes zero RGB. Blend factors are one and
one-minus-source-alpha for both color and alpha. Never decode premultiplied encoded
RGB directly or premultiply twice.

Use signed transfer extensions for allowed negative linear values; SDR export
clamps final output to `[0,1]`. Encoded BGRA8 composition in an all-SDR window
preserves existing CPU pixels. Promoted mixed windows intentionally blend in
linear space. Program outputs must be finite premultiplied values in their
declared encoding; alpha-zero output must have zero RGB. CPU ingestion checks
known data; arbitrary program output is the trusted author's responsibility,
with validation/readback regressions rather than synchronous per-frame CPU scans.

### 12.3 Display mapping contract

Port upstream's finite headroom-limited mapping into a shared CPU reference helper
and MSL implementation. For unpremultiplied linear RGB `c`, set
`L=max(0,max(c.r,c.g,c.b))`, `H=max(1,finiteHeadroom)`, `K=min(1,0.75*H)`.
When `L>K`, multiply `c` by
`(K + (H-K)*(L-K)/((L-K)+(H-K)))/L`; otherwise keep it.
Repremultiply after mapping. At alpha zero return transparent zero. This mapping
applies once at the final display/export boundary, not between program passes,
not per CPU/GPU run, and not to the all-SDR fast path. Headroom-one HDR white is
compressed; all-SDR white remains unchanged, matching upstream's documented split.

Native EDR uses RGBA16Float, extended-linear-sRGB layer colorspace, and
`wantsExtendedDynamicRangeContent`; SDR uses a correctly matched encoded output.
Observe screen/headroom changes and remap cached output without stepping state.
Apple's [tone-mapping guidance](https://developer.apple.com/documentation/metal/performing-your-own-tone-mapping)
documents this EDR layer configuration and changing display headroom. The mapping
formula itself is Stana policy from the pinned upstream shader, not an Apple
prescription. PQ, HLG, arbitrary primaries, and monitor-calibration APIs are out of
scope; never label encoded sRGB bytes as a linear texture to avoid conversion.

### 12.4 Readback and diagnostics seam

Add a private native test capture operation for complete successful offscreen
tiles before final display mapping. Readback is asynchronous and owns buffers
through completion, returning revision, dimensions, scale, profile, and tightly
packed top-down premultiplied BGRA8 or RGBA16Float bytes. It neither calls
`waitUntilCompleted` on UI nor invents presentation evidence. This does not require
porting upstream's entire public window capture API.

Extend `WindowDiagnostics`/private statistics with selected device/profile,
working/output format, completed offscreen revision, state steps, compilation and
pipeline cache hits, resource upload bytes, CPU raster pixels, GPU leaf draws,
dirty/composited tile counts, current/peak/retired tracked bytes, bounded errors,
and revision-tagged prepare/compile/GPU/composite timings. Existing confirmed
revision keeps its strict meaning. Diagnostics are snapshots, not mutable native
objects, performance guarantees, or synchronization mechanisms.

## 13. File-level implementation map

| Location | Implementation responsibility |
| --- | --- |
| `modules/primitives/primitives.cppm` | Minimal semantic color token/resolver additions; existing identity/math contracts. |
| `modules/graphics/graphics.cppm` | Re-export new partitions; CPU command equality/helpers; mixed projection facade. |
| `modules/graphics/gpu.cppm` | GPU drawing, mesh, color and resource value declarations; partition of existing module. |
| `modules/graphics/program.cppm` | Program/pass/binding/error declarations; no native dependencies. |
| `src/graphics/drawing.cpp` | CPU validation/equality and non-template drawing algorithms. |
| `src/graphics/gpu.cpp` | Immutable GPU values, COW appends, checked geometry and payload validation. |
| `src/graphics/gpu_program.cpp` | Descriptor validation, stable DAG, version/resource replacement compatibility. |
| `src/graphics/shader_compiler.hpp/.cpp` | Private WGSL validation/inspection, portable profile, overrides and lowering boundary; no exported compiler types. |
| `cmake/ShaderCompiler.cmake`, `tools/bootstrap_tint.py` | Pinned verified Tint dependency, private build targets, offline/relocatable packaging. |
| `src/graphics/storage.hpp` | Private shared module-free immutable blob/array ownership used across bridge. |
| `modules/ui/ui.cppm`, `src/ui/mount.cpp` | Rename, aggregate declarations, scoped reactive mounts, clip defaults, callbacks. |
| `modules/core/core.cppm`, `src/core/document.cpp` | Node kinds/storage, classified drawing assignment, disposal and diagnostic queue. |
| `src/layout/layout.cpp`, `src/scene/scene.cpp` | Content rect, complete/patched CPU/GPU projection and coverage. |
| `src/application/application.cpp`, module interface | Color-mode transport, explicit native conversion, diagnostics adaptation. |
| `src/platform/native_contracts.hpp`, `gpu_contracts.hpp` | Private owning transport, typed mixed entries, native diagnostics/capture. |
| `src/platform/macos/renderer.*`, `presentation.*` | Integrate task-01 tile preparation, leases, scheduling and evidence. |
| `src/platform/macos/gpu_resources.*`, `gpu_programs.*`, `compositor.*` | Concrete native implementation described in sections 9–12. |
| `src/platform/macos/shaders/stana.metal`, `tools/embed_shader.py` | Tracked source and deterministic compiled-in resource generation. |
| `modules/testing/testing.cppm` | Production-backed typed canvas inspection, deliberate diagnostic test injection. |
| `tests/graphics/`, `tests/ui/`, `tests/platform/macos/` | Portable, native-offscreen, and window-server suites. |
| `tests/consumer/`, `CMakeLists.txt`, `tests/CMakeLists.txt` | Real public import/link/install fixtures and source registration. |
| `examples/canvas/main.cpp`, docs, README | Complete CPU/GPU/compute/state examples, migration and platform limits. |

Partitions are not separate public per-class modules. Keep dependency direction
primitives → graphics → core → UI → application; graphics never imports core/UI.
`NodeId` is already a primitive, so typed diagnostics do not require a cycle.
Native files remain Apple-only; do not make non-Apple builds locate Metal, Xcode,
AppKit, CoreGraphics, or `.metal` tooling. Install module sources/partitions through
the existing CMake file set. No public native header API is introduced.

## 14. Delivery milestones and acceptance gates

Each milestone is a reviewable implementation commit with its own evidence. The
final canvas task is complete only after all milestones pass; a rename, compiling
header, or rectangle-only native demo is not “full implementation.”

### M0 — Lock reference and prerequisite contracts

- [ ] Record the pinned source mappings above in implementation notes; add complete
  public consumer fixtures for CPU, primitives, mesh, WGSL program, and color mode.
- [ ] Pin/bootstrap/package private Tint and its dependencies; prove WGSL validation,
  inspection and native-source translation build on every existing host target.
- [ ] Add the public-interface audit: all shader/binding/configuration contracts are
  platform-neutral across direct and transitive exports; no native overload escape.
  Audit every exported module and its transitive imports, plus valid consumer
  examples; include negative fixtures for `GPUShader::metalSource`,
  `GPUShader::metalLibrary`, native binary ingress and backend-selection options.
- [ ] Reconcile task-01 exceptions explicitly; implement its persistent projection,
  damage, resource ownership, and presenter/evidence prerequisites, or reference
  their already-merged verified commits. Do not assume pending plans are code.
- [ ] Fix shared content-rectangle and optional clip-default semantics with focused
  regressions preserving non-canvas defaults.

**Gate:** no contradictory active rendering contract; dependency boundaries and
full/patch/clip tests agree before native hybrid work begins.

### M1 — Rename and complete CPUCanvas

- [ ] Hard rename all active API/diagnostic uses; add interaction and reactive
  `Property<Drawing>`; retain builder conveniences and command-stream capability.
- [ ] Implement command validation/equality, strong replacement guarantee,
  scoped subscription, padded origin, semantic colors, paint-only invalidation.
- [ ] Update migration docs and external consumer fixtures; prove old type rejected.

**Gate:** constant/signal/computed drawings work without remount or unrelated
measure/arrange work; explicit unclipping, nested clips, RTL and disposal pass.

### M2 — Portable GPU values and graph

- [ ] Implement all drawing/resource/program descriptors, factories, versions,
  payload limits, COW storage, `updatingResources`, and stable DAG.
- [ ] Add GPU node kind/aggregate and production-backed typed headless projection.
- [ ] Verify all values and examples compile/import on every existing build target.

**Gate:** sections 4–7 behavior tests pass without creating a native application
or GPU device. No shader source is executed by headless validation.

### M3 — Transport, resources, and ordered built-ins

- [ ] Implement owning bridge/cache, unified/discrete profiles, budgeted leases,
  shader embedding, built-in shapes/mesh/polyline, transparent CPU runs.
- [ ] Compose dirty final tiles from mixed inputs with exact retained order/clips;
  preserve CPU-only fast path and add independent full-reference reconstruction.

**Gate:** GPU/CPU/GPU overlap, alpha, borders, scrolling, move/remove/reorder,
scale and fractional tiles match full reconstruction; no Metal validation errors.

### M4 — Trusted programs and diagnostics

- [ ] Implement bounded compiler queue, WGSL validation/lowering/overrides, logical
  binding remapping and private native reflection, cache-hit replacement checks,
  render/indexed/compute passes and zero initialization.
- [ ] Route deduplicated diagnostics safely; preserve prior output on replacement
  failure and make unavailable initial programs transparent.
- [ ] Verify explicit capability failure and no implicit CPU fallback.

**Gate:** valid compute → render and multiple dependent targets produce expected
pixels; bad compiler/binding/capability cases leave a coherent previous scene.

### M5 — Temporal execution and scheduler integration

- [ ] Implement per-instance state bundles, commit-on-success, initializer/reset
  rules, latest desired coalescing, retry reuse, and output pinning.
- [ ] Handle stale compile completions, close/removal during work, GPU errors,
  bounded recovery and retired-resource accounting.

**Gate:** 1,000 successful steps produce the independent expected result; no
additional steps on retry/exposure/recomposition, no cross-instance state sharing,
and no candidate commit after failure or old-epoch completion.

### M6 — Extended color and final display mapping

- [ ] Add window modes, ingress conversion, linear composition/promotion, exact
  CPU/MSL mapping parity, EDR/headroom change handling and format-aware budgets.
- [ ] Implement asynchronous private complete readback and diagnostic counters.

**Gate:** HDR values above one survive offscreen capture; final SDR export and
mapping references agree; no double premultiplication or tile profile mismatch.

### M7 — Qualification, documentation, and package completion

- [ ] Run the complete portable matrix, sanitizer/policy/format/consumer gates.
- [ ] Run serialized native offscreen and real window-server suites on supported
  Apple Silicon and available Intel/AMD hardware; record device/profile evidence.
- [ ] Verify source/install/relocation builds with shader source embedded and no
  Swift checkout/current-directory dependency; portable WGSL examples run without
  app-side platform conditionals or native binaries.
- [ ] Publish complete canvas examples, migration, explicit shader portability,
  color/state semantics, limits, failure behavior, and measured counters.

**Gate:** all required tests below have concrete evidence, no unresolved rendering
correctness failures, and no implemented capability is overstated. Missing hardware
verification is recorded as pending, never converted to a pass.

## 15. Verification matrix

Tests must invoke production validators/projections/executors. Test-only services
may inject completion/diagnostic ordering, not fabricate production rendering.

### 15.1 Portable contracts (every current CI target)

| ID | Case | Required evidence |
| --- | --- | --- |
| P01 | Rename/import | CPU/GPU public examples compile; `Canvas` negative fixture rejects; no native headers required. |
| P02 | Reactive CPU/GPU | Constant, signal, computed; new drawing paints without remount; equal CPU/same GPU version is a no-op. |
| P03 | Independent values | CPU builder mutation after mount has no effect until assigned; GPU COW preserves old snapshots/copies. |
| P04 | Scope lifetime | Removal/close cancels subscriptions and late diagnostics; shared app values survive. |
| P05 | Shared groups | Interaction, enabled/inert, visibility, accessibility/lifecycle follow the existing engine. |
| P06 | Geometry | LTR/RTL asymmetric padding, default/explicit clipping, zero size, negative line delta, rounded corners. |
| P07 | Invalid CPU streams | NaN/Inf, derived overflow, negative size/width/radius, bad fonts, clip underflow/unbalanced stream; old value preserved. |
| P08 | GPU versions | Copies equal; independent identical values unequal; successful mutation/replacement fresh; failed mutation unchanged. |
| P09 | Mesh/polyline | Invalid index/count/float conversion, empty/one-point/duplicate/closed points, zero width, bounded counts. |
| P10 | Texture/state bytes | Every format, exact length, overflow before multiply, partial-auto targets, bad source peak, invalid half floats/alpha. |
| P11 | Binding structure | Missing/reserved names, stage mismatch, conflicting/duplicate logical group-bindings, shared-stage declarations, limits, immutable write, previous misuse. |
| P12 | Pass graph | Stable independent order, producer forward reference, cycles/self-cycles, multiple writers, unproduced read/output, all-pass validation. |
| P13 | Program replacement | Content update reuses pipeline/state; ABI/format/schema changes invalidate correctly; invalid replacement leaves old handle intact. |
| P14 | State model | Previous reads excluded from current DAG; initializer/resetKey equality; unwritten slot carry-forward; instance separation. |
| P15 | Incremental scene | Full/patched mixed trees equivalent; leaf update path-copies only affected route; CPU compatibility flattening explicitly excludes GPU pixels. |
| P16 | Diagnostics | Owning message, exact identity, bounded queue/dedup, stale/reordered delivery, callback removes/closes/reenters safely. |
| P17 | Color math | SDR token resolution, signed transfer, alpha-zero, premultiplied conversion, headroom mapping including H=1. |
| P18 | Policy/packages | Exhaustive enums/variants, hardening, installed module partitions/private compiler archives, offline source/install/relocation consumer, non-Apple unsupported execution. |
| P19 | Budget arithmetic | Peak live/candidate/retired accounting, shared-blob counting, limit boundaries, no unchecked count/product casts. |
| P20 | Platform-neutral public API | No native shader factories/formats/options/types or backend selection in any export; negative fixtures reject the superseded factories; public examples have no platform conditionals. |
| P21 | Portable shader compiler | Same WGSL render/compute fixtures validate and lower headlessly on all CI hosts; logical binding/override/layout/workgroup errors reject; native SDKs not required. |

Table-driven malformed-descriptor tests cover combinations, not just one happy
path per constructor. Use bounded randomized graph/input generation with fixed
seeds for cycle/order and overflow regressions; no unbounded fuzzing in normal CI.

### 15.2 Native offscreen correctness (no display claim)

| ID | Case | Required evidence |
| --- | --- | --- |
| N01 | Mixed painter order | CPU behind/between/above GPU with translucent overlap, normal/focus borders, nested clips and scroll. |
| N02 | Built-ins | Rect/rounded/ellipse, non-square ellipse, indexed gradient triangles, round caps/joins, duplicate/one-point lines. |
| N03 | Sampling/coordinates | 1×/2× and supported fractional scales, fractional origins, padding/RTL, tile-edge seams, offscreen geometry crop. |
| N04 | Custom programs | Same public WGSL source, all override types, logical binding remapping, indexed/instanced render, compute→render and multi-target graph; output exact reference. |
| N05 | Reflected bindings | Missing/wrong type/access and undersized buffer; invalid replacement rechecked on pipeline cache hit. |
| N06 | Transient initialization | Unwritten target/storage bytes are zero; samplers return zero outside extent; no stale-pool information leak. |
| N07 | Temporal correctness | 1,000 admitted input steps; exact expected counter/state; atomic multi-slot commit; initializer/reset, hidden/resume, independent instances. |
| N08 | No phantom steps | Retry, repeated final tiles, exposure, clip/opacity/movement, headroom and redraw reuse output without execution. |
| N09 | Replacement errors | Compiler/binding/capability errors preserve previous valid output; initial failure transparent; other drawing commands still appear. |
| N10 | HDR/ingress | Linear values above one, signed values, all encodings/formats/alpha combinations, promotion/demotion, SDR white preservation. |
| N11 | Full reference | Incremental dirty final tiles equal forced full mixed reconstruction and untiled reconstruction under one fixed AA profile. |
| N12 | Teardown/failure | Close/resize during compile/GPU, old-epoch result, simulated allocation/encoder/command failure, no premature release or state commit. |
| N13 | Device accounting | Unified path zero separate CPU upload; discrete path correct synchronization and explicit copy bytes; leases stay charged through retirement. |
| N14 | Packaging | Installed and relocated consumer validates WGSL and executes built-ins/application programs without source checkout, Swift runtime, network, or CWD shader files. |

Use exact bytes for identical-path copy/order/state fixtures; permit at most two
8-bit channel units for independently rasterized antialiased edges. For half-float
color comparisons use `max(0.002, 0.002 * abs(expected))` per channel and exact
integer-representable temporal counters. Compare interiors exactly where possible;
never use a whole-image loose tolerance that hides missing/incorrectly ordered
content. GPU hardware AA differences are profiled and tested separately from
algorithmic parity; forced sample-one runs enable deterministic comparisons.

Drive the 1,000-step offscreen regression through the production offscreen executor
with sequential success observations, not by disabling the application presenter's
unconfirmed-history limit. It tests execution/state, while the separate visible
suite tests joined display evidence. A window returning only zero timestamps must
still reach its normal bounded suspension/recovery policy.

### 15.3 Native presentation and failure-state evidence

- [ ] Visible real-window CPU/GPU content gets GPU-success plus positive timestamps
  for exact expected attempt/tile sets before geometry confirms.
- [ ] Zero/missing/duplicate/late/old-epoch callbacks cannot confirm or erase newer
  ambiguity; return-to-old-bounds and mixed-frame cases preserve input gates.
- [ ] Starved drawables acquire no new compiler/GPU state work and stay bounded;
  hidden/minimized windows pause demand; resume reconstructs current content.
- [ ] Scale/profile/screen/attachment changes replace the appropriate epoch and
  reject stale callbacks without losing immutable resource ownership.
- [ ] Recovery is bounded, reports execution failures honestly, and never silently
  selects CPU fallback or turns readback success into presentation evidence.

Run real-window tests serialized, because focus/display state is process-global.
Keep logged-in/window-server requirements distinct from offscreen Metal-device
requirements; do not skip native work and describe headless tests as GPU evidence.

### 15.4 Performance acceptance

Record Release results with device/OS/compiler/profile, viewport/backing scale,
command counts, warmup, and at least p50/p95 over 1,000 updates. Use counters as
hard gates and timings as measured evidence, not a universal FPS promise.

| Workload after warmup | Hard work-reduction gate |
| --- | --- |
| Equal CPU/same GPU assignment | No new scene, layout, upload, pipeline build, program dispatch, or final damage. |
| One CPU rectangle color update | No remount/measurement/arrangement; unchanged unrelated GPU output reused. |
| One GPU leaf change among 1,000 retained leaves | No unrelated CPU reraster/mesh upload/pipeline compile; only affected route and overlap tiles rebuilt. |
| Program uniform replacement | Binding revalidation and necessary input upload, zero pipeline recompilation/state reset. |
| Unchanged mesh in 1,000 updates | One upload per device/mesh version while retained, no per-update vertex-array copy. |
| Presentation-only retry | Zero extra raster/upload/compile/program/state work. |
| Pure canvas translation/opacity | Cached content reused; damage is old/new coverage; zero extra temporal step. |
| Headroom change | Final mapping only; zero CPU reraster/temporal step. |
| 10,000 drawing replacements and delayed completions | Tracked memory/queues/cache/history stay within bounds; retired owners eventually release. |
| CPU-only task-01 fixture | Existing zero-unnecessary-layout/tile/upload gates remain valid on its unified profile. |

Do not declare O(1) for copying a mutated shared command vector or rerendering a
large affected canvas. COW avoids copying on value transfer, not on every edit;
large meshes and outputs have real upload/fill cost. Optimize after measured
profiles without removing validation or weakening ordering.

### 15.5 Commands and evidence to record during implementation

Existing commands remain authoritative:

```sh
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug
cmake --build build/debug --target format-check
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
cmake --preset asan-ubsan
cmake --build --preset asan-ubsan --parallel
ctest --preset asan-ubsan
```

Windows uses its supported `asan` preset, not `asan-ubsan`. Retain current
Linux/Windows/macOS-x64/macOS-arm64 CI entries, strict source policy, external
consumers and ICU/toolchain requirements. Register proposed native suites as
`stana.native.canvas.offscreen` (Metal device required) and
`stana.native.canvas.presentation` (logged-in window server), enabled by the
existing explicit native-test option. Run them with `MTL_DEBUG_LAYER=1` and
`MTL_SHADER_VALIDATION=1` where the device/tooling supports those modes; report
unsupported validation instrumentation separately, never disable failing checks.
Add `STANA_NATIVE_OFFSCREEN_TESTS`, default OFF, to register the offscreen suite
without enabling GUI tests. `STANA_NATIVE_TESTS` registers both suites. Neither
option is enabled implicitly on headless runners.

The final implementation evidence file records actual commands, outcomes,
device/profile, native validation, captured reference comparisons, sanitizer
results, performance counters and remaining platform limitations. No fabricated
hashes, screenshots, timing numbers, or promises of untested Intel/discrete output.

## 16. Research sources and decision trace

Web research performed 2026-11-04. Primary documentation establishes API/lifetime
facts; resource limits, architecture, mapping policy, C++ names, and milestone
choices are Stana design decisions in this plan, not claims made by vendors.

| Primary source | Fact checked / consequence |
| --- | --- |
| [W3C: WGSL](https://www.w3.org/TR/WGSL/) | Platform-neutral application shader syntax; Stana selects a bounded supported profile, logical bindings, overrides and workgroup dispatch. |
| [W3C: WebGPU coordinates](https://www.w3.org/TR/webgpu/#coordinate-systems) | Distinguish homogeneous clip position, NDC, framebuffer and texture coordinates independently of the chosen native adapter. |
| [Dawn: Architecture](https://github.com/google/dawn/blob/main/docs/dawn/overview.md) | Tint translates WGSL to native shader languages; use translation privately rather than exposing backend payloads. |
| [Dawn: CMake](https://github.com/google/dawn/blob/main/CMakeLists.txt) | Selectable Tint reader/writer libraries enable a private compiler-only dependency rather than a full graphics runtime. |
| [Dawn: CMake quickstart](https://github.com/google/dawn/blob/main/docs/quickstart-cmake.md) | C++/CMake dependency and packaging workflow; Stana additionally requires immutable pins, verified bootstrap and offline consumers. |
| [Apple: Resource synchronization](https://developer.apple.com/documentation/metal/resource-synchronization) | Tracked classic-queue resources differ from Metal 4 synchronization; select explicit classic executor constraints. |
| [Apple: Resource fundamentals](https://developer.apple.com/documentation/metal/resource-fundamentals) | Resource storage and hazard modes are backend details; do not leak native resources through public values. |
| [Apple: Intel/AMD storage modes](https://developer.apple.com/documentation/metal/choosing-a-resource-storage-mode-for-intel-and-amd-gpus) | Unified and discrete storage/synchronization differ; preserve correctness profiles, not blanket zero-copy claims. |
| [Apple: MTLTextureBinding](https://developer.apple.com/documentation/metal/mtltexturebinding) | Reflection exposes texture type/data shape; validate actual shader bindings rather than only names. |
| [Apple: Shader Validation](https://developer.apple.com/documentation/xcode/validating-your-apps-metal-shader-usage) | Runtime shader errors need native instrumentation; descriptor safety is not arbitrary-shader isolation. |
| [Apple: Function specialization](https://developer.apple.com/documentation/metal/using-function-specialization-to-build-pipeline-variants) | Specialization/pipeline creation belongs outside frame-critical work and needs compatible versioned caches. |
| [Apple: presentedTime](https://developer.apple.com/documentation/metal/mtldrawable/presentedtime) | Zero can mean unpresented/dropped; GPU completion alone cannot confirm geometry. |
| [Apple: CPU/GPU synchronization](https://developer.apple.com/documentation/metal/synchronizing-cpu-and-gpu-work) | Resource reuse must respect GPU completion; keep in-flight ownership separate from presentation observations. |
| [Apple: Own tone mapping](https://developer.apple.com/documentation/metal/performing-your-own-tone-mapping) | EDR pixel format/colorspace/layer opt-in and changing screen headroom require final mapping policy. |
| [Apple: Texture sample count](https://developer.apple.com/documentation/metal/mtltexturedescriptor/samplecount) | Query sample support and match attachment/pipeline counts; no assumed MSAA configuration. |
| [Khronos: Synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html) | Portable graphs must preserve memory dependencies/readback readiness for future native adapters. |
| [Microsoft: D3D12 resource barriers](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12) | Render/read/write resource-state transitions belong to adapters, not a Metal-specific public ordering assumption. |
| [WG21: P0323R12](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0323r12.html) | C++23 value/error results fit fallible immutable factories without invalid placeholder objects. |
| [C++ Core Guidelines: RAII](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-raii) | Resource lifetime and bounds-aware interfaces remain mandatory alongside performance work. |

### Final completion checklist

- [ ] CPUCanvas hard rename, full reactive CPU behavior and shared properties.
- [ ] GPU drawing primitives, meshes, color variants and immutable values.
- [ ] Validated resources/render/compute programs, reflection, replacement caches.
- [ ] Ordered incremental CPU/GPU composition and supported memory profiles.
- [ ] Correct per-instance temporal state, bounded scheduling and safe teardown.
- [ ] Diagnostics/failure fallback contracts without silent renderer fallback.
- [ ] Extended-linear/HDR composition, texture ingress and one final mapping.
- [ ] Portable CI, native offscreen and exact presentation gates with evidence.
- [ ] Public examples, rename migration, platform limitations and relocated packages.
- [ ] Platform-neutral shader/interface audit, private compiler packaging and portable WGSL fixtures.
- [ ] Cross-task specifications reconciled; all implementation evidence recorded.

Only then change `status` to `done`. Keep `plan: final` to distinguish the settled
specification from execution status; implementation checkboxes are not completed
by the existence or commit of this plan.
