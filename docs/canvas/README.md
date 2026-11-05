# CPUCanvas and GPUCanvas

Import `stana` for authoring; import `stana.testing` only for deliberate headless
inspection and `validateGPUProgram`. Native execution is currently macOS 26+.
Linux/Windows build the same public values, dependency plans, WGSL validation and
headless retained scenes; they do not emulate GPU pixels or create native windows.

## Migration and geometry

Rename `Canvas` to `CPUCanvas`; the old spelling is intentionally rejected.
`Drawing`, its command vector, and `fill`/`stroke` conveniences remain. Prefer
`fillRect`/`strokeRect` for clarity. `CPUCanvas::drawing` is now `Property<Drawing>`:
constants, `Signal<Drawing>` and computed properties mount once and replace paint
without remount/layout. CPU values compare structurally; modifying an app-side
builder does nothing until it is assigned. Streams validate before publication;
invalid input throws `std::invalid_argument` and leaves retained paint unchanged.

Both canvases use the direction-resolved **padded content origin** in logical
points. Supply an explicit frame; command geometry does not determine intrinsic
size. CPUCanvas/GPUCanvas/ScrollView clip to their rounded outer bounds by default;
ordinary containers preserve their existing unclipped default. Explicit
`.appearance = {.clip = false}` disables that outer clip. GPU output is still
cropped to its finite content rectangle. Drawing commands do not create semantic
children: supply accessibility labels and meaningful equivalent text.

## GPU values and programs

`GPUDrawing` contains ordered rects, ellipses, meshes, round polylines and programs.
Copies share a version; a successful append creates a new version and detaches
shared storage. Independently authored identical drawings are not equal. A moved
from drawing is reusable and empty. Mesh/source/buffer/texture/state factories
copy or take ownership of inputs and return `std::expected<..., GPUError>`; check
results. No raw GPU handles, native payload factory or backend option is public.

Shaders are owned **WGSL strings**. Descriptors use logical group/binding indices,
not native slots. Uniform/storage bytes must follow WGSL host-shareable layout;
applications provide viewport/time data explicitly if wanted. Vertex inputs use
`vertex_index`; index buffers contain tightly packed little-endian `uint32_t`.
See the [complete example](../../examples/canvas/main.cpp) and its
[application resource](../../examples/canvas/field.wgsl) for explicit uniform
packing, indexed mesh, compute-to-render, state and checked replacements.

Targets may independently specify zero width/height, resolving that axis to the
canvas backing extent. Pass order is a stable topological sort of current-step
resource producers. All passes validate, including unreachable passes. Exactly
one producer may write a transient/state output; same-pass current read/write
aliases, missing producers and cycles reject. Immutable inputs cannot be written.
Previous-state reads do not form current-step dependencies. Initial storage and
unwritten target pixels are zero. Sampling is clamp-to-zero outside texture extent.

`GPUProgram::create` validates descriptor structure; it does **not** assert shader
compilation or native capability. `validateGPUProgram` additionally runs production
WGSL validation/inspection/lowering headlessly. Native pipeline reflection and
replacement binding sizes still validate at execution. Trusted app shaders can be
expensive; this API is not an untrusted-code execution sandbox.

`updatingResources` creates a new immutable input version. Compatible resource
contents reuse pipeline/state identities; incompatible schema/layout/format resets
the relevant identity. Native compilation is serial per device, with one latest
waiting request per window and bounded caches/admission. No native compilation
runs inline in painting. Failed/unavailable initial programs are transparent;
failed replacements preserve the previous valid output, with other commands
remaining visible. Capability failures never silently select a CPU renderer.

## Temporal state

State belongs to a mounted canvas occurrence/program slot, not the app-side handle.
Two occurrences do not share history. Every admitted new input step reads committed
previous state and writes separate candidate state. The entire state bundle commits
only after successful offscreen GPU completion in the current epoch. Failure,
removal, close or stale-epoch completion cannot publish candidates. Equal initializer,
format/extent and reset key preserve compatible history; a changed reset key resets.
Unwritten state carries forward. Hidden occurrences pause, rather than dispatching
an animation loop. Recomposition, movement, opacity, presentation retry, exposure
and final headroom mapping reuse output and do not step state. No implicit time,
frame callback or display-link animation service is introduced.

Offscreen success is distinct from display evidence. Existing bounded presentation
joins exact batch/attempt identities, GPU success, transaction issuance and a
positive native proof-tile timestamp before confirming geometry. Readback alone
never confirms display. Ready offscreen output and its geometry remain pinned
through retries; drawable starvation does not repeatedly step the program.

## Color

`Color` is encoded SDR; semantic `text`, `secondaryText`, `background`,
`surface`, `border` and `accent` resolve against retained
environment appearance/contrast. `ExtendedColor` is **straight scene-linear
extended-sRGB**, with finite RGB in binary16 range and alpha in [0,1]. Mesh vertices
and built-ins are packed/premultiplied privately once; public double-layout objects
are not copied as a native ABI. Texture bytes declare format, encoding,
premultiplication and source peak. `rgba16Float` byte order is explicitly
little-endian IEEE binary16; nonfinite values/invalid alpha reject. Scalar r8 inputs
cannot carry nondefault color metadata. Negative extended RGB is allowed.

Mixed output promotes to half-float linear composition when required. CPU bitmap
runs remain transparent BGRA8 and convert at composition. Signed sRGB transfer and
alpha-zero canonicalization avoid double premultiplication. One final hue-preserving
luminance/headroom map is applied at presentation; private captures are before this
map. `Window::renderingColorMode` follows `content` in aggregate order and defaults
to `automatic`; `sdr` forces SDR output, `extendedLinear` requests a linear surface.
Actual output capability/headroom selects the final attachment. Headroom/screen
notifications remap cached content without reraster/temporal steps. Hardware EDR
transitions and Intel/AMD output remain separately qualified, not assumed.

## Limits and ownership

Portable limits are checked before products/conversions: 16,384 texture axes,
256 MiB aggregate payload, 16 MiB source, 65,536 drawing commands, 1,048,576
vertices/polyline points, 3,145,728 mesh indices, 128 resources/passes/constants,
128 distinct logical coordinates per pass, four groups and 1,024-byte names.
Compute workgroup/count products are checked against the portable invocation
profile. Native device limits can be tighter. Invalid descriptors report errors;
allocation failure remains an exception. Builder invalid arguments throw.

Tracked live/candidate/retired resource payload is bounded to 256 MiB/window and
512 MiB/device. Final format and 4x MSAA resolve scratch affect admission; fallback
uses a consistent 1x profile, exposed in rendering diagnostics. These ledgers are
not a total-process/driver-memory bound: CPU snapshots, compiler internals, native
allocation padding, Core Text and compositor swapchain overhead are separate.
Managed discrete writes/readback are explicitly synchronized; zero separate CPU
upload is a unified-device claim only. Immutable resources stay owned through GPU
completion, including partial-encoding failures and retired window epochs.

## Diagnostics and build

`GPUCanvas::onError` receives an owning `GPUDiagnostic` on the UI executor, never
inside a compiler/GPU callback or mutable encoder traversal. Delivery validates
occurrence, drawing, program and epoch identities; duplicate messages coalesce and
queues/messages are bounded. The callback can remove/close/reenter its document.
No Stana reporting service or application logging policy is imposed.

Provision private Tint explicitly before normal configure (no CMake downloads):

```sh
python3 tools/bootstrap_tint.py --compiler /path/to/clang++
cmake --preset debug
cmake --build --preset debug --parallel 3
ctest --preset debug
```

Use `--offline` after verified archives are cached; select the matching sanitizer
with `--sanitizer=address-undefined` or Windows `address`, and matching Windows
Debug/Release CRT configuration with `--config`. Pinned revisions, licenses and
compiler contract ship with installed static packages. Consumers rebuild module
sources/BMIs with the documented toolchain; private Tint headers are not a public
API dependency. Source/install/relocation tests validate WGSL without a Swift
checkout, network access or shader lookup in the current working directory.

Native tests are opt-in: `STANA_NATIVE_OFFSCREEN_TESTS=ON` requires a Metal device;
`STANA_NATIVE_TESTS=ON` also requires a logged-in window server. Run with
`MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1`; GUI tests serialize. See
[actual evidence and remaining qualification](implementation-evidence.md).
