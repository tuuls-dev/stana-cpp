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
| `.todos/` | Plans, progress, acceptance gates and verification evidence |

Dependencies point inward: primitives → reactive → retained core; UI mounts
through the core, projections consume core state, native adapters consume owning
value snapshots. Core never imports the authoring facade or native frameworks.
Testing hosts reuse the engine rather than implement a second UI runtime.

Directories represent implemented responsibilities, not speculative platforms.
Do not add placeholder Windows/Linux backend trees, generic utility dumping
grounds, per-class public modules or circular umbrella imports. Keep templates
small; move expensive non-generic work into compiled implementation units.

See [coding conventions](../coding-conventions.md) and [deferred symbols](symbols.md).
