# Task 03 implementation evidence

Scope: `.tasks/03-cpu-gpu-canvas.md` only. Implementation is tracked separately
from the settled specification; unchecked gates are not claimed as complete.

## Reference and foundation

- Starting C++ commit: `ce77be9` (task-01 implementation merged in PR #1).
- Swift reference: `../stana-ui`, specification pin
  `a31555c779c3bfd4f8222606996e4a5d6eadb919`. No build/runtime dependency on Swift.
- Reference mapping: `STNCanvas.swift` / `STNGPUCanvas.swift` for scoped properties
  and clip defaults; `GPUDrawing.swift` for immutable versions and ordered commands;
  `GPUProgram.swift` for descriptor, DAG and state behavior; `Color.swift` for the
  semantic palette. Native scene/program/shader sources guide execution parity.
- Existing persistent projections, sparse invalidation, tiles, raster leases and
  joined presentation evidence are in `5e82c9d`. They are foundations, not evidence
  that the new hybrid renderer has passed its own native gates.

## Qualification

Commands and actual outcomes are appended at each verified checkpoint. Native
readback, native display, portable descriptor tests and hardware profiles are
reported separately. Other-platform builds and Intel/AMD execution require their
own evidence; this Apple Silicon machine cannot establish those passes.

### CPU checkpoint

- `cmake --preset debug` and `cmake --build --preset debug --parallel 3`: pass
  with unchanged first-party warnings, source policy and clang-tidy enforcement.
- `build/debug/tests/stana-cpu-canvas`: pass (production reactive mount/scene).
- `build/debug/tests/stana-ui-contracts`: pass after updating accent expectations
  to compare resolved paint values, not unresolved authoring tokens.
- Focused regression distinguishes outer node motion from padded local origins
  in RTL; environment direction invalidates local text/canvas paint as needed.
- `ctest --preset debug -E 'stana.consumer|stana.native'`: 11/11 pass
  (73.68 seconds; includes production CoreGraphics raster tests, not GPU pixels).

### Portable GPU / compiler checkpoint

- Official Dawn/Tint pin: `cb91d23e247042a2a5490c27d9b06300db91afef`;
  exact Dawn, Abseil and SPIRV-Headers archive digests are recorded in
  `tools/bootstrap_tint.py`. SPIRV-Headers is an upstream configure requirement,
  not an enabled application shader-ingress API.
- `python3 tools/bootstrap_tint.py --compiler /opt/homebrew/opt/llvm@22/bin/clang++ --offline`:
  pass using verified cached archives. Reader, validator/inspector, IR and MSL
  writer build without Dawn graphics backends. Compiler archive: 13 MiB on arm64.
- `cmake --preset debug`, `cmake --build --preset debug --parallel 3`: pass.
- `ctest --preset debug -R 'stana.gpu-values|stana.shaders|stana.cpu-canvas|stana.incremental|stana.ui|stana.canvas-api'`:
  6/6 pass (3.98 seconds). The shader suite invokes production Tint parsing,
  inspection and lowering, including WGSL render/compute, typed overrides,
  resolved workgroup size, logical remapping, robust runtime-array size metadata,
  and cached-pipeline binding-size revalidation. It creates no native device.
- Owning module-free transport retains shared source/byte/index owners and packed
  immutable meshes. Typed TestHost inspection exposes mixed painter order and
  explicitly excludes GPU pixels from CPU compatibility flattening.
- Native GPU execution, cross-host compiler builds, installed/relocated GPU
  consumers and native color/presentation gates remain unqualified at this checkpoint.

### Native executor checkpoint (offscreen only)

- Hardware: Apple M1 Pro; macOS 27.0.1 (26A434), arm64; Clang 22.1.8;
  macOS deployment target remains 26.0. No Intel/AMD execution claim.
- Strict debug build: pass, including Objective-C++ analysis. Combined allocation
  ledgers retain live/candidate/retired ownership; admitted peak reservations split
  into individual owner charges rather than staying charged to unrelated resources.
- `ctest --preset debug -E 'stana.consumer|stana.native'`: 14/14 pass
  (83.92 seconds), including existing CPU raster regressions.
- `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 build/debug/tests/stana-canvas-offscreen`:
  pass with actual Metal API Validation and GPU Validation enabled. The sandbox
  cannot enumerate a device; qualification ran with native device access.
- The native test uses 1,000 public validated `updatingResources` program versions,
  the production owning converter, Tint/native pipeline compilation and reflection,
  production dependency encoding, successful asynchronous GPU completion, and
  half-float readback of each independently expected integer counter. It creates
  no application window and claims no presentation evidence.
- Built-in shader source is deterministically embedded; no runtime shader file or
  Swift checkout lookup. First real shader compilation caught/fixed an MSL
  reserved-word local in the built-in mesh function.
- Ordered composition and presentation integration are the next checkpoint, not
  established by the temporal-only offscreen test.

### Mixed compositor / native presentation checkpoint

- Strict debug build passes with production ordered composition, transparent CPU
  operation contributions, rounded clip stacks, primitives/meshes/polylines,
  complete offscreen final tiles, and one final linear display mapping integrated.
- Production mixed CPU/GPU/CPU interior pixels, premultiplied overlap, HDR values
  above one, and presentation-only reuse pass at 1x, 2x and 1.25x in the native
  offscreen suite with both Metal validation modes enabled.
- `ctest --preset debug -R '^stana.native.canvas.presentation$'`: pass (0.98 s)
  after adding a real public WGSL compute/state program to the window fixture.
  Actual positive proof-tile timestamps joined the exact GPU-success attempts;
  the trace includes zero-time retry, subsequent positive confirmation, a GPU-only
  drawing update, and a paint-equivalent anchor. No extra temporal step or
  unrelated CPU raster/layout work was observed for those updates.
- Final mapping was exercised through a real SDR output attachment with extended
  linear intermediate content. EDR hardware/headroom transition qualification is
  separate and is not established by the SDR presentation gate.
- `ctest --preset debug -R '^stana.consumer$'`: pass (163.46 s), covering source,
  installed and relocated module consumers plus removed/native-ingress negatives.
- Native ready packets pin one geometry root beyond the short history and remain
  stable through drawable starvation/retry; old-epoch offscreen completions retire
  ownership without committing state. These paths have explicit regression seams;
  broader injected-failure and long-running memory/performance gates remain pending.
- The CPU-only direct raster-buffer path remains; discrete-device managed writes
  are explicitly notified/accounted, without asserting untested Intel/AMD output.
