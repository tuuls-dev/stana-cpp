# Architecture and source organization

Stana separates generic authoring descriptions from a compiled retained engine.
Public consumption is named-modules-only; private native headers are not an
alternative public API.

| Location | Ownership |
|---|---|
| `modules/<subsystem>/` | Named interfaces and the templates consumers instantiate |
| `modules/stana.cppm` | Convenience public re-exports only |
| `src/` | Compiled implementation units, grouped by subsystem as they are added |
| `src/platform/macos/` | AppKit/Core Text/Core Graphics/Metal adapters, ARC ownership |
| `tests/<subsystem>/` | Behavioral/regression tests for that subsystem |
| `tests/consumer/` | External source/install/relocation consumer |
| `examples/counter/` | Small public-API-only native application |
| `tools/`, `cmake/` | Build policy, dependency setup and distribution |
| `.tasks/` | Plans, progress, acceptance gates and verification evidence |

Dependencies point inward: primitives → reactive → retained core; UI mounts
through the core, projections consume core state, native adapters consume owning
value snapshots. Core never imports the authoring facade or native frameworks.
Testing hosts reuse the engine rather than implement a second UI runtime.

Directories represent implemented responsibilities, not speculative platforms.
Do not add placeholder Windows/Linux backend trees, generic utility dumping
grounds, per-class public modules or circular umbrella imports. Keep templates
small; move expensive non-generic work into compiled implementation units.

See [coding conventions](../coding-conventions.md) and [deferred symbols](symbols.md).

## Incremental publication and native ownership

`Document` merges typed dirty domains and direct-child routes. Layout caches retain
complete available/assigned keys and service generations. A settled commit stages
immutable scene and geometry roots before promoting projection caches. Failed
inputs retain the previous complete publication and do not spin without a changed
input/service stamp. Removed occurrences leave lookup before owning scope cleanup.

`src/scene/geometry.cpp` is the private persistent AVL implementation; scene history
and admitted presentation pins share roots. Flat compatibility views are lazy.
`src/application/application.cpp` pairs native/source branches by occurrence and
shares unchanged local transport. Weak document callbacks cannot resurrect closure.

`src/platform/tiles.cpp` owns portable checked grid/budget/damage arithmetic.
`src/platform/scheduler.cpp` is the production single-batch state machine with an
injected monotonic clock. `macos/renderer.mm` owns immutable prepared Core Text/path
resources and CoreGraphics contexts over shared Metal buffers. `presentation.mm`
owns tile layers, independent GPU-read owners, scheduled main-thread presentation,
retries, recovery and draining. `native_host.mm` only adapts AppKit lifecycle/input.

Raster content and confirmed display are separate baselines. GPU completion alone
never acknowledges geometry. Exact epoch, attachment, attempt and tile identities qualify evidence. The native
backend's approved proof subset is the first submitted tile, joined with the
complete GPU batch and its issued transaction; portable declared multi-tile sets
remain strict. Other native tile timestamps are diagnostic only; surface replacement gates input immediately. Source pixels are
immutable throughout a GPU read. Independent submission resources do not own their
command buffer; late callbacks can release resources without touching a closed host.
The full reference replay is a private test operation, not a fallback host.
