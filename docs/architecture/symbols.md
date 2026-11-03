# Deferred: build-time compiled symbols

Status: design only, explicitly excluded from this implementation pass.
Reference: Swift Stana SVG V1 and symbol playback contracts at
`a31555c779c3bfd4f8222606996e4a5d6eadb919`.

## Recommended architecture

A host-only C++ compiler accepts SVG source assets and emits typed module source.
A CMake helper schedules generation before module scanning and adds generated
interfaces to a consumer module file set. Cross builds use a host-built compiler,
never a target executable. No Swift macro/plugin or runtime SVG parser is needed.
Generated assets must be usable by external packages via `import stana;` without
exposing compiler or native dependencies.

The compiler validates the existing SVG V1 subset, normalizes paths/transforms/
paints and emits immutable shared geometry with a source-versioned bridge.
Retain the upstream finite-number, XML namespace, external-resource rejection,
1 MiB input, 4096 element, depth 64, 65536 path command and numeric-budget rules.
Report source file/line/column diagnostics; never read external resources.
Do not silently broaden supported SVG/CSS or turn generated data into a runtime
import format. Semantic equality ignores comments, IDs and source names.

A future Symbol aggregate will take fixed typed Property inputs for asset, size,
color, optional stroke width and playback. Mount-local deterministic playback
supports rotate/pulse/bounce, one-shot request generations and loops. Lifetime,
host visibility and reduced motion govern clock admission. Animation affects
paint/ink damage only, not measurement, hit targets or semantic bounds. Assets
are shared; clocks are not. A label opts into image semantics; no label is
decorative.

The macOS worker pipeline prepares bounded native paths, caches them in a bounded
LRU and feeds the normal CPU tile/Metal compositor. In-flight owners survive
cache eviction. Generation-tagged errors are delivered after commit and stale
callbacks are discarded. Follow upstream retry and one-shot recovery semantics.

## Future acceptance gates

- SVG V1 positive/negative corpus and all compiler budget boundaries.
- External-package generated-module consumption and cross-compilation.
- Geometry normalization/equality and immutable ownership.
- Independent instance clocks, request deduplication and reduced motion.
- RTL, baseline, ink overflow, damage, accessibility and deterministic time.
- Native preparation budgets, cache eviction, cancellation and failure recovery.

No placeholder Symbol public API is introduced by this document.
