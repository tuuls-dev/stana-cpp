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
