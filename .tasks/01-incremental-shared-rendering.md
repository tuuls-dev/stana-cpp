---
status: done
---
# Incremental core, shared raster buffers, and tiled Metal presentation

This is the implementation specification for replacing Stana's full-window
rendering path. The declarations and algorithms below are chosen target contracts,
not sketches and not claims that these capabilities already exist. Implement the
ownership, ordering, cache, failure, and verification rules together; do not replace
them with a second UI runtime or an undocumented drawable-preservation assumption.

**Execution status:** implementation and qualification in progress. The old upload
path is removed; no fallback/toggle is retained. See [recorded evidence](../docs/rendering-evidence.md).

**User-approved raster-gate amendment (2026-11-04):** full-viewport versus tiled
CoreGraphics replay may differ only at a reference edge (a nonuniform 3×3 coverage
neighborhood), with RGB error ≤32/255, exact alpha, exact uniform regions, and
≤0.25% differing pixels per fixture. This supersedes the byte-exact full-viewport
comparison statements below. Incremental assembled tiles versus fresh target tiles,
damage/erasure, geometry, buffer safety, and presentation evidence remain strict.
The unchanged global pixel phase remains required. Fixed-gutter experiments
(2/32/64 pixels) did not provide a general exact-pixel solution and were removed.
The final path retains no raster gutter or antialiasing-mode change.

**User-approved hardware gate amendment:** real positive-timestamp, sparse update,
resize, attachment, occlusion, two-window and close gates run at onscreen sizes.
4K/8K resource/transfer gates use actual full-size offscreen Metal buffers/textures.
Physical backing-scale switching and 4K/8K display confirmation remain explicitly
unqualified pending suitable displays; CPU/GPU references cover 1× and 2×.

**User-approved native evidence amendment:** native acknowledgement joins success
of the one command buffer containing every tile copy, an issued batch transaction,
and a real positive timestamp from the first submitted tile. Other tile times are
diagnostic, never fabricated proof. Native admission declares that actual proof
subset; copied/presented tile sets are recorded separately. The public tracker and
explicit composite test seams continue to require every tile in their declared
sets. This supersedes native all-copied-tiles timestamp requirements below and is
not proof of per-tile display or scanout-atomic multi-layer presentation.

Geometry records additionally own shared ordered child identities and route
generations: an unsubmitted reorder must continue hit testing the acknowledged
order, not the current mutable child vector.

Reading order:

1. [Intent, boundaries, and success](#intent-boundaries-and-success).
2. [Current implementation and ownership](#current-implementation-and-ownership).
3. [Core invalidation and layout](#core-invalidation-and-layout).
4. [Immutable projections and geometry](#immutable-projections-and-geometry).
5. [Native transport and preparation](#native-transport-and-preparation).
6. [Shared storage and tile rasterization](#shared-storage-and-tile-rasterization).
7. [Tiled presentation and scheduling](#tiled-presentation-and-scheduling).
8. [Evidence, input, and recovery](#evidence-input-and-recovery).
9. [Verification, delivery, and completion](#verification-delivery-and-completion).

## Intent, boundaries, and success

### Decisions already made

| Decision | Committed behavior |
| --- | --- |
| Incremental scope | Optimize retained-core layout and scene generation as well as native rasterization. |
| CPU/GPU handoff | CoreGraphics writes directly into shared Metal buffer memory. |
| Final-copy scope | Avoid copies for unchanged tiles through tile-level presentation. |
| Raster executor | Keep preparation, CoreGraphics drawing, and mutable renderer state on the UI executor. |
| Native hardware | Apple Silicon, macOS 26+, Apple-family unified-memory Metal device. |
| Portable engine | Remains buildable and testable headlessly on the currently supported build targets. |
| Public changes | Preserve existing authoring and inspection APIs; add only contracts needed for testing, diagnostics, and composite evidence. |
| Drawing technology | CoreGraphics/Core Text continue to produce pixels; no shader-based UI renderer. |
| Specification reference | Use the architecture, concrete declarations, edge cases, and acceptance-gate detail of `.tasks/00_omnibus.md`. |

### Intended improvement

Today a small label change can cause a document-wide measure/arrange/paint traversal,
a full native scene conversion, full-window CPU rasterization, a bitmap upload,
and a full-window GPU texture copy. The replacement must reduce work at each of
those boundaries rather than merely replace one allocation API.

```text
retained mutation
  → classify invalidation and settle the transaction
  → remeasure/rearrange the affected dependency frontier
  → path-copy changed immutable scene branches and geometry records
  → convert/prepare changed native branches
  → compare against raster and confirmed-display baselines
  → map old/new ink damage to physical-pixel tiles
  → clear/replay dirty tiles directly into shared MTLBuffers
  → blit presentation-dirty buffers into fresh tile drawables
  → present that batch through a Core Animation transaction
  → acknowledge geometry after GPU success and every required tile timestamp
```

For an in-tile foreground change after warm-up, success means zero measurement,
zero arrangement, no paint rebuilding outside the affected occurrence, no native
preparation of unrelated branches, one tile raster/copy, zero bitmap-upload bytes,
and no new shared-buffer allocation. Ancestor branch path copies are expected.

For same-size text replacement, success permits remeasurement of the changed text
and affected container aggregates, but not unrelated text measurement or unrelated
sibling arrangement. If text changes size, layout-dependent siblings must move.

### Non-goals and honest limits

- Do not add a raster worker, display-link animation loop, shader-based primitive
  renderer, native Windows/Linux host, general graphics abstraction, or service
  locator. Use the existing modules and application composition root.
- Do not add sub-tile partial repainting, retained GPU shape geometry, scroll blits,
  occlusion subtraction, glyph atlases, or a general spatial-index package.
- Do not silently implement the backlog's changed sizing/allocation, wrapping,
  typography, editor, shadow, or focus-style semantics. Preserve current pixels
  and layout except where this document explicitly changes cache or evidence APIs.
- A dirty tile is fully repainted. Unchanged primitives overlapping that tile may
  need to be drawn again to reconstruct correct transparency and painter order.
- The GPU still copies each submitted tile into a drawable. This plan removes the
  separate CPU image upload and avoids unchanged-tile copies; it does not eliminate
  all GPU reads/writes or Core Animation compositor traffic.
- Joining all tile observations is not proof of scanout-atomic multi-layer display.
  Group presentation in one transaction and gate changed geometry conservatively.
- `nextDrawable` can block until its native timeout. Keeping acquisition on the UI
  thread is an explicit constraint, not a promise of nonblocking frame production.
- An affected stack can require scanning its direct children, and a moved subtree
  can require updating every descendant's document coordinates. Do not claim an
  O(1) update or zero full-window work for changes with full-window dependencies.

### Defaults and budgets

These are named internal constants, not new authoring options.

| Constant | Value | Meaning |
| --- | --- | --- |
| `tilePixels` | 512 | Full tile width/height in physical pixels; edge tiles are smaller. |
| `rasterRowAlignment` | 256 bytes | Shared bitmap row-pitch alignment. |
| `bytesPerPixel` | 4 | BGRA8 pixel payload. |
| `maximumDimension` | 16,384 pixels | Maximum physical viewport extent on either axis. |
| `maximumTiles` | 1,024 | Maximum tile-layer count in one window. |
| `maximumRasterBytes` | 256 MiB/window | Current plus retired shared raster-buffer ownership. |
| `tileDrawableCount` | 2 | Maximum drawables in each tile layer's swap queue. |
| `maximumActiveBatches` | 1/window | One target composite awaiting completion/confirmation. |
| `presentationHistoryLimit` | 64 | Existing maximum registered unresolved attempt identities. |
| `unsubmittedHistoryLimit` | 64 | Recent scene/geometry roots not pinned by admission. |
| `maximumInputGateRegions` | 64 | Coalesce excessive input uncertainty to viewport coverage. |
| `drawableRetryDelay` | 16 ms | One delayed visible acquisition retry before recovery. |
| `confirmationDeadline` | 1,000 ms | Maximum wait for joined evidence for a committed batch. |
| `recoveryDrainDeadline` | 1,000 ms | Additional bounded wait for unfinished GPU owners during recovery. |
| `maximumRecoveryCycles` | 1 | Replaced-grid recovery allowance between successful confirmations. |

A window's shared-buffer budget does not include application-held scene snapshots,
Core Text allocations, driver-private allocations, or Core Animation surfaces.
Report estimated drawable payload separately rather than calling the raster budget
a total-process or total-GPU-memory limit. Device allocation failure remains a
real failure even when the arithmetic budget passes.

## Current implementation and ownership

### Grounded starting points

| Existing source | Current responsibility/limitation | Planned change |
| --- | --- | --- |
| `modules/core/core.cppm` | `Document`, mutable nodes, one dirty flag, flat snapshot storage, full geometry maps. | Classified work state, persistent projections/indexes, lazy compatibility views. |
| `src/core/document.cpp` | Creation/removal/reorder, broad invalidation, resize/environment wiring. | Mutation classification, safe retirement, generation updates. |
| `src/core/input.cpp` | Focus/scroll invalidation and single-drawable geometry evidence. | Local work requests, composite gates, pinned geometry. |
| `src/layout/layout.cpp` | Recursive measure and arrange of the retained tree. | Input-keyed caches and dirty-route traversal, preserving layout semantics. |
| `src/scene/scene.cpp` | Full node snapshots and a full paint vector on every dirty commit. | Persistent branch projection and explicit lazy flattening. |
| `src/ui/mount.cpp`, `modules/ui/ui.cppm` | Bindings assign retained fields and broadly invalidate. | Classified built-in assignments, no-op suppression, static-mount audit. |
| `modules/graphics/presentation.cppm` | Bounded attempt tracker joining one GPU result and one timestamp. | Expected tile sets, typed observations, compatible singleton overloads. |
| `src/application/application.cpp` | Copies a complete `SceneSnapshot` into flat native commands. | Incremental owning tree conversion and private callback adaptation. |
| `src/platform/native_contracts.hpp` | Module-free native scene/command/callback transport. | Tree transport, composite submission/evidence values, rendering statistics. |
| `src/platform/macos/native_host.mm` | AppKit host plus full CPU bitmap/upload/Metal blit/presentation. | Keep host/event wiring; extract raster preparation and tile presentation. |
| `modules/testing/testing.cppm` | Production-backed headless host and synthetic singleton evidence. | Explicit full-reference and composite-test seams. |
| `tests/CMakeLists.txt` | Portable suites and opt-in real-window smoke. | Register focused core, raster, scheduler, and native gates. |

New implementation units are responsibility splits, not new public modules:

- `src/scene/geometry.cpp`: persistent geometry index and geometry-root comparison.
- `src/platform/tiles.hpp` / `tiles.cpp`: module-free pixel-grid arithmetic and tile
  selection, usable by portable tests and the native renderer.
- `src/platform/macos/renderer.hpp` / `renderer.mm`: private prepared scene, ink
  calculations, shared bitmap resources, and reference/tile raster operations.
- `src/platform/macos/presentation.hpp` / `presentation.mm`: private tile layers,
  batches, callbacks, backpressure, and recovery. Keep window event handling in
  `native_host.mm`; do not grow another monolithic host implementation.

Register compiled sources in the existing library target. Apple-only units and
framework dependencies stay under the Apple build branch. Private headers are
not installed and do not become alternate public consumption APIs.

### Dependency direction and executors

The retained engine owns identity, bindings, layout, projection, input policy, and
presentation geometry. The application bridge owns conversion. The native adapter
owns AppKit/Quartz/Metal resources. Core never imports a native framework.

Mutable document, renderer, layer, and scheduler state is main/UI-executor-confined.
Metal callbacks run on unspecified threads and may only transport owning values
or perform independent resource cleanup before dispatching state changes to main.
No callback calls into a closed document through a borrowed reference.

Published scene, geometry, native transport, and prepared resource graphs are
immutable. Delayed work carries exact attempt, surface, and attachment identities.
Do not select a service or resource based on whichever window is currently key.

### Identity vocabulary

Keep these identities separate; never use scene revision as a substitute for all
of them:

| Identity | Changes when | Used to reject |
| --- | --- | --- |
| `NodeId` | A retained occurrence is created; never reused. | Removed/replaced occurrence references. |
| Scene revision | A coherent document commit publishes. | Scene/geometry mismatches. |
| Branch/paint generation | A new immutable branch or local paint segment is built. | Stale conversion/preparation cache entries. |
| Surface epoch | Viewport, backing scale, or surface configuration changes. | Scale/geometry-dependent work. |
| Presentation epoch | Any tile-grid/layer attachment is replaced or invalidated. | All prior drawable evidence and scheduled transactions. |
| Attempt sequence | Each actual admission, including retries and anchor attempts. | Duplicate/retried submissions of the same revision. |
| `TileId` | Row-major ordinal within one presentation epoch. | Observations for another tile in the same attempt. |

Use never-reused identity allocation or checked monotonic advancement as
appropriate. Overflow is diagnosed before wraparound; do not make stale work valid
again by resetting an identity counter.

## Core invalidation and layout

### Canonical work-state additions

Add the following internal concepts in core. Declarations omit includes/module
prologues/Doxygen here, not in the future implementation.

```cpp
namespace stana::detail {
enum class Dirty : std::uint8_t {
    none = 0,
    measure = 1,
    arrange = 2,
    paint = 4,
    snapshot = 8,
    structure = 16
};

struct MeasureKey {
    Size available{};
    std::uint64_t localInputs = 0;
    std::uint64_t descendantInputs = 0;
    std::uint64_t textEnvironment = 0;
};

struct ArrangeKey {
    Rect assigned{};
    std::uint64_t placementInputs = 0;
    std::uint64_t childMeasurements = 0;
    Direction direction = Direction::leftToRight;
};

struct NodeWork {
    Dirty local = Dirty::none;
    std::set<NodeId> measureChildren;
    std::set<NodeId> arrangeChildren;
    std::set<NodeId> projectionChildren;
    std::optional<MeasureKey> measuredAt;
    std::optional<ArrangeKey> arrangedAt;
    std::shared_ptr<const SceneBranch> projection;
};
}
```

Define typed flag operations beside `Dirty`; do not scatter numeric masks. Store
input generations in the node/document records owning those inputs. Advance a
relevant generation when its first invalidation is merged into the current dirty
batch. Multiple writes before measurement still produce one final computation.

`NodeWork` distinguishes a local need from a path leading to a dirty descendant.
Marking a child dirty does not automatically mean its parent's local paint, text
measurement, or arrangement must be recomputed.

Keep a document-level pending-work marker so the reactive runtime can cheaply
skip clean documents. It is an admission flag, not the cache invalidation model.

### Mutation and binding contract

Built-in binding assignments return whether retained semantics changed after
validation. A changed result carries or selects the precise `Dirty` flags and
whether inherited projection context may change.

- Compare retained values before mutation. For floating scalars, use the project's
  ordered comparison conventions rather than triggering `-Wfloat-equal`.
- Initial installation must run even if the property's scalar matches a default
  when an additional semantic flag changes: setting black foreground can still
  change `isForegroundExplicit` from false to true.
- Do not suppress updates by comparing only a text pointer, hash, or property
  wrapper identity. Equality is resolved-value equality.
- `Metrics::propertyUpdates` keeps its delivered-assignment meaning. A delivered
  equal assignment may count as delivery but must not invalidate or publish.
- Built-in assignment functions do not invoke application callbacks. Validate
  first so a rejected property cannot leave half-updated retained style fields.
- Keep a conservative compatibility path for existing internal unclassified
  assignments; route it to full layout/paint invalidation, never guessed reuse.
  All first-party built-in assignments must move to the classified path.
- Static mounting writes are covered by the new occurrence's initial full dirty
  state. Later code must not write retained layout/paint fields behind invalidation.

### Invalidation matrix

The table is normative for current fields. `ancestors` means affected dependency
paths, not every ancestor's descendants.

| Retained input | Measurement | Arrangement | Paint/projection | Other rules |
| --- | --- | --- | --- | --- |
| Text | Self + aggregate ancestors | Only if measured size or placement dependency changes | Local text and snapshot | Same-size text does not displace siblings. |
| Font | Self + aggregate ancestors | Where size/placement changes | Local text | Include effective text scale/provider generation. |
| Frame/padding | Self + ancestors | Local placement, then changed descendants | Local paint/snapshot as needed | Padding can move text with unchanged outer bounds. |
| Container spacing | Container + ancestors | Container's child placement | Only changed geometry/paint | Reuse child text measurements at unchanged proposals. |
| Foreground | None | None | Local text paint | No new inherited foreground behavior. |
| Background/border color | None | None | Local paint | Preserve existing paint order. |
| Border width | None | None | Local paint/ink | Current borders do not consume layout space. |
| Corner radius | None | None | Local paint; affected clipped subtree coverage | Children can retain local paint if only clip shape changes. |
| Opacity | None | None | Local and descendant effective opacity | Apply alpha exactly as current painter does. |
| Enabled | None | None | Local/descendant disabled opacity + eligibility | Nested disabled nodes retain current multiplicative behavior. |
| Inert | None | None | Interaction snapshots/geometry | Do not invent an inert paint style. |
| Visible ↔ hidden | None | None | Subtree visibility/snapshots | Hidden preserves measurement/arrangement footprint. |
| Any ↔ collapsed | Self + ancestors | Affected container/descendant placements | Subtree visibility/snapshots | Do not reuse stale nonzero collapsed measured size. |
| Clip | None | None | Clip commands and affected subtree coverage/geometry | Current hit policy remains rectangular. |
| Focus | None | None | Old/new focused local paint + snapshots | Re-focusing the same valid node is a no-op. |
| Scroll offset | None | Scroll subtree | Changed document geometry | Preserve current clamp algorithm; do not remesure text merely for motion. |
| Create/remove/reorder | Parent aggregates + ancestors | Affected container placement | Structure, visible snapshots, old/new ink | Preserve keyed identity where it already exists. |
| Theme | None | None | Viewport background and theme-dependent text | Full viewport paint damage is valid. |
| Direction | Only if a future measured input actually depends on it | Affected subtree | Changed bounds/snapshots | Preserve current sizing semantics. |
| Text scale/provider | Text nodes + ancestors | Where dimensions change | Text paint/preparation | Old measurements from another provider are invalid. |
| Viewport size | Where available proposals differ | Root and affected children | Changed geometry | Do not force unchanged leaf measurement keys to miss. |
| Accessibility/diagnostic metadata | None | None | Relevant snapshots only | No raster damage unless an existing visual input changes. |

### Dirty propagation algorithm

For one changed node:

1. Merge local dirty bits and advance only the corresponding local input stamps.
2. Record its identity in the parent's appropriate dirty-child sets.
3. Walk the ancestor route, recording the direct child identity at every edge.
4. Measurement-affecting changes invalidate ancestor aggregate measurement keys.
5. Arrangement-only changes do not invalidate text measurements.
6. Projection-only changes do not set local paint bits on ancestors.
7. Stop when the same complete route/domain is already recorded for this batch;
   do not repeatedly walk the same path for every property in a transaction.
8. Mark the document pending and let the outer reactive settlement perform work.

If an occurrence has been retired, ignore its late binding invalidation. Do not
recreate its dirty route or make it document-addressable again.

### Measurement algorithm

Use the current measuring equations as the behavioral oracle.

```text
measure(node, available):
  if current complete key matches cache and there is no measure work:
    return cached measured size
  if collapsed:
    store zero measured size for this occurrence
    propagate a changed result if its old measured size was nonzero
    return zero
  copy the owning inputs needed across any provider callback
  derive contentAvailable exactly as today
  measure text OR aggregate children using cached unchanged child results
  resolve current frame/min/max rules exactly as today
  compare previous and new measured sizes
  store measurement result/key
  if size changed:
    invalidate the parent's child-placement aggregate
  return measured size
```

Container recomputation may read cached sizes of its direct children. It must not
call the native text provider for every sibling merely because one text changed.
Do not skip child measurement under fixed outer dimensions: arrangement may still
need child sizes. Constraints include available height as well as width.

A text-size change need not travel to the root as a *changed result* if a capped or
fixed ancestor's result is unchanged. That ancestor can still require internal
arrangement. Separate those facts rather than clearing all downstream work early.

A size-only provider replacement advances the text-environment generation. The
provider contract remains the existing one; adding wrapping/caret layout is not a
prerequisite. Revalidate identity after service calls and abort/resettle a draft
if retained mutation occurred during a reentrant provider call.

### Arrangement algorithm

```text
arrange(node, assignedBounds):
  if local arrangement key matches:
    visit only recorded descendant arrangement routes with their current bounds
    return without a local placement computation
  compute the node's content rectangle and local placement exactly as today
  for each participating direct child:
    calculate its proposed rectangle from current measured sizes
    recurse only if its proposal changed or it has arrangement work
  update changed document-relative bounds and their projection/geometry work
  store arrangement key and clear only successfully consumed route entries
```

A changed parent origin invalidates document coordinates even when sizes remain
identical. With the chosen document-relative paint representation, moving a whole
subtree can require updating its descendants; do not pretend those are unchanged.

Keep hidden children in layout, exclude collapsed footprints according to current
behavior, and avoid leaving stale measurements on collapsed records. Test
root/fill overrides, scroll clamping, and spacing as they currently work. Existing
layout deficiencies described by the omnibus document are separate work, not an
excuse to change the oracle in an optimization patch.

## Immutable projections and geometry

### Canonical complete render projection

Define these internal core types after the existing `NodeSnapshot` declaration.
They are available to compiled engine/bridge code, not a new authoring API.

```cpp
namespace stana::detail {
struct PaintSegments {
    std::uint64_t identity = 0;
    std::vector<PaintCommand> prefix;
    std::vector<PaintCommand> suffix;
    std::optional<Rect> childClipBounds;
};

struct ProjectionContext {
    double opacity = 1;
    bool visible = true;
    bool eligible = true;
    Rect rectangularClip{};
};

struct SceneBranch {
    NodeId id{};
    std::uint64_t generation = 0;
    ProjectionContext context{};
    std::shared_ptr<const PaintSegments> paint;
    std::shared_ptr<const NodeSnapshot> snapshot;
    std::vector<std::shared_ptr<const SceneBranch>> children;
};

struct GeometryRecord {
    Rect bounds{};
    Rect rectangularClip{};
    NodeId parent{};
    bool eligible = true;
};

struct GeometryIndex;
using GeometryRoot = std::shared_ptr<const GeometryIndex>;

struct RenderSnapshot {
    std::uint64_t revision = 0;
    Size size{};
    Color background = Color::white();
    std::uint64_t textEnvironment = 0;
    std::shared_ptr<const SceneBranch> root;
    GeometryRoot geometry;
};
}
```

Local paint remains in document-relative logical points to preserve current
`PaintCommand` semantics. Transform-only subtree rendering is not added here.
Branch generations and paint identities are never-reused process-local identities,
not a persistent serialization format or a native-resource pointer cast.

### Branch rebuilding and inherited context

Visit a branch only if its local input, child route/structure, or effective incoming
context changed. Return the existing shared branch otherwise.

When an ancestor only needs a new child pointer, share its `PaintSegments` and
`NodeSnapshot` if those local values remain valid. Rebuild the ordered child-pointer
vector and ancestor branch, not the unchanged descendants' drawing lists.

Compute effective opacity from incoming opacity, local opacity, and the current
local disabled multiplier. Effective eligibility additionally includes visibility,
local enabled state, and inert ancestors. Do not use effective eligibility to apply
an extra disabled paint multiplier to every descendant: that changes existing alpha.

Visibility is inherited. Hidden/collapsed ancestors suppress descendant paint and
visible node snapshots while retaining mounted records. The persistent geometry
index must remove those descendants until they become visible again. A hide/show
transition cannot reuse a visible geometry entry only because its bounds match.

A clip change can alter subtree coverage and interaction clipping without changing
child local paint. Keep `childClipBounds` consistent with the actual node/scroll
clip command; it is conservative bounding metadata, not a substitute for replaying
the real rounded path during drawing.

### Painter ordering and clip validation

The flattened observable order must remain:

```text
background fill
push node clip, if any
local text
Canvas commands
children, in retained order
pop node clip, if any
normal/current focus border
```

A Canvas command stream must balance its own clip stack. Reject pop underflow and
leftover pushes with a diagnosed invalid-input error before publishing the draft.
Canvas clips cannot span into unrelated children or leak between reusable branches.
The node-owned clip may surround its local content and children; its matching
restore remains in the node's suffix.

Validate geometry fields used by each operation. Rectangles/clips have finite
nonnegative sizes and radius; stroke widths and font sizes are finite/nonnegative;
line endpoints may describe either direction and must be normalized for ink bounds.
Do not reject a negative line delta merely because other rectangles require a
nonnegative extent. Preserve zero-width/hairline semantics where CoreGraphics has
them. Validate RGBA components under the existing color contract.

### Transaction-coherent publication

Build a `CommitDraft` containing the new scene root, geometry root, and staged
per-node cache promotions. Existing public inspection caches still describe the
previous committed revision until the draft is complete.

Publication sequence:

1. Confirm the reactive binding batch is settled.
2. Compute the required measurement and arrangement dependency frontier.
3. Build changed paint/snapshot branches and geometry entries into the draft.
4. Validate clip/command invariants and the draft's viewport/environment stamp.
5. Publish the complete `RenderSnapshot` and promote successful node caches.
6. Invalidate lazy flat inspection caches for the new revision.
7. Record geometry history and notify the native host.
8. Let the existing runtime deliver mount effects after coherent publication.

Do not publish a half-new scene or increment the public scene counter on a failed
draft. Leave relevant work dirty for a recoverable resettlement; invalid author
input remains diagnosed rather than retried forever. A reentrant provider mutation
invalidates the draft's input stamp and requires re-resolution, not continued use
of borrowed node storage. Preserve the runtime's existing nonsettling-update bound.

No-op assignments do not create drafts. A meaningful metadata-only commit may
publish a revision even if its paint tree is equivalent; native acknowledgment is
handled explicitly through an evidence anchor, not a fake presentation timestamp.

### Lazy compatibility views

Keep these existing types and signatures working:

- `SceneSnapshot`, including its `std::vector<PaintCommand> commands`.
- `TestHost::scene() -> std::shared_ptr<const SceneSnapshot>`.
- `TestHost::nodes() -> std::vector<NodeSnapshot>`.
- `NodeSnapshot`, `Drawing`, `PaintCommand`, component aggregate authoring, and
  `Application::run`.

`Document::renderSnapshot()` supplies the internal complete tree to production.
`Document::scene()` flattens prefix → children → suffix only when inspection asks.
`Document::snapshots()` similarly materializes visible node values lazily. Cache
both results for that committed revision. Repeated reads reuse the cache; a later
commit never mutates a previously returned owning scene value.

A read inside a transaction returns the previous complete commit, not a view of
partially assigned mutable nodes. Input operations such as Tab may deliberately
traverse a complete committed projection; this is not a reason to eagerly flatten
every native frame. Keep counters so native conversion cannot hide accidental
inspection materialization.

### Persistent geometry index

Implement a private immutable AVL tree keyed by full `NodeId` order. An entry owns
`GeometryRecord`, immutable left/right roots, and a cached height.

Required operations:

```cpp
namespace stana::detail {
auto geometryFind(const GeometryRoot &, NodeId)
    -> std::optional<GeometryRecord>;
auto geometrySet(const GeometryRoot &, NodeId, const GeometryRecord &)
    -> GeometryRoot;
auto geometryErase(const GeometryRoot &, NodeId) -> GeometryRoot;
void geometryChanges(const GeometryRoot &before, const GeometryRoot &after,
    std::vector<Rect> &oldAndNewRegions);
}
```

Insert/update/remove path-copy the search path and rebalance immutably. Equal
updates return the same root. Do not copy a `std::map` in place of a persistent
index, and do not expose the index as a new public container library.

Compare geometry roots by shared identity first. When keys align, recurse only
into unequal child roots. Rotations are not semantic changes: if tree shapes differ,
compare keyed values for the affected ranges rather than gating every rectangle
because an AVL root moved. A full comparison of a genuinely restructured affected
subtree is acceptable; unchanged paint-only commits must keep the geometry root.

Include old and new bounds when geometry, parent, eligibility, or effective clip
changes. Store only visible occurrences, including noninteractive structural nodes
as needed for current hit-route clipping. Current rounded-rectangle paint does not
create a new rounded hit-test rule; keep the existing rectangular policy.

### Geometry history and safe disposal

Retain at most 64 recent unsubmitted revision/root records. Admission additionally
pins `{attempt identity, revision, geometry root}` until that attempt is confirmed,
retired, reset, or closed. More than 64 later publications cannot invalidate an
active attempt's root. PresentationTracker's existing 64 unresolved-attempt limit
remains independent; the native scheduler normally uses only one.

A confirmation installs the exact pinned root without rebuilding a complete map.
Late confirmations cannot move acknowledged geometry backwards. Pruning old
unsubmitted history is not itself evidence of a presentation failure.

On subtree removal, capture old immutable roots for damage; detach the subtree;
retire every lookup identity and dirty-route entry; clear invalid focus/press
identities; update current geometry; then dispose scopes while an owning removal
batch keeps storage alive. Cleanup may reenter the document but sees stale removed
handles and cannot recursively delete the same records.

## Native transport and preparation

### Module-free transport shape

Replace the private flat production scene with owning immutable tree transport.
Keep public `PaintCommand`/`SceneSnapshot` unchanged. Native transport can change
because it is private and not installed as an application-facing header.

```cpp
namespace stana::native {
struct NodeIdentity {
    std::uint64_t document = 0;
    std::uint64_t serial = 0;
};

struct PaintSegments {
    std::uint64_t identity = 0;
    std::vector<Command> prefix;
    std::vector<Command> suffix;
    std::optional<LogicalRect> childClipBounds;
};

struct SceneBranch {
    NodeIdentity id{};
    std::uint64_t generation = 0;
    bool visible = true;
    std::shared_ptr<const PaintSegments> paint;
    std::vector<std::shared_ptr<const SceneBranch>> children;
};

struct Scene {
    std::uint64_t revision = 0;
    LogicalSize size{};
    SceneColor background{};
    std::uint64_t textEnvironment = 0;
    std::shared_ptr<const SceneBranch> root;
};
}
```

Use the following standard-library-only mirrors for the value fields:

```cpp
namespace stana::native {
struct LogicalPoint { double x = 0; double y = 0; };
struct LogicalSize { double width = 0; double height = 0; };
struct LogicalRect { LogicalPoint origin{}; LogicalSize size{}; };
struct SceneColor { double red = 0; double green = 0; double blue = 0; double alpha = 1; };
}
```

`LogicalRect`, `LogicalSize`, and `SceneColor` are small private standard-library-
only transport aggregates mirroring the required values. They do not import core
or use `CGRect`, `NSColor`, module-defined `Rect`, or an Objective-C handle. Keep
UTF-8 strings, fonts, opacity, corner radius, and line width owning/value-based as
in today's `Command`. Bridge conversion is explicit in both directions where
needed; never reinterpret module structs as native structs.

### Bridge conversion algorithm

Retain the previous source root and converted native root per window/session.
Convert the latest complete `RenderSnapshot` as follows:

1. If source generation matches the previous paired branch, reuse its native root.
2. Otherwise match previous/new children by full occurrence identity, not by vector
   position alone. Reorder must not pair a row with another row's cached resources.
3. If local paint identity matches, reuse the native `PaintSegments` and its owned
   strings. Otherwise convert prefix/suffix values exactly once for that segment.
4. Recursively convert changed children; copy only the necessary child-root list.
5. Publish the complete owning native scene with revision, viewport, and background.

An ancestor's generation changing because of one child must not cause every local
text string to be copied again. An empty or invisible source branch remains
explicitly invisible, not an accidental visible branch with stale children.

Keep only the last paired conversion roots and roots still owned by latest/raster/
confirmed/active snapshots. Do not add a process-global strong cache of every scene
or a raw-address lookup that can collide after allocator reuse.

### Prepared scene ownership

Native preparation uses immutable objects separate from mutable layer state:

```cpp
namespace stana::native {
struct PreparedText;       // Owns the current Core Text line and baseline metrics.
struct PreparedPath;       // Owns the native rounded rectangle/clip path.
struct PreparedOperation;  // Command value + owning text/path resources + ink.
struct PreparedPaint;      // Prepared prefix/suffix for one local paint identity.
struct PreparedBranch;     // Local prepared paint, children, conservative ink.
struct PreparedScene;      // Source scene, surface key, and complete prepared root.
}
```

A prepared paint resource is reused when source paint identity and all relevant
native preparation inputs remain equal. A prepared branch can be reused when its
paint, ordered prepared children, visibility, and surface-dependent inputs match,
even if nonvisual core snapshot metadata changed.

The surface key includes physical viewport, backing scale, pixel format/color
contract, and text/native preparation generation. Scale-dependent antialiasing and
ink calculations must not reuse a 1× preparation as a 2× result.

For text, construct the same attributed string, `CTLine`, ascent-based baseline,
and drawing orientation as today's renderer. Own the resulting immutable Core
Text objects. Do not construct a new line for every tile that intersects the same
label. Do not independently wrap, truncate, or change typography in this pass.

The omnibus backlog's eventual shaped-text service can later provide the owning
resource. This migration must not install a competing editor/text-layout runtime.
Native objects remain private; no `CTLineRef` enters a public module interface.

Portable paint colors already include effective opacity. Native rasterization must
not multiply the branch context opacity into those colors again. Preserve the
current attributed-text color/alpha application exactly once, including color-font
behavior; an extra CGContext alpha multiplier would change nested opacity.

### Conservative ink contract

Every supported paint operation needs a conservative device-aware paint bound.
Layout bounds alone are insufficient: glyphs, strokes, Canvas geometry, and focus
outlines can extend beyond them.

| Operation | Required conservative bound |
| --- | --- |
| Fill rectangle | Geometric extent expanded by one device pixel for coverage. |
| Stroke rectangle | Include half-width stroke expansion plus coverage margin; preserve native join behavior. |
| Line | Normalize both endpoints, include stroke/hairline extent and coverage margin. |
| Text | Use the prepared line's actual image extent, translated by the same baseline and Y transform used for drawing. |
| Push/pop clip | Change clip state; they do not independently contribute ink. |
| Node background | Evaluate under incoming ancestor clip, before the node's child clip. |
| Local text/Canvas | Evaluate under the node/content clip active at that operation. |
| Children | Union child ink under the enclosing node clip, if present. |
| Node outline | Evaluate after restoring the node-owned clip, under ancestor clip. |
| Invisible branch | Empty painted extent; retain old extent when visibility changes. |

The one-device-pixel margin is `1 / backingScale` in logical coordinates before
outward device rounding. A zero-width native hairline still needs physical-pixel
coverage. Clip bounds may use their enclosing rectangle for damage; actual replay
must still use the real rounded path.

Text bounds must use the same prepared resources and drawing-context settings as
painting. Core Text image/path bounds can be insufficient for unusual color or
bitmap glyph resources; if a reliable conservative extent cannot be established,
mark that operation's ink unknown and damage the viewport. Do not choose an
arbitrary text padding and call it guaranteed coverage.

Store raw/conservative branch ink so reusable child resources are not incorrectly
bound to another ancestor clip. Apply old and new incoming clip bounds separately
while diffing. If a parent's clipping changes, invalidate its old/new clipped
subtree coverage even when every child resource is shared.

Unknown bounds are a correctness fallback, not an allocation size. They select
viewport tiles and increment `fullDamageFallbacks`. Nonfinite invalid author
geometry is diagnosed; do not feed it into `floor`, casts, or native allocation.

### Damage algorithm and required cases

Damage comparison takes complete prepared scenes and returns a bounded tile mask.
It does not rely on receiving every document revision in order.

```text
diff(oldBranch, newBranch, oldClip, newClip):
  if branch identities and applicable clip contexts are equal: return
  if branch inserted/removed/visibility changed:
    add the appropriate old/new subtree ink
    return
  if local paint changed:
    add old/new local ink
    if child clip changed: add old/new clipped subtree ink
  if child order changed:
    add old/new affected parent subtree coverage
    return
  match remaining children by occurrence identity
  recurse into changed child pairs; skip shared children
```

For reorder, conservative whole-parent subtree damage is chosen for this migration.
Do not introduce a minimum-overlap reorder optimizer before correctness is proven.
Pure ancestor path copies with unchanged local paint are not themselves damage.

| Change | Must invalidate | Must not assume |
| --- | --- | --- |
| Text/color/shape edit | Old + new local ink | Equal layout bounds mean equal pixels. |
| Movement/resizing | Old + new painted extents | The old location gets erased automatically. |
| Removal | Old extent | Removed nodes are still addressable in the document. |
| Insertion | New extent | Newly acquired drawables are cleared. |
| Opacity/disabled | Affected subtree ink | Only the ancestor's background changed. |
| Clip expansion/contraction | Old + new clipped subtree coverage | Children with unchanged commands require no display update. |
| Z-order/reorder | Affected old/new parent coverage | Reusing row identity preserves compositing order. |
| Canvas overflow | Actual command ink | Canvas bounds clip it implicitly. |
| Theme/background | Complete viewport | Only explicit background nodes need paint. |
| Scale/grid replacement | Entire new grid | Point-size equality makes old pixels valid. |
| Uncertain failed display | All recovery-grid tiles | An earlier successful blit proves visible content. |

### Two baselines: raster content and confirmed display

Retain these concepts independently:

- `rasterBaseline`: the complete scene currently represented by shared tile pixels.
- `confirmedComposite`: the complete scene whose necessary tile evidence joined.
- `activeTarget`: the immutable target and resources for one admitted batch.
- `latestRequested`: the latest complete pending scene, replacing older pending work.

Some of these roots can be the same object. They are not an unbounded scene queue.
Maintain no more than these owning roles plus transient conversion/preparation
objects and explicitly pinned presentation resources.

For a latest target `T`, calculate:

```text
rasterDirty       = damage(rasterBaseline, T)
presentationDirty = damage(confirmedComposite, T) ∪ untrustedTiles
```

Bring all `rasterDirty` buffers to `T` before advancing the raster baseline. A tile
may be raster-dirty but not presentation-dirty if an unsubmitted intermediate was
abandoned and its pixels are being restored to already displayed content.

Example that must work:

```text
A is confirmed and red
B is blue; CPU buffers reach B, but drawable acquisition fails
C requests red again and is visually equivalent to A
```

Comparing only confirmed A to C gives no damage, but the shared buffer is still B.
The raster baseline comparison must restore it before any anchor or later
submission can use it. Conversely, raster success for B does not confirm B onscreen.

After any *submitted* batch becomes uncertain, discard display assumptions through
a new presentation epoch and full-grid recovery. Do not try to infer which dirty
tiles happened to become visible from partial callbacks.

## Shared storage and tile rasterization

### Portable pixel-grid values

Use the same arithmetic implementation in native code and portable tests.

```cpp
namespace stana::native {
struct PixelSize {
    std::size_t width = 0;
    std::size_t height = 0;
};

struct PixelRect {
    std::size_t x = 0;
    std::size_t y = 0;
    std::size_t width = 0;
    std::size_t height = 0;
};

struct TileLayout {
    std::uint32_t ordinal = 0;
    PixelRect pixels{};
    std::size_t bytesPerRow = 0;
    std::size_t allocationBytes = 0;
};

struct TileGrid {
    PixelSize viewport{};
    std::size_t columns = 0;
    std::size_t rows = 0;
    std::size_t rasterBytes = 0;
    std::vector<TileLayout> tiles;
};
}
```

Grid construction uses row-major ordering:

```text
pixelWidth  = ceil(logicalWidth  × scale)
pixelHeight = ceil(logicalHeight × scale)
columns = ceil_div(pixelWidth, 512)
rows    = ceil_div(pixelHeight, 512)
tile.x = column × 512
tile.y = row × 512
tile.width  = min(512, pixelWidth  - tile.x)
tile.height = min(512, pixelHeight - tile.y)
bytesPerRow = align_up(tile.width × 4, 256)
allocationBytes = bytesPerRow × tile.height
```

Validate finite nonnegative logical size and finite positive scale before
multiplication. Check finite products and dimension limits before conversion to
`size_t`. Use checked arithmetic for alignment, tile count, total bytes, and device
buffer limits. Avoid overflow-prone `(value + divisor - 1)` on unchecked inputs.

A zero physical width or height creates no tile resources and no admission; it
keeps pointer geometry unavailable. Do not allocate a fake 1×1 viewport merely to
obtain positive presentation evidence.

### Mapping damage into tile selection

Intersect damage with the physical viewport after conservative expansion. Round
left/top down and right/bottom up, then select every tile touched by the resulting
nonempty pixel rectangle. A rectangle exactly ending at a tile boundary does not
select the next tile unless its antialiasing/stroke expansion crosses the boundary.

Use a bounded `maximumTiles` mask internally and enumerate sorted ordinals for
submission. Union directly into the mask rather than maintaining an unbounded list
of overlapping damage rectangles. Do not merge disjoint small changes into a
viewport-sized rectangle merely for convenience.

Out-of-viewport ink cannot allocate offscreen tiles. In-viewport pixels affected by
an overflowing Canvas or glyph still select their actual destination tiles.

### Shared raster-resource ownership

One reusable shared buffer/context belongs to each grid tile. A small buffer ring
is not required because this design permits only one active composite/window and
holds its source pixels immutable until the attempt terminates safely.

```cpp
// Private Objective-C++ renderer resource, never exported by a C++ module.
struct RasterTile {
    TileLayout layout;
    id<MTLBuffer> buffer;
    CGContextRef bitmapContext;
    // Noncopyable; destruction releases the context before the buffer owner.
};
```

Creation sequence:

1. Ensure `current + retired + requested` raster storage stays within budget.
2. Allocate the Metal buffer with `newBufferWithLength:options:` using shared
   storage and default CPU cache mode. Validate nonnil buffer and contents pointer.
3. Create sRGB color space with balanced Create/Release ownership.
4. Create a bitmap context over `buffer.contents`, 8 bits/component, aligned row
   pitch, premultiplied-first alpha, and little-endian 32-bit byte order.
5. Retain the buffer for at least as long as the context and any GPU read of it.
6. Diagnose allocation/context failure; do not silently switch to vector upload.

Default CPU cache mode is deliberate: CoreGraphics compositing can read destination
pixels. Do not choose write-combined memory solely because the final operation is
an upload. Do not use an ordinary vector plus `newBufferWithBytesNoCopy`; such an
allocation does not establish the required page alignment/VM/lifetime contract.

Never call `replaceRegion:withBytes:` in the production shared-buffer path. There
is no intermediate scene texture and no separate CPU bitmap-to-resource memcpy.
Copying tiny metadata/command values remains allowed and is not an image upload.

### Raster replay and pixel phase

Fully clear a dirty tile to the scene's opaque background, preserving the current
clear-alpha behavior. Replay the prepared scene under the tile's device-aligned
clip. Save/restore complete graphics state around each tile pass so previous
transforms, clips, alpha, text state, and antialiasing settings cannot leak.

For tile origin `(px, py)`, scale `s`, and physical height `h`, use the same global
point-to-device mapping as the reference with a device-integer tile translation:

```text
translate(0, h)
scale(s, -s)
translate(-px / s, -py / s)
```

Use the matching coordinate convention for the buffer-to-drawable copy. Geometry
and glyphs crossing tile boundaries must retain the same sampling phase as the
single full-window bitmap. Do not lay out or independently round each tile's text.

Traverse the prepared tree in painter order, culling only branches whose
conservative ink cannot intersect the tile under the active clip. Preserve clip
stack entry/exit even when culling descendants. Redraw unchanged intersecting
content when needed to reconstruct the tile; do not alpha-blend new output over
unreset stale pixels.

No gutters are allocated for current fill/stroke/line/text operations. Exact
pixel-phase and full-reference comparison are hard gates. Future blur/shadow or
filter operations need an explicit sampling-neighborhood contract before they can
use this zero-gutter rule; they are not silently supported by this plan.

Advance `rasterBaseline` only after all dirty tiles for that target are successfully
rasterized. A partial failure cannot mark the complete raster image current.
Ordinary buffers may back the CPU-only reference test, but that reference is not a
production fallback renderer.

### Shared-memory access and accounting

CoreGraphics finishes its writes before the command buffer is committed. From
commit through safe batch termination, source tile buffers are read-only to CPU
rasterization. GPU completion releases the read hazard; the stricter one-active-
batch policy also waits for presentation evidence before starting another target.

Retain GPU-read buffers through an independent submission owner. A stale callback
still permits resource cleanup, even though it may not mutate current window or
document evidence. Do not use presentation timeout as permission to release or
rewrite a buffer whose GPU command has not completed.

Track aligned allocation bytes separately from actual copied pixel payload.
Padding bytes are not visible pixels and need not be compared or counted as copied
image payload. Buffer reuse must not increase `bufferAllocations` after warm-up.

## Tiled presentation and scheduling

### Layer hierarchy and exact configuration

Replace the window's single full-window Metal layer with:

```text
STANAView
  hosting CALayer, clipped to current view bounds
    CAMetalLayer tile (column 0, row 0)
    CAMetalLayer tile (column 1, row 0)
    ...
```

The hosting root is opaque and has the scene background. For the owned layer
hierarchy set root `geometryFlipped=YES`, tile `geometryFlipped=NO`, and each tile's
`anchorPoint=(0,0)`. Use tile bounds `(0,0,width/scale,height/scale)` and position
`(x/scale,y/scale)` from its pixel rectangle. AppKit still owns the view's logical
frame; do not introduce another view-space Y flip. Keep tile content orientation
consistent with the existing bitmap transform and verify with an asymmetric
reference fixture, not a symmetric rectangle.

Each tile layer uses the selected device, BGRA8Unorm, sRGB, `opaque=YES`,
`framebufferOnly=NO`, `maximumDrawableCount=2`, `displaySyncEnabled=YES`,
`presentsWithTransaction=YES`, and `allowsNextDrawableTimeout=YES`.

Use identity transforms, disabled implicit actions, no edge antialiasing, and no
rasterization/filter effects on the hosting grid. Set `contentsScale` and
`drawableSize` explicitly for each tile. Edge tiles must not stretch a full 512-pixel
image into a smaller rectangle. The root clips the final ceil-rounded viewport
pixel where the logical view ends at a fractional device boundary.

The expected texture size of a drawable is exactly its tile's physical size.
Unexpected size, format, or device is a surface failure, not permission for an
out-of-bounds blit. Grid/layer replacement advances the entire presentation epoch;
this plan does not preserve evidence across piecemeal layer replacement.

### Batch and scheduler state

Keep platform-independent decision logic testable through private scheduler values;
AppKit calls and layer ownership stay in the adapter. This is the production
scheduler's state machine, not an alternate test renderer.

```cpp
namespace stana::native {
enum class FramePhase : std::uint8_t {
    idle, building, awaitingEvidence, occluded, draining,
    recovering, closed, failed
};

struct ActiveBatch {
    FrameIdentity attempt;
    std::uint64_t surfaceEpoch = 0;
    std::uint64_t attachmentGeneration = 0;
    std::shared_ptr<const PreparedScene> target;
    std::vector<TileId> expectedTiles;
    bool gpuFinished = false;
    bool scheduledPresentationIssued = false;
    // Independent resource owner retains source buffers until GPU completion.
};
}
```

The prepared scene can be opaque/forward-declared in a portable header. Actual
Metal/Quartz owners live in the Objective-C++ batch resource object.

Scheduled, completed, and presented callbacks are independent observations. Do not
assume their main-queue delivery forms one linear sequence. Phase and observation
flags are updated only after validating exact identities.

| Current condition/event | Required transition |
| --- | --- |
| Clean/idle, new scene publication | Replace `latestRequested`; queue one main-frame request. |
| Already queued request | Coalesce; do not post another equivalent request. |
| Active attempt, newer publication | Replace pending latest scene only; do not mutate active buffers. |
| Occluded, publication | Retain newest demand; acquire no drawables. |
| Becomes visible/key/attached | Reevaluate pending demand and display trust; request frame. |
| Target built, all drawables acquired | Register/gate exact attempt, encode/commit one command buffer. |
| Some drawables acquired, next acquisition nil | Release all without presenting; retain valid raster result and demand. |
| Scheduled callback for current attempt | Present its drawables in one main-thread transaction. |
| GPU and all tile observations join | Install confirmed target, release active role, schedule latest pending if needed. |
| Negative/expired evidence | Retire attempt and enter bounded recovery. |
| Resize/scale/attachment change | Invalidate epoch immediately; cancel stale presentation and drain owners safely. |
| Close | Stop demand, retire/gate appropriately, detach host; cleanup-only callbacks may remain. |
| Terminal failure | Record diagnostic and stop; do not loop or pretend displayed success. |

### Submission sequence

For one target:

1. Check closed/failed state, native visibility, viewport validity, and active-batch
   backpressure. Do not prepare a queue of every intervening revision.
2. Obtain the newest complete native scene and prepare changed resources.
3. Calculate raster and presentation tile sets against their separate baselines.
4. Rasterize required tiles directly into writable shared buffers.
5. If a new revision has no presentation damage, select the top-left tile as an
   evidence anchor. Reuse its valid pixels; do not force another raster pass.
6. Acquire only the expected tile drawables as late as possible, in row-major order.
   Validate all drawables before admitting/presenting any part of the batch.
7. Build a local owning submission draft. Create the command buffer and encoder;
   encode complete buffer-to-texture copies for every expected tile.
8. Register the exact portable attempt and pin its geometry. Gate changed old/new
   interaction regions before GPU submission can expose new content.
9. Install scheduled/completed/presented callbacks carrying the full attempt and
   generation identities. Start the confirmation deadline at commit.
10. Commit the one command buffer; publish submitted diagnostics, not confirmed
    geometry. Never clear pending newer demand just because a buffer was committed.

The blit uses source offset zero, tile-aligned `bytesPerRow`, 2D
`sourceBytesPerImage=0`, the full tile width/height/depth-one, destination origin
zero, slice zero, and level zero. No source or destination region exceeds its
checked allocation/texture extent.

### Transaction-based presentation

Use `addScheduledHandler` to transport the batch's acquired drawable owners to a
main-queue presentation closure. Return promptly from the Metal callback.

On main:

1. Verify the window is open and the exact attempt, surface epoch, attachment
   generation, and active target still match.
2. If stale/retired, release drawables without presenting them.
3. Begin one `CATransaction`, disable implicit actions, and call `present` on every
   batch drawable using the current transaction.
4. Commit the transaction and release the drawable collection promptly inside an
   autorelease-pool scope.

Do not call command-buffer `presentDrawable:` for this transaction-synchronized
path. Do not call `waitUntilCompleted()` or add a main-thread spin while waiting
for GPU/presentation callbacks. The scheduled callback supplies ordering without
adding a synchronous `waitUntilScheduled()`.

Register per-drawable presented handlers before submission; their captures contain
only value identities and a weak host. They must not capture their own drawable.
Do not retain every previously presented drawable to remember its pixels. The
persistent *layer* retains displayed content; future drawable contents are unknown.

### Why tile-sized full initialization is required

A fresh drawable is never assumed to contain its tile's last image. For each
submitted tile, copy its complete current pixel payload. Unchanged pixels are
preserved by leaving their *other tile layers* untouched, not by leaving part of a
new drawable uninitialized.

Do not key pixel preservation on `drawableID`, a texture object's address, or a
recycled IOSurface identity. This plan deliberately avoids requiring a public
preservation guarantee that the existing CAMetalLayer path does not establish.

A 1600×1200 viewport has 12 tiles with this grid, including smaller edge tiles.
Changing text confined to one full tile copies 512×512×4 = 1,048,576 payload bytes,
not 7,680,000 viewport bytes. A label crossing a tile boundary legitimately updates
more than one tile. A full theme change legitimately updates all 12.

## Evidence, input, and recovery

### Typed portable presentation additions

Extend the existing graphics presentation partition. Keep current singleton
callers source-compatible and use a strong tile value to avoid interchangeable
numeric tile/timestamp arguments.

```cpp
namespace stana {
struct TileId {
    std::uint32_t value = 0;
    auto operator<=>(const TileId &) const = default;
};

struct TilePresentation {
    TileId tile{};
    double timestamp = 0;
};

// Add overloads to the existing PresentationTracker.
auto begin(std::uint64_t epoch, std::uint64_t sceneRevision,
    std::span<const TileId> expectedTiles)
    -> std::expected<PresentationAttempt, Error>;
auto reportPresented(PresentationAttempt attempt, TilePresentation observation)
    -> bool;
auto contains(PresentationAttempt attempt, TileId tile) const -> bool;
}
```

These are additions to the existing class, not namespace-level replacements.
Existing `begin(epoch, revision)` registers singleton tile zero. Existing
`reportPresented(attempt, timestamp)` reports singleton tile zero. `reportGPU`,
`confirmed`, `retire`, `reset`, and `pendingCount` retain their existing roles.

Admission copies the expected set into owned bounded state. Empty sets, duplicate
IDs, ordinals outside `[0,1024)`, or sets larger than 1,024 return
`Error::invalidValue` before registering any attempt. The native scheduler also
validates ordinals against the current grid's actual tile count. Do not advance the active epoch or consume an admission slot for an
invalid tile set. A stale/zero epoch remains invalid; a full attempt history remains
`Error::nativeFailure` under the existing contract.

Each state owns exact attempt identity, GPU-finished status, and positive-evidence
bits keyed by expected tile ID. Confirmation requires GPU success and every bit.
Keep a first finite positive observation; a later zero/invalid duplicate must not
erase it. Unknown tiles have no effect, including no negative input gate.

Any GPU failure retires the attempt. Explicit retirement, epoch reset, and close
are terminal for those identities; late success cannot revive them. Duplicate
valid callbacks do not allocate more state. Callback order is not prescribed.

The native private header mirrors tile identity/observation values without
importing `stana.graphics`. The application adapter explicitly converts them to
`TileId` and `TilePresentation`, just as it currently converts `FrameIdentity`.

### Document/host callback additions

Add these internal document operations alongside the current singleton operations:

```cpp
namespace stana::detail {
// Additions to Document.
auto beginPresentation(std::uint64_t epoch, std::uint64_t sceneRevision,
    std::span<const TileId> expectedTiles)
    -> std::expected<PresentationAttempt, Error>;
auto reportPresentation(PresentationAttempt, TilePresentation) -> bool;
void gatePendingPresentation(PresentationAttempt);
void gateSurface();
}
```

`beginPresentation` validates that the requested revision has an available complete
geometry root, registers the tracker attempt, and pins that root. It does not
implicitly fabricate display evidence. Native composite admission additionally
calls `gatePendingPresentation` before commit. Existing singleton TestHost tests
retain their current pending-frame behavior unless they explicitly choose the
composite gate operation.

Use these additions/replacements inside the private `native::Window` contract
on the current code. This is the same owning header boundary, not a new service:

```cpp
namespace stana::native {
struct TileId { std::uint32_t value = 0; };
struct TilePresentation { TileId tile{}; double timestamp = 0; };
struct PresentationRequest {
    std::uint64_t epoch = 0;
    std::uint64_t sceneRevision = 0;
    std::vector<TileId> expectedTiles;
};
using SceneResult = std::expected<std::shared_ptr<const Scene>, std::string>;

// Fields inside native::Window; existing input/resize/appearance callbacks remain.
std::function<SceneResult()> scene;
std::function<std::expected<FrameIdentity, std::string>(const PresentationRequest &)>
    beginPresentation;
std::function<bool(FrameIdentity, bool)> gpuCompleted;
std::function<bool(FrameIdentity, TilePresentation)> drawablePresented;
std::function<void(FrameIdentity)> gatePendingPresentation;
std::function<void()> gateSurface;
}
```

The string failure channel matches the current private native startup/admission
convention; do not invent a second public error hierarchy. If the omnibus typed
native failure channel has landed, these fields use its existing result alias
instead, preserving the same cancellation/native-failure distinctions.

Private host callbacks extend expected tile IDs and typed tile observations; GPU
completion remains one success/failure result for the batch's single command
buffer. WindowSession owns the document lifetime; native callbacks capture weak
document references, locking and validating before each operation. Expired scene/
admission requests return cancellation/native failure through the current private
result path; teardown callbacks become safe no-ops. No callback borrows a node or
component description.

Add an explicit private callback for `gateSurface` so attachment/scale changes can
invalidate coordinate-driven input even when the logical scene revision is reused.
Do not depend on a resize publication happening before every scale-only event.

### Geometry gates and confirmation

Before composite submission, compare the confirmed geometry root with the pinned
target root. Gate old and new bounds for any changed record, including clipping,
parent/route identity, enabled/inert eligibility, visibility, and insertion/removal.

Unchanged regions can continue using acknowledged geometry. Keyboard/model updates
do not wait for display evidence. Changed regions must not accept location events
against old bounds while some new tile pixels may already be exposed.

Preserve these orderings:

```text
admit attempt → pin geometry → gate changed regions → commit GPU work
GPU success + all required tile times → install pinned geometry → clear eligible gates
```

Maintain the newest revision/generation responsible for uncertainty. Confirmation
of an older target cannot clear gates for a newer unknown revision. On native
surface invalidation gate the viewport immediately; an old-epoch confirmation
cannot clear it even if it carries the same scene revision.

If more than 64 distinct gate rectangles accumulate, conservatively use the current
input viewport coverage instead. Coalescing is allowed to over-gate temporarily;
it may not make an ambiguous location clickable. Removed nodes remain stale even
if acknowledged geometry still contains their old identity.

On successful confirmation, install the attempt's pinned geometry root, acknowledge
the complete target composite, retire older tracker/pin entries, and clear only
uncertainty covered by that confirmation. No full geometry-map rebuild is allowed
on the normal confirmation path.

### Evidence anchor for visually equivalent revisions

A committed metadata/interaction revision can be paint-equivalent to the confirmed
scene. It still needs an explicit native evidence boundary before acknowledging
new interaction geometry under this contract.

Use tile zero as the anchor:

- Ensure its raster buffer matches the target; repair raster-only damage first.
- Acquire one fresh anchor drawable and copy the complete tile.
- Admit a new attempt for the target revision with expected set `{tile zero}`.
- Gate changed geometry across the document, not just geometry inside the anchor.
- Confirm only after that real anchor GPU/presentation evidence joins.

Reusing the other tiles is valid only because complete-scene damage comparison
proved their target pixels unchanged and their attachments remain confirmed.
Never represent this as every tile being newly presented. Diagnostics distinguish
an anchor attempt from raster work. There is no anchor for a zero-size viewport.

### Drawables unavailable before admission

All-or-none acquisition is mandatory: the batch cannot present its first acquired
tiles and later discover that another required drawable is unavailable.

1. On a nil drawable, release all drawables acquired for that draft. Do not call
   `present` on any of them and do not advance submitted/confirmed diagnostics.
2. Keep successful raster pixels and latest demand. They are not display evidence.
3. Recheck actual visibility/attachment after acquisition returns. If occluded or
   detached, enter `occluded`, cancel the delayed acquisition retry, and wait for
   the corresponding native event. Do not consume recovery allowance for occlusion.
4. If still visible, schedule exactly one retry after 16 ms for that generation.
5. A second consecutive visible failure enters the one allowed full-grid recovery.
6. During that recovery, permit the same initial acquisition plus one delayed
   retry. A second visible failure there is terminal native failure.

Only one acquisition-retry timer may be pending per window. Timers carry the
surface/attachment generation and do nothing after replacement, success, or close.
No immediate polling loop and no timer per missing tile are allowed.

### Failure after admission

Start one 1,000 ms confirmation timer at command-buffer commit. A missing/zero/
nonfinite tile observation leaves the batch unconfirmed; the deadline resolves
missing evidence. A GPU error is immediately a failed attempt.

On failure or expiry:

1. Retire the exact portable attempt and retain any immutable geometry needed to
   establish conservative input uncertainty before releasing the pin.
2. Mark submitted display state untrusted; do not reuse partial positive tile
   evidence as a complete composite.
3. Advance the presentation epoch, invalidating scheduled presentation closures
   and subsequent callbacks from the failed attachment.
4. Gate current surface geometry and retain newest requested scene.
5. If no recovery has been consumed since the last successful confirmation,
   consume one recovery cycle and request a completely initialized replacement grid.
6. If GPU access is unfinished, enter `draining`; retain source-buffer ownership
   and defer replacement allocations when needed to stay within raster budget.
7. A current GPU-completion cleanup event can finish draining even if its display
   evidence is now stale. It cannot confirm the new epoch.
8. If drain cannot complete within the additional 1,000 ms deadline, terminate
   with a GPU-drain diagnostic. Do not wait synchronously or reuse unsafe memory.
9. If the full-grid recovery also fails, terminate rather than starting a new
   allowance because the scene revision happened to change.

Reset recovery allowance only after a successful joined composite confirmation.
Successful CPU drawing, GPU completion alone, key-window changes, or new model
signals do not reset it.

Native allocation/context/encoder failures are diagnosed explicitly. Invalid
geometry/overflow is rejected before allocation. A nil command queue/device at
startup is not a recoverable draw loop. Keep the existing no-silent-CPU-fallback
behavior and provide actionable failure messages through current run/diagnostics.

### Resize, scale, attachment, and appearance

| Event | Retained core work | Native work/evidence policy |
| --- | --- | --- |
| Logical viewport resize | New viewport proposal, affected cached layout and geometry | Advance surface/presentation epochs, gate, rebuild grid, fully initialize it. |
| Backing scale only | No invented point-layout change | Advance epochs, invalidate scale-dependent preparation and grid; new evidence may reuse scene revision. |
| Window/view attachment replaced | Preserve complete logical scene | New presentation epoch; no old tile content/evidence reused. |
| Light/dark appearance | Existing environment/theme assignment | New paint and background damage; reuse grid if surface key unchanged. |
| Becomes key | Existing input behavior | Request demand evaluation; do not unconditionally rerasterize all valid tiles. |
| Occluded | Core/model still settle normally | Suppress drawable acquisition; retain only latest demand. |
| Reappears with valid layers | No forced remount | Reevaluate trust/demand; unchanged layers can remain; do not invent reappearance timestamps. |
| Surface uncertainty on reappearance | No forced remount | New epoch/full-grid recovery as required by trust state. |
| Window close | Dispose scopes/retire handles | Stop demand, detach layers, cleanup-only in-flight ownership. |

Do not allocate both a full obsolete grid and a full replacement grid in violation
of the shared-buffer budget. Detach/retire obsolete state immediately but defer
physical release until GPU readers finish. Release safe obsolete buffers before
allocating the new grid. During that transition the host may show its opaque
background/old clipped content; do not promise a perfectly seamless resize.

### Lifetime and cycle rules

Use an independent batch-resource owner for source buffers/prepared resources.
Metal's completion handler may own that resource owner, but the owner must not
strongly own the command buffer that owns the handler. Break that potential cycle.

Scheduled-presentation closures temporarily retain acquired drawables, but only
until their transaction is issued or discarded. Presented handlers do not retain
their own drawable. Resource callbacks carry a weak `WindowState`; document/window
closure cannot be undone by late locking/capture.

Closing releases the host's current/pending roots and layer hierarchy. It does not
prematurely free source pixels still read by GPU work. There is no call to user
application code while holding renderer-resource iterators or mutable node pointers.

### Hardware selection and compatibility

Require a Metal device supporting Apple-family GPU capability and unified memory.
Check Metal capabilities, not a parsed product name. `hasUnifiedMemory` alone is
not the Apple-Silicon-only check because other integrated hardware can share memory.

Keep the current macOS 26 deployment minimum. Unsupported native hardware returns
an explicit diagnostic before creating a partially functioning tile host. Do not
fall back to the old image upload renderer or introduce managed-buffer support
contrary to the selected scope.

Intel/macOS and non-Apple platforms retain portable/headless engine support where
the existing toolchain permits it. Apple-only headers and source files must not
leak into portable core/scene/geometry tests. Update README hardware claims and
native test prerequisites accordingly.

## Diagnostics and public compatibility

### Core metrics additions

Add the following fields to the existing `Metrics` aggregate. Preserve current
mount/property/scene counters and do not reinterpret them as GPU work.

```cpp
struct Metrics {
    // Existing fields remain in their existing order.
    std::uint64_t measurements = 0;
    std::uint64_t arrangements = 0;
    std::uint64_t paintBuilds = 0;
    std::uint64_t projectionBuilds = 0;
    std::uint64_t nodeMaterializations = 0;
    std::uint64_t sceneMaterializations = 0;
};
```

Counter definitions:

- `measurements`: actual local measurement computations; cache-return calls do not
  increment it. Aggregate-container recomputation counts even if its result matches.
- `arrangements`: actual local placement computations, not traversal through a dirty
  ancestor route that simply forwards its child's unchanged assigned bounds.
- `paintBuilds`: local prefix/suffix regeneration, not ancestor path copying.
- `projectionBuilds`: new immutable branch objects, including ancestor path copies.
- `nodeMaterializations` / `sceneMaterializations`: complete lazy flat cache builds,
  not every API read and not merely returning an existing materialization.
- Existing `layoutPasses` now means commits performing measurement or arrangement.
  Paint/metadata-only commits do not increment it; document the changed meaning.

### Native diagnostics additions

Append a rendering-work value to `WindowDiagnostics`; do not change or remove
`submittedSceneRevision`, `confirmedSceneRevision`, or `lastFailure`.

```cpp
struct RenderingWork {
    std::uint64_t tileCount = 0;
    std::uint64_t rasterizedTiles = 0;
    std::uint64_t presentedTiles = 0;
    std::uint64_t rasterizedPixels = 0;
    std::uint64_t copiedBytes = 0;
    std::uint64_t cpuUploadBytes = 0;
    std::uint64_t bufferAllocations = 0;
    std::uint64_t rasterBytes = 0;
    std::uint64_t peakRasterBytes = 0;
    std::uint64_t estimatedDrawableBytes = 0;
    std::uint64_t fullDamageFallbacks = 0;
    std::uint64_t recoveries = 0;
    std::uint64_t anchorAttempts = 0;
    bool hasActiveBatch = false;
    bool hasPendingScene = false;
};
```

Use explicit private/native statistics conversion in the application bridge.
Counters are cumulative except current gauges (`tileCount`, `rasterBytes`,
`estimatedDrawableBytes`, active/pending flags). `copiedBytes` increments for
committed buffer-to-texture pixel payloads; `presentedTiles` increments when
presentation is issued, not when a raster buffer is merely prepared. Confirmation
continues to be represented by acknowledged revision/evidence, not by that count.

`cpuUploadBytes` must remain zero in the new production path. Do not include small
command/string copies in that metric or imply that a zero value proves the system
compositor performed no copies. Estimate drawable payload as the sum of tile pixel
payload × configured drawable count; label it as an estimate, not measured VRAM.

Trace mode should include scene revision, surface/presentation epoch, sequence,
expected tile count/IDs, raster tile count, copied bytes, GPU status, per-tile
presentation result, and recovery reason. Preserve `STANA_TRACE_NATIVE` as the
opt-in switch; do not add noisy unconditional frame logs.

### Compatibility inventory

Unchanged: public component aggregates, `Drawing` builders, `PaintCommand`, flat
`SceneSnapshot::commands`, `NodeHandle`, basic `TestHost` inspection, singleton
presentation APIs, and explicit native startup semantics.

Additive/changed where necessary: typed tile evidence, metrics/diagnostics fields,
private render-tree/transport contracts, composite TestHost seams, and documented
`layoutPasses` meaning. Native hardware support intentionally narrows to Apple
Silicon; portable engine support does not.

There is no persistent document/on-disk schema migration. Render snapshots are
process-local values and never a stable plugin ABI. Do not add serialization,
network formats, runtime configuration services, or application-facing tile knobs.

## Verification, delivery, and completion

### Test architecture and production-path seams

Extend existing suites instead of hiding all coverage in a graphical demo:

| Suite | Registration | Responsibility |
| --- | --- | --- |
| `tests/graphics/incremental.cpp` | Default portable CTest | Classified invalidation, caches, persistent snapshots/indexes, reference comparison. |
| `tests/graphics/tiles.cpp` | Default portable CTest | Checked grid math, dirty tile selection, edge/budget cases. |
| `tests/graphics/presentation.cpp` | Existing default portable suite | Composite tracker, pins, gates, production scheduler transitions. |
| `tests/platform/macos/raster.mm` | Default Apple CPU-only CTest | CoreGraphics full-reference/tile pixel parity; no Metal device or NSApplication required. |
| `tests/platform/macos/shared_copy.mm` | Opt-in `STANA_NATIVE_TESTS` | Actual shared-buffer writes/blits/readback on Metal. |
| `tests/platform/macos/window_smoke.cpp` | Existing opt-in native gate, expanded | Real layered window, positive tile timestamps, update/resize/occlusion/shutdown. |

Portable tests must not compile Objective-C++ or initialize a native application.
Native CPU raster tests can use ordinary writable reference buffers and Core Text;
they must assert no `NSApplication` was created before/after the test. GPU tests
and window-server tests remain explicit opt-in hardware gates.

Use the same production invalidation, preparation, damage, scheduler, and evidence
code in tests. A fake sink can record scheduler decisions and supply native
outcomes; it cannot replace the algorithm with a second implementation that always
passes. Fault-injection seams stay private to tests/native adapter composition.

Add these deliberate TestHost operations:

```cpp
// Additions to TestHost, not application authoring APIs.
void forceFullRecompute();
auto renderComposite(std::span<const TileId> expectedTiles)
    -> PresentationAttempt;
void reportTilePresentation(PresentationAttempt, TilePresentation);
```

`forceFullRecompute` invalidates all production layout/projection caches and commits
from current retained input, giving an oracle for reuse behavior without a second
layout runtime. Ignore revision/counter differences when comparing semantic output.
`renderComposite` admits through production Document operations and enables the
pending changed-geometry gate. Existing `render(bool)` remains singleton behavior.
No synthetic operation claims actual macOS display proof.

### Core regression matrix

Use named scenarios with explicit assertions, not only that a window did not crash.

| ID | Scenario | Required assertion |
| --- | --- | --- |
| C01 | Equal signal/resolved assignment | No extra layout or scene publication; initial semantic installation still occurs. |
| C02 | Foreground change inside a fixed label | Zero measurements/arrangements; one local paint build; ancestor projection copies only. |
| C03 | Same-size numeric text change | Changed text measured; unrelated text provider calls stay zero; unchanged siblings not rearranged. |
| C04 | Text grows in VStack/HStack | Dependent following siblings move; unrelated sibling subtrees reuse measurements. |
| C05 | Fixed/capped ancestor result unchanged | Stop changed-size propagation while still arranging affected internal children. |
| C06 | Padding changes with same outer frame | Local content position and clip/text paint update despite equal node outer bounds. |
| C07 | Fill/percentage/available-size change | Keys include proposal changes; final bounds match forced-full current algorithm. |
| C08 | Parent moves with identical size | Descendant document coordinates/geometry/paint update. |
| C09 | Scroll offset changes | No text remeasurement solely for motion; correct clamp/clip and old/new bounds. |
| C10 | Visible ↔ hidden | Same layout footprint; subtree disappears/reappears in paint/visible geometry. |
| C11 | Enter/leave collapsed | Zero stale measured footprint; siblings and descendants match full recomputation. |
| C12 | Nested disabled/opacity changes | Current multiplicative alpha preserved; eligibility/snapshots update without layout. |
| C13 | Inert or metadata-only update | Correct inspection/input state; no invented visual style or raster damage. |
| C14 | Focus old → new and repeated same focus | Only old/new local outlines update; repeated valid focus is no-op. |
| C15 | Keyed insert/remove/reorder | Stable surviving identities/lifetimes; correct painter order and dependent layout. |
| C16 | Cleanup reenters/removes siblings | Retired handles already stale; no double cleanup or dirty references to freed records. |
| C17 | Provider/text-scale replacement | Relevant measurement keys miss; unrelated nontext work remains bounded. |
| C18 | Theme-only update | Zero measurement; correct default text/background and explicit foreground handling. |
| C19 | Repeated inspection | One flat materialization/revision; old owning scene unchanged after later commits. |
| C20 | Production conversion without inspection | Flat-materialization counters do not increase. |
| C21 | Measurement/projection failure | No half-new published snapshot or counter advancement; dirty work remains recoverable/diagnosed. |
| C22 | Geometry AVL rotations/removal | Keyed values correct, immutable old roots valid, rotations do not create fake geometry changes. |

Add a deterministic differential suite using a fixed seed recorded in the test
source and at least 1,000 valid edit operations per representative fixture. Exercise
text/style/visibility/spacing/frame/structure/scroll changes; capture incremental
bounds/nodes/paint, force full recomputation, and compare complete logical output.
Do not compare revision numbers, resource addresses, or expected work counters as
semantic paint equality. Retain old snapshots during the sequence to detect
accidental mutation through shared branches.

### Damage, clipping, and pixel-reference matrix

The full-scene raster reference draws the complete prepared scene into one bitmap
without damage/tile culling. Tile assembly must satisfy the user-approved edge-bounded comparison above under
the same pixel/color/antialiasing configuration. Ignore row padding only. Uniform
regions and alpha are exact; incremental versus fresh tile replay is byte-exact.
Do not use an unrestricted screenshot tolerance or relax stale-pixel checks.

| ID | Scenario | Required assertion |
| --- | --- | --- |
| R01 | Local color edit | Changed tile reconstructed; all other assembled pixels unchanged. |
| R02 | Text shortens/changes overhang | Old glyph ink erased beyond new layout/text extent. |
| R03 | Combining/fallback/emoji text | Prepared resource reuse and conservative ink or explicit full-damage fallback; no stale glyphs. |
| R04 | Node moves/removes | Old location cleared and underlying overlap repainted. |
| R05 | Translucent overlap | Correct background and painter-order reconstruction, no alpha accumulation. |
| R06 | Z-order/reorder | Overlap matches full rendering, even for shared retained row identities. |
| R07 | Border/focus/hairline overflow | Pixels outside layout bounds included in damage and tile selection. |
| R08 | Canvas overflow | Out-of-layout but in-viewport ink updates and erases correctly. |
| R09 | Nested/rounded clip changes | Expansion/contraction and suffix outline behavior match reference. |
| R10 | Malformed Canvas clips | Underflow/unbalanced stream rejected before publication/submission. |
| R11 | Tile edge/corner crossing | No gap, duplicate coverage, filtering seam, or text phase shift. |
| R12 | Fractional points at 1×/2× | Exact global sampling phase retained. |
| R13 | Odd dimensions/partial edge tiles | No stretch/read-overrun; reference parity for asymmetric markers. |
| R14 | Background/theme | All required tile clears; no previous-theme pixels remain. |
| R15 | Skipped B between A and C | Direct complete-snapshot comparison produces correct latest image. |
| R16 | Raster B, failed acquisition, return to A | Raster baseline repairs B pixels even though display baseline A matches target. |
| R17 | Unknown text/operation ink | Full viewport damage recorded; no unsafe guessed extent. |
| R18 | Repeat unchanged scene/expose | No unnecessary rasterization; content trust and evidence rules retained. |

Use representative sizes both aligned and unaligned to tiles, including 1025×769
physical pixels and logical equivalents producing odd 2× dimensions. Include
asymmetric top/bottom/left/right markers so coordinate flips cannot pass by symmetry.
Test every current PaintKind and combinations, not only isolated opaque rectangles.

### Grid and resource-boundary tests

- Exactly 512×512 creates one full tile; 513×512 creates one full and one 1-pixel
  edge column; 512×513 creates a 1-pixel edge row.
- Row-major ordinals and frames are unique, cover the viewport exactly, and never
  produce zero-sized allocated edge tiles.
- A zero viewport creates no resources/evidence; negative/nonfinite logical values
  and nonpositive/nonfinite scale are rejected before integer conversion.
- Dimensions just above 16,384, tile-count overflow, align-up overflow, device
  `maxBufferLength`, and total raster budget boundaries are checked before allocation.
- Include edge-tile row padding in allocation accounting, not in visible copy bytes.
- Test current plus retired bytes across repeated resize, including a replacement
  that must wait for safe old-buffer retirement rather than exceed 256 MiB.
- After warm-up, repeated in-grid small edits reuse buffers/contexts.
- Closing with GPU work in flight keeps sources alive until completion, then frees
  them without resurrecting host state or retaining a command-buffer cycle.

### Presentation, input, and scheduler matrix

| ID | Scenario | Required assertion |
| --- | --- | --- |
| P01 | GPU before tile times | No confirmation until every expected tile has positive evidence. |
| P02 | Tile times before GPU | Same result; callback order does not matter. |
| P03 | One missing tile | Entire composite unconfirmed, deadline/recovery remains armed. |
| P04 | Empty/duplicate/out-of-range/oversized expected set | Admission rejected with no state/epoch/slot mutation. |
| P05 | Unknown tile/forged attempt | No confirmation, input poisoning, or state growth. |
| P06 | Zero/NaN/infinite timestamp | Never positive evidence; later valid duplicate follows the documented join rules. |
| P07 | Positive then invalid duplicate | Positive evidence is not erased. |
| P08 | GPU failure then late success | Retired attempt cannot revive. |
| P09 | Surface reset, same scene revision | Old epoch evidence and scheduled presentation rejected. |
| P10 | Changed geometry before batch completion | Old/new changed locations gated; unchanged locations use confirmed geometry. |
| P11 | Older confirmation after newer uncertainty | Newer gate is not cleared. |
| P12 | >64 later document publications | Active admitted root stays pinned and confirms its exact geometry. |
| P13 | Newest-demand coalescing | One active batch plus latest pending, no revision-length frame queue. |
| P14 | Partial drawable acquisition | None presented; acquired owners released; valid raster result retained. |
| P15 | Occlusion during failed acquisition | No recovery allowance consumed; wake on native visibility event. |
| P16 | Visible nil twice | One recovery cycle; persistent recovery nil becomes terminal. |
| P17 | Submission deadline | Exact attempt retired, input gated, full-grid new-epoch recovery. |
| P18 | GPU still executing at timeout | No unsafe buffer reuse; bounded draining then failure if needed. |
| P19 | New revision during recovery | Does not reset allowance; recovery targets latest complete scene. |
| P20 | Visually equivalent new revision | One real anchor presentation, no fake timestamp or forced raster. |
| P21 | Close before scheduled main closure | Closure drops drawables; no post-close present/publication. |
| P22 | GPU completes before scheduled closure is delivered | Independent flags handle order; source resources remain valid. |
| P23 | Surface changes after scheduling but before transaction | Exact generation check discards old transaction. |
| P24 | Confirmation/pin retirement repeated past history bound | No unbounded ownership or eventual admission exhaustion. |

Use a deterministic injected clock/event queue for deadlines and retry tests. Do
not add wall-clock sleeps to portable tests. Test callbacks through the production
tracker/document/scheduler operations, not by manually setting confirmed fields.

### Native hardware qualification

Extend the opt-in real Metal/window fixtures. Programmatic handle activation alone
does not qualify the rendering changes.

Required real-device observations:

1. Draw into shared buffer memory with CoreGraphics; blit to a texture; read back
   after proper GPU completion and compare expected pixels, including padded rows.
2. Open the actual tile-layer window and obtain positive native tile presentation
   timestamps. GPU success or a screenshot alone is insufficient.
3. After initial confirmation, mutate a label well inside one tile. Assert native
   diagnostics show one tile raster/copy and no unrelated drawable acquisition.
4. Move/change content across tile boundaries and verify every expected tile's
   evidence plus assembled reference pixels for the rendered image.
5. Exercise logical resize and backing-scale change without stale-epoch confirmation,
   reused old-scale buffers, or visible tile seams.
6. Occlude/reveal the window while publishing many scene changes. Confirm the latest
   complete target, bounded pending demand, and correct retained content.
7. Use two independent windows with different sizes/content; evidence/resources and
   retry allowance remain window-specific.
8. Close with pending work; verify the process exits and no late publication occurs.
9. Run representative 3840×2160 (40 tiles) and 7680×4320 (135 tiles) surfaces, recording
   layer count, estimated surfaces, actual raster bytes, peak ownership, and native
   callback behavior. Do not silently reduce coverage because large grids reveal
   resource/layer overhead.

The tiled layer/transaction path has a hard feasibility gate. If public-API behavior,
resource bounds, or pixel parity cannot meet it, stop and record the blocker in this
plan. Do not substitute unsupported recycled-drawable preservation or mark the
work complete based only on the portable state machine.

### Performance qualification and work-count acceptance

Use fixed deterministic fixtures and actual operation counts as hard gates. Record
wall-clock CPU/GPU timings as supporting evidence, including hardware, scale,
viewport, build type, iteration count, warm-up, and workload.

| Workload after warm-up | Hard acceptance |
| --- | --- |
| Equal update | No new scene, local work, raster, drawable copy, or allocation. |
| In-tile foreground edit | Zero measurement/arrangement; affected local paint + ancestor branch copies; one dirty tile. |
| Same-size in-tile text edit | No unrelated text provider calls or sibling rearrangement; affected tile only. |
| Boundary-crossing text | Only conservatively intersecting tiles plus required overlap; full-reference parity. |
| Container size/reflow | Actual dependency frontier changes correctly; do not require artificially tiny damage. |
| Scroll | Valid measurements reused; necessary viewport tiles updated; no scroll-blit claim. |
| Theme | Full required damage accepted; buffers reused; zero CPU image upload. |
| Native production frame | Flat scene/node materialization counters do not grow unless inspection explicitly requests them. |
| Stable grid | Shared-buffer allocation count constant across repeated edits. |
| Coalesced burst | One active target + one latest pending role; no unbounded frame/resource queue. |

`copiedBytes` for a committed batch equals the sum of `tile.width × tile.height × 4`
for its expected tile set, not viewport bytes or aligned source-allocation bytes.
`cpuUploadBytes` is zero for all workloads. Do not claim faster rendering merely
because the API name changed from texture upload to shared allocation.

No universal FPS or percentage speedup is a release criterion. Regressions in
sparse work counts or correctness are failures even if an unrelated timing sample
looks favorable. Large-layer overhead is measured and recorded explicitly.

### Detailed delivery sequence

Each phase is a reviewable increment with its own tests. All boxes are pending;
writing this specification does not authorize executing its implementation steps.
Do not defer tests, ownership rules, or native evidence to an unspecified final pass.

#### Phase A — Reference behavior and instrumentation

**Dependency:** none beyond the current checkout/toolchain.

- [ ] Extract or expose the current complete paint replay as a private full-scene
  reference without changing its typography, alpha, orientation, or painter order.
- [ ] Add deterministic fixtures for every existing paint operation and representative
  nested layout/clip/visibility/overlap cases.
- [ ] Add core work counters with documented meanings; sample counters before any
  inspection call that intentionally materializes a flat view.
- [ ] Add private fixture recording of prepared branch IDs and drawable acquisitions
  so native reuse assertions prove actual work, not just inferred scene equality.
- [ ] Capture current public authoring/inspection/presentation consumer examples.
- [ ] Record existing Debug/Release contract baseline and platform prerequisites.

**Exit gate:** reference fixtures and counter definitions are reviewed; no intended
visual/layout semantic change is hidden in the baseline extraction.

#### Phase B — Classified core invalidation and cached layout

**Dependency:** Phase A reference and counters.

- [ ] Add typed dirty domains, node input generations, and per-domain dirty-child
  routes. Keep the cheap document pending-work marker separate.
- [ ] Convert first-party bindings and direct retained mutations using the matrix.
- [ ] Preserve initial explicit-foreground semantics and property delivery counters.
- [ ] Add measurement keys/results and dependency propagation, including collapsed
  zero-size storage and changed available-size/provider inputs.
- [ ] Add arrangement keys and dirty-route-only forwarding, including same-size
  text, dependent stack siblings, parent motion, scrolling, RTL, and fill cases.
- [ ] Make focus old/new-local and scroll arrangement-only invalidations explicit.
- [ ] Preserve current transaction behavior and full invalidation as a diagnostic
  oracle, not the default optimized path.
- [ ] Pass C01–C18 and differential layout cases before changing native rendering.

**Exit gate:** measured/arranged results match forced-full computation, and sparse
paint/text changes meet their core work-count assertions.

#### Phase C — Persistent projections, geometry, and safe retirement

**Dependency:** stable dirty routing and layout keys.

- [ ] Implement immutable `PaintSegments`, `SceneBranch`, `RenderSnapshot`, and
  staged promotion/publication.
- [ ] Generate and share local painter segments independently of ancestor path copies.
- [ ] Validate Canvas clip/geometry invariants before publication.
- [ ] Implement the private persistent AVL geometry index and keyed semantic diff.
- [ ] Switch history to shared roots and pin exact geometry at attempt admission.
- [ ] Retire/detach removed identities before reentrant cleanup, keeping owning
  disposal batches and removing all surviving dirty-route references.
- [ ] Implement lazy flat compatibility views without mutating old snapshots.
- [ ] Add `forceFullRecompute` through the same production algorithms.
- [ ] Pass old snapshot/geometry/lifecycle tests and all relevant C19–C22 cases.

**Exit gate:** local edits build affected local segments and ancestor paths only;
paint-only geometry roots remain shared; normal history/confirmation does not copy
all node rectangles; existing public flat inspection consumers still work.

#### Phase D — Native tree conversion and prepared resources

**Dependency:** complete immutable render snapshot available to the bridge.

- [ ] Replace production flat scene retrieval with complete tree conversion; adapt
  the current full-window raster host to replay the prepared tree while retaining
  its existing display path until the tile-host switch below.
- [ ] Pair previous/current branches by identity, sharing unchanged local strings
  and paint transport.
- [ ] Extract private native renderer units from `native_host.mm` and register them
  in the existing Apple library target.
- [ ] Implement immutable paths/Core Text resources and conservative ink metadata.
- [ ] Preserve existing baseline, foreground/alpha, and clip behavior exactly.
- [ ] Implement complete-scene damage comparison, including insert/remove/move,
  reorder, clipping, unknown bounds, and skipped revisions.
- [ ] Verify native conversion triggers no flat inspection materialization.
- [ ] Pass CPU-only preparation/raster reference cases without NSApplication.

**Exit gate:** unrelated prepared resources survive local changes and the complete
native image still matches the reference before damage suppression is enabled.

#### Phase E — Shared-buffer rasterization

**Dependency:** prepared native scene and reference raster entrypoints.

- [ ] Implement checked tile-grid/row-pitch/budget arithmetic in portable units.
- [ ] Allocate shared Metal buffers and create bitmap contexts over their contents.
- [ ] Implement tile-relative replay with global pixel phase and full dirty-tile clears.
- [ ] Encode buffer-to-texture blits with checked source/destination geometry.
- [ ] Ensure the new shared renderer has no vector-backed bitmap upload or
  intermediate scene texture. Keep current host composition wired until Phase F
  qualifies and switches to the tile path; do not add a public runtime toggle.
- [ ] Add independent GPU resource owners and current/peak allocation accounting.
- [ ] Pass actual shared-memory write/blit/readback tests on Apple Silicon.
- [ ] Prove zero production image-upload bytes and warm buffer reuse.

**Exit gate:** the new shared-buffer path reaches Metal correctly, reference parity
holds, and source storage cannot be freed/overwritten during a GPU read. This phase
qualifies that path offscreen; it does not claim the still-wired old host is already
using zero-upload presentation.

#### Phase F — Tiled native presentation feasibility

**Dependency:** shared tile raster resources and portable composite tracker additions.

- [ ] Add typed expected-tile tracking while preserving existing singleton APIs.
- [ ] Build the persistent tile-layer hierarchy with explicit frames/scale/orientation.
- [ ] Add one-batch ownership, row-major all-or-none drawable acquisition, and
  transaction-based scheduled presentation.
- [ ] Validate exact per-tile positive native timestamps and aggregate confirmation.
- [ ] Verify edges/partial tiles with asymmetric visual markers and 1×/2× parity.
- [ ] Exercise 4K/8K tile counts, two-window separation, and repeated resize.
- [ ] Record the feasibility result, resource measurements, and any public-API blocker.
- [ ] After the gate passes, switch the production composition root to the qualified
  tile host, remove the old production bitmap-upload branch, and run the existing
  native smoke through the new default. Retain no permanent fallback/toggle.

**Exit gate:** the real tile-layer path meets pixel, evidence, and resource rules.
This is a hard gate before claiming the final-copy optimization works. Failure
leaves this phase incomplete and the reason documented; no silent alternate design.

#### Phase G — Incremental raster/presentation demand and input gates

**Dependency:** feasible fully initialized tile presentation.

- [ ] Track raster baseline, confirmed composite, active target, and latest pending
  scene as distinct roles.
- [ ] Enable tile-damage suppression without assuming preserved drawable pixels.
- [ ] Skip unchanged tiles' CPU drawing, acquisition, blits, and presentation.
- [ ] Add newest-scene coalescing and paint-equivalent anchor attempts.
- [ ] Add pending changed-geometry gates and surface invalidation gates.
- [ ] Prove pinned roots survive publication churn and older confirmations do not
  clear newer uncertainty.
- [ ] Pass all damage/revert, sparse-work, and composite input tests.

**Exit gate:** the intended end-to-end sparse workload avoids unrelated core,
preparation, raster, and copy work while maintaining acknowledged input geometry.

#### Phase H — Recovery, teardown, and bounded ownership

**Dependency:** normal incremental path and exact evidence identities.

- [ ] Add visible nil retry, occlusion suppression, and all-or-none draft cleanup.
- [ ] Add confirmation/drain deadlines with an injected-clock test seam.
- [ ] Enforce one recovery allowance reset only by successful display confirmation.
- [ ] Gate/reset new epochs, discard stale scheduled transactions, and fully initialize
  replacement grids without unsafe memory reuse or excess raster allocation.
- [ ] Close with active GPU/presentation ownership and verify cleanup-only callbacks.
- [ ] Pass P14–P24 plus resource lifetime/resize stress scenarios under sanitizers.

**Exit gate:** every failure/cancellation path is bounded, observable, and incapable
of inventing display proof or resurrecting a closed document.

#### Phase I — Distribution, docs, and final evidence

**Dependency:** all functional phases and hard native feasibility gate.

- [ ] Update README and architecture docs with the new pipeline, Apple-Silicon-only
  native requirement, dirty-tile granularity, and remaining tile-sized copy.
- [ ] Update diagnostics/Doxygen and `layoutPasses` counter meaning.
- [ ] Extend external consumer examples for additive public types/fields.
- [ ] Run the existing Debug/Release, policy, format, consumer/install/relocation,
  and applicable sanitizer gates plus all new suites.
- [ ] Record real native evidence, sparse work counts, and representative timings.
- [ ] Audit that no obsolete full-window production upload path or transitional
  host wiring remains; preserve only the explicitly named full-scene reference
  used for verification.
- [ ] Audit strong ownership and pending-history/cache bounds one final time.
- [ ] Mark completion only when the record below contains all required evidence.

**Exit gate:** no outstanding required portable/native gate and no unsupported
performance/zero-copy claim in user-facing documentation.

### Quality-gate commands for the future implementation

These commands belong to execution of the future plan; they were not run merely
by writing this document. Use the repository's pinned toolchain and existing
presets rather than bypassing analysis to get a build through.

```sh
cmake --preset debug
cmake --build --preset debug --parallel 3
ctest --preset debug
cmake --build build/debug --target format-check

cmake --preset release
cmake --build --preset release --parallel 3
ctest --preset release

cmake --preset asan-ubsan
cmake --build --preset asan-ubsan --parallel 3
ctest --preset asan-ubsan
```

For real native gates, configure the appropriate Apple build with
`-DSTANA_NATIVE_TESTS=ON`, rebuild, and run its `native`-labelled tests in a logged-in
window-server session with Metal access. Keep GPU/GUI gate failures explicit when
that environment is unavailable. Do not report an unregistered/skipped native test
as a pass. Native fixture timeouts must cover deliberate deadline/recovery cases
without turning them into indefinite waits.

Existing consumer testing already covers source/install/relocation behavior; keep
it enabled in ordinary unsanitized Debug and Release configurations. Add new source
units to normal policy/format scanning. Fix diagnostics; do not add suppression
pragmas, sanitizer exclusions, or analysis bypasses to accommodate the new code.

### Cross-plan integration rules

The omnibus backlog describes future text-layout resources, paired preparation/
publication callbacks, bridge state, and richer presentation geometry. This plan
must compose with those contracts without forcing that entire backlog to land first.

- On the current code, stage the new projection within the existing settled document
  commit path. Do not add a second reactive runtime or global responsive-reader
  settlement loop merely for rendering reuse.
- If the backlog's `PreparedDocument`/`DocumentCommitCallbacks` contracts have landed
  before implementation reaches this phase, place the persistent render/geometry
  roots in that existing draft/publication path. Do not keep a competing draft
  publication mechanism beside it.
- Keep failed-state retry bounded: retain the last complete publication and block
  repeated preparation of the same failed input/service stamp until that stamp
  changes. Do not repeatedly spin on an identical invalid shape/provider failure.
- If the richer owning native text resource already exists, reuse it for preparation,
  ink, and drawing. Do not reshape independently or make tile rendering own another
  editable-text layout service.
- If the native `WindowBridge`/typed failure transport already exists, extend it with
  scene-tree/composite callbacks. Otherwise use owning WindowSession lifetime plus
  weak-document native callbacks and the current explicit native failure path;
  do not introduce the full host-service architecture solely for this migration.
- New paint kinds added by other work must implement ink/damage/replay rules or
  explicitly trigger conservative full damage. A new blur/shadow cannot inherit the
  current zero-gutter tile assumption without its own sampling-neighborhood rule.
- Do not merge unrelated layout-fidelity fixes into reference outputs. Coordinate
  their separate semantic tests if they land concurrently.

### Completion record

All rows start pending. Record commands/build configuration, hardware/scale where
relevant, exact scenario, observed result, and unresolved failures. Evidence should
be reproducible, not a statement that implementation was attempted.

| Gate | Status | Evidence to record |
| --- | --- | --- |
| Classified invalidation/layout | Pending | C01–C18 and counter assertions. |
| Persistent projection/geometry | Pending | C19–C22, snapshots/history/retirement, churn tests. |
| Differential randomized core | Pending | Seed, fixture list, operation count, semantic comparison. |
| Portable grid/budget arithmetic | Pending | Edge dimensions, overflow, padding, current+retired storage. |
| CPU full/tile pixel parity | Pending | R01–R18, 1×/2×, edge sizes, glyph/clip cases. |
| Shared-buffer GPU transfer | Pending | Hardware, actual blit/readback, zero upload count. |
| Composite evidence/input | Pending | P01–P24 and exact production seams exercised. |
| Tile-layer feasibility | Pending | Real timestamps, 4K/8K grid/resource record, transaction behavior. |
| Native lifecycle | Pending | Resize/scale/occlusion/two windows/close. |
| Sparse work/performance | Pending | Warm operation counts and contextualized timing samples. |
| Policy/format/Debug/Release | Pending | Commands and results with pinned analysis enabled. |
| Consumer/install/relocation | Pending | Existing external consumer results with additive APIs. |
| Sanitizers | Pending | Applicable preset/results and any actual platform limitation. |
| Hardware/docs/API audit | Pending | Accurate support/copy claims and no accidental authoring break. |

Planning baseline only: six focused existing Debug suites previously passed during
repository exploration. Those existing binaries do not validate this proposed
renderer. No implementation or new native qualification is claimed here.

### Definition of done

Completion requires all of the following, not merely a compiled native window:

1. Core work is dependency-driven; paint-only updates bypass layout.
2. Production consumes complete persistent projections without forced flat snapshots.
3. Geometry/history/confirmation reuse shared roots rather than copying the document.
4. CoreGraphics writes directly into reusable shared Metal buffers.
5. Unchanged tiles avoid rasterization, drawable acquisition, and image copies.
6. Every submitted fresh drawable is completely initialized for its tile.
7. GPU success plus exact required native tile evidence controls acknowledgment.
8. Changed/potentially mixed geometry is gated conservatively until confirmation.
9. Coalescing, nil acquisition, recovery, resize, close, and resource cleanup are bounded.
10. Existing public authoring/inspection remains usable; necessary additions are
    documented and exercised by consumers.
11. Pixel, operation-count, portable, sanitizer/distribution, and named native gates
    have recorded evidence; no required result is replaced by synthetic proof.
12. User-facing documentation describes both the benefit and the remaining copy/
    UI-thread/layer-memory constraints accurately.

## Reference contracts

These are adapter/API contracts, not portable-engine dependencies or a promise of
universal performance. Use the current SDK headers alongside the documentation
when implementing the chosen native path.

- [Framework backlog](../.tasks/00_omnibus.md): ownership, retirement-before-cleanup,
  publication, bridge lifetime, source organization, and verification conventions.
- [Repository architecture](../docs/architecture/README.md): inward dependencies and
  named-module/public versus private native boundaries.
- [Repository conventions](../docs/coding-conventions.md): names, Doxygen, ownership,
  executor, lifetime, and failure documentation beside important declarations.
- [Apple GPU storage modes](https://developer.apple.com/documentation/metal/choosing-a-resource-storage-mode-for-apple-gpus):
  shared physical memory versus GPU-private resource access.
- [Shared storage synchronization](https://developer.apple.com/documentation/metal/mtlstoragemode/shared):
  shared allocation does not remove the obligation to synchronize CPU/GPU access.
- [MTLBuffer contents](https://developer.apple.com/documentation/metal/mtlbuffer/contents%28%29):
  CPU-addressable resource storage for the bitmap context.
- [No-copy buffer allocation](https://developer.apple.com/documentation/metal/mtldevice/makebuffer%28bytesnocopy%3Alength%3Aoptions%3Adeallocator%3A%29):
  alignment/VM/lifetime requirements explaining why an ordinary vector is not the
  chosen backing allocation.
- [Metal blit encoder](https://developer.apple.com/documentation/metal/mtlblitcommandencoder):
  buffer-to-texture copies and source/destination resource constraints.
- [CAMetalLayer](https://developer.apple.com/documentation/quartzcore/cametallayer):
  drawable ownership, pool behavior, and display attachment.
- [Transaction-based Metal presentation](https://developer.apple.com/documentation/quartzcore/cametallayer/presentswithtransaction):
  use of the current transaction and the distinction from command-buffer present.
- [Command-buffer scheduled handlers](https://developer.apple.com/documentation/metal/mtlcommandbuffer):
  asynchronous scheduled/completed observations used by the presentation path.
- [Drawable presentation time](https://developer.apple.com/documentation/metal/mtldrawable/presentedtime):
  native display evidence, including unpresented/dropped zero timestamps.
- [Core Text line image bounds](https://developer.apple.com/documentation/coretext/ctlinegetimagebounds%28_%3A_%3A%29):
  context-dependent glyph image extent used alongside conservative fallback.
- [Layer geometry flipping](https://developer.apple.com/documentation/quartzcore/calayer/isgeometryflipped)
  and [contents scale](https://developer.apple.com/documentation/quartzcore/calayer/contentsscale):
  explicit distinction between sublayer geometry, content orientation, points,
  and physical pixels.
