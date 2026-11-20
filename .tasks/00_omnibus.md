---
status: todo
---

# Native application framework backlog

Reviewed against the repository on 2026-11-20. This remains a framework backlog,
not a specification for the later Todo application. `status: todo` means that the
remaining capabilities below are still open; it does not mean every foundation is
missing.

**Status convention:** **Implemented** means the source contains the capability
and referenced contracts cover it; it does not claim those tests were run during
this documentation review. **Partial** means a usable foundation exists but the
stated outcome is incomplete. **Open** means the proposed integration/API is absent.
All requirements under “remaining” describe future work, not current exports.

The previous version's declaration blocks are superseded by this review. They
used obsolete module names, namespaces, scalar geometry, event registration, and
shadow contracts. Existing source interfaces are authoritative for implemented
APIs; the behavioral requirements below guide additions to those interfaces.

- **P0 — Functional blocker:** normal mounted text entry and editing interactions.
- **P1 — Visual-fidelity gap:** faithful controls, typography, and layout.
- **P2 — Supporting completeness:** accessibility, platform integration, and verification.

## Current state at a glance

| Capability                                  | Status                 | Current boundary / remaining work                                                                                                                                         |
| ------------------------------------------- | ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Public module and subsystem organization    | Implemented            | One `import stana;`; functional partitions/namespaces, not separate public modules.                                                                                       |
| Separate build/verification/package steps   | Implemented            | `build` stages, `test` runs contracts, `analyze` runs audits/tidy, `verify` aggregates checks; `package` only stages/archives.                                            |
| Reactive retained UI and keyed composition  | Implemented            | Signals, bindings, transactions, scopes, `If`, `ForEach`, custom components, lifecycle.                                                                                   |
| Incremental layout/render publication       | Implemented foundation | Dirty domains, cached measurement/placement, persistent render/geometry trees, lazy diagnostic materialization. Responsive multi-document prepare/publish is still open.  |
| Callback-safe subtree retirement            | Implemented foundation | All removed identities leave lookup before child-first cleanup; removal blur/hover notifications remain open.                                                             |
| Unicode editing buffer                      | Partial                | `text::TextBuffer` has committed selection, simple marked text, grouped history, UTF conversion; no full stamped composition protocol or mounted editor.                  |
| Mounted text field and native IME           | Open                   | No `ui::TextField`, editor node, text-input target/session protocol, or `NSTextInputClient` integration.                                                                  |
| Routed pointer, key, focus, and blur events | Partial                | Typed `.events` registration, capture/bubble, default prevention; keyboard vocabulary is Tab/Space/Enter. Rich editing commands and focus coordination remain open.       |
| Hover/motion transport                      | Partial                | Native tracking and headless move/leave already exist; stationary-pointer reconciliation and published hover state remain open.                                           |
| Double-click and rich pointer metadata      | Open                   | Document pointer input is `Point`; no node-event button/modifier/click-count transport.                                                                                   |
| Toggle binding/activation                   | Partial                | Retained `checked` and common activation exist; no distinct checked indicator or checked snapshot.                                                                        |
| Wrapping and extended typography            | Open                   | System size/weight and foreground only; single-line measurement/drawing.                                                                                                  |
| Horizontal remaining-space allocation       | Open                   | Each fill child can receive the whole content width; no sibling-aware allocation.                                                                                         |
| Constraint-preserving arrangement           | Partial                | Measurement applies min/max; root/fill assignment can override those results.                                                                                             |
| Live viewport/container composition         | Open                   | `resize()` reflows; no `ComponentContext::viewportSize()` or `SizeReader`.                                                                                                |
| Drop/inset shadows and rich box contours    | Implemented            | `rendering::BoxShadow`, `.appearance.boxShadows`, elliptical radii/per-edge borders, retained/GPU/native coverage. Do not implement the old `Shadow`/`.shadows` proposal. |
| Configurable independent focus ring         | Open                   | Fixed accent focus stroke replaces the ordinary border.                                                                                                                   |
| Accessible labels                           | Partial                | Optional explicit label reaches `NodeSnapshot`; no semantic tree/native element bridge.                                                                                   |
| Editable/checkbox accessibility             | Open                   | No typed semantic actions, text range queries, or native checked/editable projection.                                                                                     |
| Link / external URL opening                 | Open                   | No `ui::Link` or public `application::openUrl()`.                                                                                                                         |
| Production-backed headless host             | Partial                | Mounting, events, time, snapshots, presentation evidence and rendering diagnostics exist; editor/services/semantic seams do not.                                          |

## Source map and architecture to preserve

### One public module, functional ownership

[`modules/stana.cppm`](../modules/stana.cppm) exports the single public module.
Names such as `stana:document` and `stana:rendering` are interface partitions;
consumers must not import the old `stana.core`, `stana.graphics`, `stana.text`, or
`stana.testing` module names. A namespace is not another module.

| Owner                     | Current sources                                                | Responsibility / direction for remaining work                                                                                                                   |
| ------------------------- | -------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Root primitives           | `modules/primitives/`                                          | Geometry, typed quantities, `Font`, environment, identities, portable `Error`.                                                                                  |
| `stana::text`             | `modules/text/text.cppm`, `src/text/text.cpp`                  | UTF validation/conversion, ICU graphemes, buffer/selection/composition/history. Add reusable text-layout values here without depending on UI/native frameworks. |
| `stana::reactive`         | `modules/reactive/`, `src/reactive/`                           | Signals, observations, bindings, scopes, executor ownership, settlement.                                                                                        |
| `stana::tasks`            | `modules/tasks/tasks.cppm`                                     | Scope-aware cancellable work and deterministic scheduler. Reuse for editor timers.                                                                              |
| `stana::layout`           | `modules/layout/layout.cppm`, `src/layout/layout.cpp`          | Layout properties/cache keys and sizing/placement helpers. Document member definitions remain in `stana::document::detail`.                                     |
| `stana::document`         | `modules/document/document.cppm`, `src/document/`              | Retained nodes, handles, events, eligibility/focus, document operations. Future editor/services/semantics belong here.                                          |
| `stana::rendering`        | `modules/rendering/`, `src/rendering/`, `src/rendering/scene/` | Public drawing/GPU APIs, shadows, presentation values; scene projection implementation also defines document members in their owning namespace.                 |
| `stana::ui`               | `modules/ui/ui.cppm`, `src/ui/mount.cpp`                       | Typed component descriptions, shared property groups, mounting and observation.                                                                                 |
| `stana::symbol`           | `modules/symbol/`, `src/symbol/`                               | Compiled assets, playback and scene integration. Preserve existing identity/generation checks.                                                                  |
| `stana::application`      | `modules/application/`, `src/application/`                     | Window sessions/configuration/requests/events, native execution and explicit transport conversion.                                                              |
| `stana::platform::detail` | `src/platform/native_contracts.hpp`, `src/platform/macos/`     | Private module-free transport and native host/rendering/facilities. Portable GPU internals also use `stana::gpu::detail` in `src/gpu/`.                         |
| `stana::testing`          | `modules/testing/testing.cppm`, `tests/`                       | Production document dispatch, explicit test configuration and owning inspection.                                                                                |

Use subsystem `detail` namespaces. Root `stana::detail` is restricted to the
existing shared primitive utilities (`nextIdentity`, `requireFinite`,
`requireNonnegative`), not a new home for document/editor/native services. Do not
create `src/core/` or `src/scene/` to match the old document. New editor and
accessibility implementations should extend `src/document/`; shaping stays in
`src/platform/macos/`. Register any additions in
[`build_support/sources.zig`](../build_support/sources.zig).

Current quantities are part of the contract: use `Points`, `FontWeight`, `Opacity`,
`ScaleFactor`, and the other appropriate wrappers, not raw doubles for exported
logical sizes/weights. `Font::size`, geometry, frame constraints, and padding use
`Points`; `Font::weight` is the existing native-scale `FontWeight` value type, not
the old proposed 100–900 enum. Adapter transport may extract `.value()` explicitly.

### Portable engine and current native host

The retained engine owns identity, state, layout, routing, selection/history and
future semantics. Native adapters translate platform services; no AppKit/Core Text
objects, native key codes, or UTF-16-only ranges enter portable authoring APIs.

macOS is the current native backend. Linux/Windows portable engine/headless support
is not a native window or native text implementation. Extend
`src/platform/unsupported.cpp` for newly introduced unavailable operations with
explicit failure; do not add placeholder backend trees or simulated native success.

Native support is broader than when the original backlog was written: typed window
configuration, capability/observation events, request streams, close handling,
window actions and facilities already have contracts. Reuse the existing session
and controller lifetime in `src/application/window_session.*` and configuration
code; do not introduce another window runtime to add editor services. Window drag/
drop and window interaction tokens do not provide mounted-editor clipboard,
selection capture, or node-event click-count semantics.

### Retained rendering and presentation

Current rendering uses an immutable `rendering::detail::RenderSnapshot`, persistent
`SceneBranch` projection and `GeometryRoot`. Flat `SceneSnapshot` paint and public
node lists materialize lazily for inspection. Native conversion consumes the
retained render tree through
[`src/application/scene_conversion.hpp`](../src/application/scene_conversion.hpp).
Do not replace that path with rebuilding/transporting flat node lists per frame.

Dirty domains distinguish measurement, arrangement, paint, snapshot and structure;
cache keys and subtree routes avoid unrelated work. Theme/focus/shadow changes
already have incremental-work regressions. New state must invalidate the affected
domains and preserve branch/resource sharing, not force full recomputation.

Presentation tracking already requires epoch-qualified attempts, GPU completion
and display evidence, including tile observations. Confirmed geometry, bounded
history, pinned in-flight roots, ambiguous-region gating and surface replacement
are implemented in `src/document/input.cpp`. Headless input uses committed geometry
unless `TestHost::enablePresentationTesting()` opts into these gates.

Extend geometry records/history with editor layout identity, store stamp, content
origin, scroll and effective clip. Do not replace the persistent index with the
old proposed flat rectangle maps. Text changes can invalidate interaction geometry
even when outer node bounds are unchanged. Never use GPU completion alone as proof
of visible caret/selection geometry, and never wait for a drawable to accept a
keyboard edit.

### Events, callbacks, and retirement

The current API is `.events = {on(Event::..., handler, Phase::...)}`. `Events` owns
typed registrations; `document::EventKind` and event payloads provide routes,
`target`, `current`, `phase` and `preventDefault()`. Extend that system for new
routed events. Do not add competing `InteractionProperties::onFocus`, `onBlur`,
`onPointerMove`, or `onDoubleClickCapture` callback channels. `InteractionProperties`
currently contains reactive `isEnabled` and nonreactive `bool inert`.

`Document::dispatch()` copies callables and routes, and resolves identities again
around application calls. Activation copies its default action and rechecks target
eligibility. Preserve these protections for editor callbacks, service calls and
new transition notifications. Never retain node/editor references or iterators
across reentrant application/native callbacks.

`Document::remove()` already detaches the subtree, extracts **all** records from
lookup into an owning batch, clears matching focus/press/hover state, and disposes
scopes child-first. Cleanup therefore sees stale handles and cannot remove those
records twice. Reuse this mechanism; the missing work is captured loss notifications
and editor/native-association cancellation, not basic safe retirement.

## Shared prerequisites still required

### Stamped text store and composition ownership — Partial

**Current:** [`modules/text/text.cppm`](../modules/text/text.cppm) exposes
`TextPosition`, `TextSelection`, UTF-8 validation, grapheme boundaries, UTF-8/UTF-16
conversion, and `TextBuffer`. The buffer has `text`, `revision`, `selection`,
`select`, `replaceSelection`, `setText`, simple `setMarkedText`/`markedText`,
`commitMarkedText`/`cancelMarkedText`, and undo/redo. Marked text is only an optional
string, not a replacement-range/provisional-selection snapshot. Validation rejects
CR/LF, not yet the full single-line break set below. There is no store stamp,
`StoreEdit` protocol, `snapshot()` or public history-availability query.

**Remaining contract:**

- Keep `TextBuffer` the sole owner of committed text/selection, composition and
  history. A future occurrence-owned editor record owns view/interaction state
  plus that buffer; retained node display text is only its committed mirror.
- Add owning snapshots and typed store operations for replace, update/commit/cancel
  composition and select. Keep committed/displayed coordinate spaces explicit.
  Content revision changes with committed content; a separate never-reused store
  revision also changes on observable selection/composition changes. Equal/no-op
  updates advance neither. Stale selection-only state must reject delayed edits.
- Composition owns committed replacement range, original selection, provisional
  UTF-8 text, relative provisional selection and its revision. Scalar-aligned native
  ranges must not weaken grapheme-safe committed user selection.
- Validate identity/stamps, ordering, bounds, overflow and UTF boundaries before
  mutation or history writes. Compute range lengths only after validating order.
- Omitted replacement uses current selection without composition, or the entire
  provisional region while composing. A displayed range intersecting preedit
  expands its committed extent and preserves untouched provisional prefix/suffix.
  A displayed range disjoint from preedit maps to committed coordinates and cancels
  the old preedit; an explicit committed range also cancels it. Never implicitly
  commit discarded provisional bytes.
- Commit resolves the replacement once and adds one undo entry. Empty provisional
  text is still active composition and can commit deletion. Commit without preedit
  is a no-op. Cancellation restores committed selection without a model write.
- Displayed selection wholly inside preedit updates provisional selection; wholly
  outside maps to committed selection and cancels after validation. Reject selection
  crossing a provisional boundary without mutation. Pointer/accessibility selection
  resolves composition before applying committed selection.
- Keep encoding conversion in adapters and coordinate mapping in text code, not
  duplicated in the macOS view or test host. Extend validation consistently for
  initial/model/insertion/preedit values. Only paste normalizes line breaks.

### One input/editing path — Open

Add document-owned input sequences and revisioned targets containing node identity,
focus-session identity and store stamp. One physical key/menu/test action owns one
sequence, reused by callbacks from native interpretation. Refresh its target stamp
before every operation; an activation-time stamp is not valid for future keys.

Use one document text-edit helper and one semantic editing-command dispatcher.
Adapters/tests/menus must not maintain another command switch or write bindings to
simulate user input. Accessibility can address an eligible editor directly through
the same helper without first stealing keyboard focus.

An operation validates document/target, eligibility, focus session where required,
then store stamp. Apply the buffer change, refresh committed mirror and geometry/
caret invalidation, capture the committed value, and write the binding only if
committed text changed. Reacquire identity after `Binding::set()`. Compare actual
buffer content for echoes; a blanket “writing” flag must not discard genuinely
different nested model writes.

Track composition entry/change/commit/cancel in a document input-sequence stack.
Submit/cancel/traversal from that same sequence is consumed without a second
application action. Nested events get independent entries; adapter RAII ends an
entry on exceptions/closure, and ending an already-retired sequence is harmless.

### Shared text layout and resource lifetime — Open

**Current:** `Document::setTextMeasure()` accepts a size-only callback. Without it,
headless measurement estimates byte-count width and one line height. Native
`measureText()` in `src/platform/macos/renderer.mm` builds a Core Text line and caps
its measured width; native paint prepares a separate line. There is no shared
line/run/caret/selection result and no portable text-layout provider.

**Remaining contract:**

- Introduce owning text-layout requests/results with UTF-8 source, fully resolved
  typography, direction, wrap, optional finite `Points` width and font-environment
  revision. Absence means unconstrained, never floating-point infinity.
- Return measured size, source ranges/baselines per line, visual runs, caret stops/
  affinity, selection fragments and an immutable paint-resource owner. Compare the
  full request as a cache key. A weak bounded cache (256 entries) may reuse layout
  identity across store revisions; presentation retains a separate current stamp.
- Scale typography once in document/layout code. Use matching native font resolution
  for measurement/painting. On macOS, shared immutable Core Text shaping resources
  must supply both geometry and drawing, rather than reconstructing lines twice.
- Carry layout/resource references through persistent paint segments and explicit
  native conversion. Preserve existing drawing APIs; legacy text commands missing
  layout can resolve once during preparation/conversion, not on each paint worker.
- Native resource wrappers are checked at conversion. Wrong provider/resource is
  `nativeFailure`, not an unchecked cast or silent reshape. Prepared/in-flight
  scenes own resources independently of document, buffer and provider cache lifetime.
  Resources may contain immutable Core Text objects, not UI-only AppKit state.
- No render worker calls back into document/provider state. Zero-size fonts/scales
  produce zero geometry and no native font resource/text paint command.
- Paint exact resolved source ranges/baselines. Selection foreground, strikethrough
  and composition decoration reuse that layout, clip and scroll transform; they
  must not shape again. Apply resolved foreground opacity once, including color fonts.
- Carets preserve bidi affinities and distinguish scalar query boundaries from
  grapheme user stops. Selection fragments follow visual runs, not a guessed x span.
  Pointer hits validate confirmed content/composition/layout state; reject stale hits.
  Candidate/accessibility queries report unavailable geometry until confirmation
  and invalidate native queries when confirmed geometry changes.

### Per-document host services and native bridge — Open

There is no current `HostServices`, `TestPlatform`, `WindowBridge`, or runtime URL
service bundle. `Application::prepare()` currently creates/mounts a document before
`runWindows()` installs native measurement. The new service installation must move
before mounting so initial layout/lifecycle use the intended provider.

- Install one owning service bundle per document: required text-layout provider,
  text-input activation/finish/discard/geometry invalidation, plain-text clipboard,
  interaction preferences, semantic publication and URL opening. Use typed requests,
  owning values and explicit failures. Missing optional operations return
  `unsupportedPlatform`, not `bad_function_call` or simulated success.
- Create per-window attachment state before the native view, integrated with the
  existing `application::detail::WindowSession`/controller. It may be captured by
  services but must not own the document; native callbacks use weak document owners.
  Text resources are separate immutable owners, not members of live views.
- Native documents start input-inactive until real attachment/activation; test hosts
  explicitly start active. Installing/importing text services alone must not create
  a native application. Keep provider identity fixed for a document lifetime.
- Resolve preferences initially and transactionally on environment/settings changes:
  caret blink, field/selection/focus/link/accent colors and disabled opacity. Use
  existing theme/color-token resolution. Default blink half-period is 500 ms;
  enabled blink requires a positive period and opacity must be in [0,1]. Failed
  resolution retains the prior environment/preferences. Font changes revise layout
  requests/caches without swapping the live provider.
- Keep native transport in `stana::platform::detail`, standard-library-only and
  module-free. Explicitly convert owning text/input/semantic/layout records and
  enums in the application bridge. No JSON, erased mutable payload, native object
  model or imports of public module types in the private transport header.
- Add failures/results where necessary to existing native callbacks/conversion;
  preserve resize, presentation, window-request and close behavior. Native failures
  are diagnosed without confirming a frame that was not painted/presented.
- Close deactivates bridge/input, retires targets and semantic elements, clears
  callbacks and disposes document state. Delayed work validates document/node/session/
  revision; presentation callbacks still require existing epoch-qualified evidence.
- Caret/autoscroll reuse `TaskScheduler::post()` with weak document, node identity
  and separate timer generations. Blur/removal/reset invalidate generations; do not
  use one latest-request scope slot for independent editor timers.

### Settlement and coherent publication — Partial

**Current:** `ReactiveRuntime::flush()` prepares observations, delivers structural
before property updates, calls registered document commit callbacks, then runs
queued effects. It repeats if provider work enqueues observations. Document commit
builds/promotes an immutable render draft, preserves the old publication on failure,
and blocks retry of the same failed mutation stamp. Reentrant measurement and
retirement already have regressions in `tests/rendering/incremental.cpp`.

**Remaining:** responsive size sources, queued focus/hover transitions, semantics
and cross-document prepare/publish need explicit coordination. Current commits are
single callbacks; there is no paired preparation/publication registry, and effects
are currently drained as a batch rather than one effect followed by settlement.

Extend the existing runtime, not a parallel runtime, with this ordering:

1. Prepare/deliver pending observations, structural before property work.
2. Reconcile eligibility and drain copied focus/hover transition notifications.
3. Prepare unpublished layout/render/semantic/interaction-geometry drafts and size offers.
4. Apply changed size sources outside recursive measure/arrange.
5. Repeat while observation/transition/size work remains; publish no intermediate draft.
6. Publish ready document projections together once the reactive turn is stable.
7. Run one lifecycle effect; settle/publish any resulting work before the next effect.

Keep runtime coordination records in `reactive::detail` so reactive code does not
import document. Draft ownership stays in document/rendering. Share a bounded
10,000-step nonsettlement budget across retries/transitions/observers. Failed
preparation retains the last complete publication and waits for a changed input/
service revision; other documents may still publish. A semantic sink failure reports
an error but does not roll back a committed portable snapshot. Host publication
must enqueue resulting application actions instead of invoking them inline.

Retain portable error meanings: `wrongDocument`, `staleHandle`, `notFocusable`,
`invalidTextRange`, `invalidValue`, `unsupportedPlatform`, `nativeFailure`, and
`cancelled`. Invalid initial aggregate configuration is a programmer-contract
exception; nonsettling reentrancy is `logic_error`. Returned recoverable failures
must not also be logged at every lower layer. Void native/test wrappers record
otherwise unreturned failures once.

## P0 — Core interaction features

### Single-line text-field component — Open

**Current:** `ui::Text` is display-only. `document::detail::NodeKind` has no editor
kind, document does not import the text partition, and UI does not re-export it.

**Remaining outcome:**

- Add `ui::TextField` with required `reactive::Binding<std::string> value`, reactive
  placeholder, inherited placeholder text overrides, shared layout/text/appearance/
  interaction/accessibility/lifecycle groups, `.events`, and owning submit/cancel
  callbacks. Submission values contain target identity and captured committed text.
- Add a distinct `textField` kind and occurrence-owned editor record. Its buffer
  owns text/history/composition; view state owns horizontal scroll, caret affinity,
  drag anchor, undo grouping and independent timer generations. The node scope
  owns observations; do not create a second scope/editor runtime/native text store.
- Mount once and mutate existing state on property changes. Committed user edits
  write the binding once per transaction. Equal model echoes preserve composition,
  selection, revisions and history. Different valid model values cancel preedit,
  clear history, reset selection/scroll and notify the native client appropriately.
- Invalid UTF-8 or multiline initial values fail mounting. Later invalid values
  report `invalidValue` and preserve valid editor state without rewriting the model.
  Reject CR, LF, NEL, U+2028 and U+2029 in direct/model/preedit values; paste has its
  own normalization policy. Enter is a command, never direct newline insertion.
- Default to one unwrapped line, preferred content width 160 points plus padding,
  one resolved line high, 6-point vertical/8-point logical horizontal padding,
  host field colors, 1-point border and 4-point radius. Explicit frame sizing wins.
  Placeholder appears only for empty content without visible composition; it is
  neither value nor automatic accessible name. Omitted typography inherits editor style.

**Verification:** typing, invalid values, external replacement, equal echoes during
preedit, unrelated updates, binding writes and keyed moves. Assert identity,
selection/history/composition and mount counts, not just display text.

### Native text input and IME — Open

Implement portable insertion, preedit update/commit/cancel, surrounding-text,
selection and range/geometry queries through the stamped input path. Native
interpretation runs before unconsumed semantic commands.

On macOS implement `NSTextInputClient` on the host view: insert/marked/unmark,
selected/marked ranges, attributed substring, index-at-point and first-rectangle
queries. Convert native UTF-16 ranges with checked arithmetic/scalar boundaries,
map through the captured displayed store, and convert document points to screen
coordinates only in the adapter. No `NSTextField` or hidden competing text store.

Provisional updates change neither binding nor undo history. A commit replaces
the saved range once; cancel restores selection. External model replacement also
invalidates native preedit. Candidate geometry follows confirmed presentation after
focus, scrolling, layout and backing-scale changes. A composition-consumed action
cannot also submit, cancel the application edit, or traverse focus.

**Verification:** CJK composition over selections, explicit marked subrange
replacement, empty preedit deletion, cancel, dead keys, combining marks, emoji,
supplementary scalars, stale/split/overflowing ranges, model updates and candidate
movement. Deterministic injection does not substitute for a real IME native gate.

### Caret, selection, and editing commands — Open

Add portable semantic commands for visual left/right, word backward/forward, line
start/end, selection-extending variants, character/word deletion, select-all,
cut/copy/paste, undo/redo, submit/cancel. Preserve existing routed key events/default
prevention; platform shortcut/selector mapping is adapter work, not a universal
Command/Ctrl convention or a replacement raw-key runtime.

- Use shaped visual caret order/affinity for bidi navigation, ICU graphemes for
  character deletion and ICU word segmentation for word commands. Without extension,
  movement collapses selection toward the requested visual edge; extension preserves
  anchor. Treat whitespace/punctuation runs coherently; line moves use visual edges.
- Primary press places the caret from confirmed geometry; Shift-click extends it.
  Qualified double-click selects a word or punctuation/whitespace run. Capture drag
  selection until release/cancel, including outside the control.
- If a press arrives during composition, capture its confirmed displayed hit before
  finishing preedit. Reuse only when resolution leaves the complete displayed text/
  layout request unchanged in the same live node/session. Otherwise cancel the
  pending default instead of applying arbitrary stale bytes.
- Clip to padded content; paint selection fragments, glyphs/composition decoration,
  then caret. Maintain/clamp horizontal scroll after text, selection, size or style
  changes. Do not split oversized graphemes to fit. Shared layout supplies strike
  metrics; composition underline covers the full provisional range, not just its
  selected subrange (1 point at baseline + 2 points).
- Blink defaults to 500 ms half-period, solid during input/dragging and suspended
  while inactive. Autoscroll ticks at 16 ms with speed
  `min(1200, 12 * outsideDistance)` points/second, clamps scroll and extends from
  the saved anchor via confirmed geometry. Stop on release/cancel, ineligibility,
  stale content, inactivity or no possible selection change. Test time drives both.

**Verification:** mixed-direction/reversed selections, emoji/combining deletion,
word/line movement, Shift-click/drag, capture cancellation, empty/long text, shrink,
scroll clamping, stale layout rejection and deterministic timer generations.

### Clipboard and undo/redo integration — Partial buffer, open integration

The buffer already has grouped undo/redo and selection restoration. There is no
mounted command path, clipboard service or native editor menu validation.

- Availability is a pure eligible-editor query: copy/cut need nonempty committed
  selection; paste needs a read-text service; undo/redo need corresponding history;
  select-all needs content. Navigation/deletion boundary no-ops and submit/cancel
  without callbacks remain handled. Never inspect OS clipboard contents to validate
  a menu item.
- Cancel pending preedit before an available explicit clipboard/history command.
  Capture resulting selection/store/session, invoke the service, then revalidate.
  Never cut a new selection after a reentrant clipboard write changed the target.
- Copy exact committed selection. Cut deletes only after successful write. Paste
  converts each CRLF, CR, LF, NEL, U+2028 or U+2029 break to one space, preserving
  other text including tabs. Empty/absent clipboard text is a no-op; invalid UTF-8
  or failed service leaves text/history unchanged.
- Group contiguous plain typing and repeated same-direction character deletion.
  Navigation, selection, word delete, focus, clipboard, submit/cancel, composition
  and external replacement close a group. Cut, paste and IME commit each form one
  group. New edits clear redo; undo/redo restore selection and write the binding
  once. No-op edits create no history/model write.

**Verification:** native shortcuts/menus, Unicode round-trips, break normalization,
failed/reentrant cut/paste, grouping, redo invalidation, selection restoration,
model echoes and service availability/failure. Tests use explicit memory services.

### Submit and cancel callbacks — Open

Unconsumed Enter submits once; unconsumed Escape cancels once. Capture owning
`{target, committed value}` and copy the callback before invoking application code
inside the input transaction. Reacquire identity afterward. Neither command inserts
text, activates an ancestor, changes focus, disposes the field or rolls back the
model. Draft/rollback policy remains application-owned.

Composition confirmation/cancellation consumes that input sequence without invoking
the application callback; a later independent action may invoke it. Suppress repeat
submit/cancel until release, without suppressing normal typing/navigation repeat.
Blur never implies submit. Callback-driven replacement/removal/focus/closure must
not manufacture another callback or stale-storage access.

### Editor focus and focus-change events — Partial

**Current:** `NodeHandle::requestFocus()`, wrapped Tab/Shift-Tab traversal, pointer
focus and routed `Event::focus`/`Event::blur` exist for buttons/toggles. `FocusEvent`
has route/default-prevention fields, but no reason/related target/editor value.
Removal clears focus without blur by the current documented contract. Window-level
focus events are not a document text-input session coordinator.

**Remaining contract:**

- Extend existing routed focus payloads with owning reason/related-target/editor
  blur-value data; retain `.events`, not the old separate interaction callbacks.
  Changing removal notification semantics must update existing API docs/contracts.
- Document owns logical target, active session, input modality and a pending focus
  request distinct from “no request.” Clear old logical focus before blur; revalidate
  destination afterward. Reentrant requests replace the pending destination and
  drain after the callback, not recursively. Same-target focus is a no-op.
- Editors/links join buttons/toggles in published tree-order traversal. Use normal
  post-publication `onMount` plus handle focus for initial focus. Primary press
  focuses the nearest focusable ancestor; nonfocusable/background press clears.
  Prevented defaults do neither; gated/uncertain input is not an outside click.
- Before ordinary transfer/deactivation, finish native composition and commit any
  remaining valid preedit once. Revalidate after synchronous native callbacks.
  Host failure is reported once, not grounds to abandon real window deactivation.
  Removal/different external model values cancel preedit instead.
- Deactivation retains logical focus but suspends active client/caret/ring; activation
  restores only an eligible target with a fresh session. Do not synthesize logical
  blur/focus pairs solely for native activation changes.

**Verification:** initial/programmatic/pointer/traversal focus, prevented/outside
clicks, related target/reason/value, reentrant redirects/removal, native finish
failure, session-stale callbacks, inactivity and closure.

### Focus validity during tree changes — Partial

`isEligible()` already checks scope, visibility, enabled and inert ancestry for
input; it does not provide the planned pre-publication focus reconciliation/loss
notification/bookmark behavior. `inert` is still nonreactive authoring state.

- Make inert observation reactive consistently with enabled state. Before projection,
  clear focus/capture/press that became ineligible through local or ancestor changes.
  Published focus must never identify hidden/collapsed/disabled/inert/disposed content.
  Do not automatically focus an unrelated node.
- Retain ordered traversal position on loss; next Tab chooses the surviving successor/
  predecessor, wrapping and skipping ineligible IDs. Explicit acquisition clears
  the bookmark; if no remembered candidate survives, use current first/last.
- Extend the existing owning removal batch to capture one blur/hover loss value
  before retirement, cancel editor work/native association, then deliver copied
  notifications after coherent structural change. Closure drains losses once even
  though there is no subsequent document commit. Bound reentrant transitions.

**Verification:** local/ancestor policy changes, removed old/new targets, blur-driven
removal/closure, stale handles during cleanup, repeated removal and no duplicate
submit/blur. Retain existing reentrant-retirement regressions.

### Double-click dispatch — Open

Current press/release callbacks already exist; the missing pieces are metadata,
double-click semantics and corresponding native/headless transport.

- Extend pointer input/events with button, modifier values and positive click count,
  retaining logical `Point`, route and prevention fields. Reject nonfinite positions
  and zero counts before dispatch; no hit is a successful no-op. Native adapters
  preserve host counts rather than inventing a timing recognizer.
- Add a typed double-click event to `EventKind`/`ui::Event` and existing registration
  machinery. Handler-only labels need not become activatable/focusable controls.
- Track press-route identity separately from activatable ancestor and the first
  completed primary release. On a qualified second primary release, capture then
  bubble double-click before that release's default activation. Replacement identity,
  cancelled drag or release outside disqualifies it. Counts above two do not trigger
  another double-click. Preserve ordinary clicks; do not delay/undo the first action.
- A prevented double-click suppresses the associated default, not already delivered
  callbacks. Editor word selection occurs on the qualified second press and is not
  retroactively undone by release prevention. Recheck route eligibility after callbacks.

**Verification:** label handlers, native count transport, capture/bubble/prevention,
first/second ordinary actions, triple click, editor word selection, disabled ancestors,
mid-dispatch replacement and presentation gating.

### Hover and pointer tracking — Partial

**Current:** native `NSTrackingArea` plus moved/dragged/entered/exited handlers
forward movement/leave. Document and `TestHost` expose `pointerMove(Point)` and
`pointerLeave()`. Hover path differences already deliver direct leaves deepest-first
and enters ancestor-first; move routes capture/bubble. Parent hover survives motion
between descendants. The old claim that hosts drop all motion/exit is obsolete.

**Remaining:**

- Retain pointer presence/position and derived hovered ancestry for publication.
  Add read-only `isHovered` inspection; use existing enter/leave events for callbacks.
  Do not add a parallel global hover signal or native-view-owned node hover state.
- Recompute with a stationary pointer after scroll, geometry confirmation, eligibility,
  removal and callback-driven layout changes. Clear on exit/deactivation/presentation
  uncertainty and recompute after confirmation. Copy removal notifications before
  retirement and settle reentrant transitions with a bound.
- Editor capture routes drag updates outside the node without redefining ordinary
  actual-hit hover. Rich metadata travels through the same pointer transport.
- Reserve row action space with `Visibility::hidden` when revealing on hover should
  not change geometry; collapsed/conditional removal has different semantics.

**Verification:** existing route/motion tests plus stationary scroll/resize/acknowledgement,
disable/removal, exit while captured, inactive window, no duplicate notifications
and stable bounds for hover-revealed actions.

## P1 — Layout and rendering features

### Visible toggle state — Partial

`ui::Toggle` already observes `Binding<bool>`, stores `checked` and flips through
the common activation/default-prevention path. Scene projection paints its text and
button-like surface, not a dedicated checked indicator. Snapshots omit checked state.

Add a reactive indicator style with checkbox, circular-checkbox and chevron forms,
18-point default size, 8-point logical label gap and optional host-resolved state/
mark colors. Preserve bool binding; no tri-state requirement. Reserve indicator
width independently, omit gap for empty labels, wrap only the label and align at
the first line's logical leading edge in LTR/RTL. Use geometric strokes, not font
glyphs. Checked changes paint, not geometry, focus or semantic role. Shared outer
appearance still applies; disabled opacity applies once to the whole control.

**Verification:** all forms/states, external bool updates, empty/wrapped labels,
RTL, prevented/disabled activation, checked snapshots and stable identity/size.

### Multiline text layout — Open

Add reactive wrap policy (`none`, `word`, `grapheme`) to shared text properties and
layout requests. Labels default to word wrap; editors force none. Resolve effective
width after constraints, padding and sibling allocation before choosing lines.

Word mode uses Unicode line-break opportunities with grapheme-safe fallback for
oversized tokens; grapheme mode fits whole graphemes. Preserve source-byte offsets,
CRLF as one hard break, trailing empty line and an empty-text line. Hard-break bytes
belong to line source ranges but have no glyph ink. Zero width returns one-line
height without a repeated-break loop; oversized indivisible graphemes clip on one
line. Unbounded width has no soft wrap. Height limits clip, not implicit ellipsis.

On macOS use Core Text line construction from the shared layout result; a capped
single-line measurement is not wrapping. Cache full request and ensure forward
progress. Painting, pointer/IME/semantic geometry must agree on exact line ranges.

**Verification:** long URLs/tokens, CJK, emoji, mixed direction, spaces/hard/trailing
breaks, zero/tiny/unbounded widths, resize reflow, adjacent control placement and
native pixels rather than scene-command counts alone.

### Text decoration and typography — Open extensions

Current `Font` supports typed system size/weight; `TextProperties` exposes only
optional reactive font/foreground. Add owned family selection, normal/italic style,
reactive strike and optional explicit `Points` line height without replacing the
existing quantity types or introducing the obsolete enum-weight API.

Resolve component defaults/inherited placeholder base, optional whole font,
individual style overrides, other properties, then one environment scaling pass.
Omitted placeholder fields inherit the editor; secondary-text foreground is its
default. Native font resolution handles family/weight/italic and fallback together,
not application-specific font-name cases.

Validate UTF-8 family strings, finite nonnegative sizes and positive explicit line
height before assignment. Zero size/scale creates no native font resource. Center
natural ascent/descent in an explicit line box; deliberately small height can overlap
ink and is not silently enlarged. Strike uses resolved run baseline/x-height and
thickness, not assumed native attributes; color/decoration do not alter breaks.

**Verification:** italic placeholders, mixed-script fallback, family/weight/style
updates, line heights, strike metrics, invalid values, zero scale, one scaling pass
and native paint without remounts.

### Remaining-space allocation in horizontal stacks — Open

`src/layout/layout.cpp` currently measures siblings against full content space and
arranges each fill width as that full width. Incremental caching does not solve
allocation. Add one shared constrained allocation used by measure and arrange:

1. Remove padding once; exclude collapsed children/gaps, retain hidden participation.
2. Reserve constrained fixed/percent/automatic siblings. Percentages resolve against
   full padded content width, not only the remainder.
3. Reserve fill minima, divide remaining space equally, freeze max-constrained fills
   and redistribute surplus. Keep minima/overflow when space is insufficient; never
   create negative widths or silently shrink fixed siblings.
4. Remeasure fill content at allocated widths for wrapping/row height and retain
   that allocation for arrangement. Never expand each fill again afterward.
5. Unconstrained rows use intrinsic constrained sizing, not infinity division. Leave
   surplus at logical trailing after maxima; RTL changes placement, not identity.

Use explicit optional-axis proposals with typed sizes, not the old `double` records.
**Verification:** fixed indicator/fill label/action, multiple fills, min/max freezes,
percent-plus-fill, overflow, padding, hidden/collapsed, zero/unbounded width, nested
wrapping and RTL, including incremental/full-recompute equivalence.

### Constraint preservation during arrangement — Partial

Measurement already resolves point/fill/percent/automatic lengths and min/max.
Arrangement currently replaces root-child/fill dimensions with available content,
which can bypass the measured cap. Share resolution so final assigned dimensions
honor constraints and explicit policy, including centered capped panels.

Validate nonfinite/negative values and contradictory min/max inputs consistently;
no negative content extent after padding. Allocation, remeasurement and arrangement
must use the same resolved widths. Cover both axes, nested containers, percentages,
scrolling, RTL and root sizing. Do not turn this increment into a new flexbox engine.

For a bounded offer, automatic uses measured intrinsic extent, points uses the
request, percent uses the offer, and fill uses the offer or stored horizontal
allocation; apply min/max afterward. With an unconstrained offer, percent/fill use
intrinsic sizing. The internal root fills the viewport; user children honor their
own frames instead of implicit root-child stretching. A minimum beyond parent
space may overflow subject to clipping. If final width changes a text proposal,
remeasure before publication rather than painting stale line breaks. Existing
center-aligned `Box` composition should center capped content without a new primitive.

### Responsive composition — Open

`Document::resize()` and `TestHost::resize()` reflow already. A mount-time
`ComponentContext::environment()` snapshot is not live viewport/container size.

Add read-only `ComponentContext::viewportSize() -> reactive::Computed<Size>` and a
`ui::SizeReader` whose content factory receives a read-only local size and runs once
per occurrence. Document/reader own writable sources; applications never receive them.

Local size is finite parent-offered space after frame/padding, not descendant
intrinsic output. Preserve finite offers larger than viewport; substitute viewport
only for unconstrained axes. Seed before first layout, correct before lifecycle,
and default reader to fill width/automatic content height. Reader content may grow
height but must not feed back its own offered-height source.

Recursive layout only collects offers. Apply changed sources between preparation
passes and settle observations/branches before publication/effects. Equality suppresses
updates; resize does not rerun factories. Ordinary `If` changes still dispose branches;
durable editor state belongs outside replacements. No new wrapping-stack primitive
or public writable size signal is required.

**Verification:** independent viewport/capped nested sizes, exact breakpoints,
padding, one/two-line footer transitions, factory count, unchanged-branch editor
identity, disposal, bounded feedback and no intermediate scene/effect publication.

### Drop and inset shadows — Implemented; preserve current API

The old proposal is completed under a different, richer contract:

- [`rendering::BoxShadow`](../modules/rendering/shadow.cppm) has typed offset,
  `Points blurRadius`, signed `spreadRadius`, optional color and `isInset`.
- `ui::AppearanceProperties::boxShadows` is a reactive ordered vector property.
  Missing color follows effective foreground; first list entry is frontmost.
  Do not substitute the previous “later entry on top” ordering.
- `CornerRadii` and `BorderWidths` support elliptical logical corners/per-edge inward
  borders, with explicit outer/padding contours. `PaintKind::boxShadow` and owning
  `ShadowPaint` transport are implemented. Own clipping does not erase outset ink;
  ancestor clipping still applies. Shadows never expand layout or hit areas.
- Native GPU masks/separable Gaussian blur, caching/resource accounting and surface
  epoch retirement exist in `src/platform/macos/shadows.*` and compositor integration.
  Explicit GPU drawing and shadow interpolation also exist. Do not replace them
  with the original standalone CPU-bitmap shadow design.
- Portable value/geometry/interpolation contracts, retained UI incremental contracts
  and native pixel/cache tests exist in `tests/rendering/shadows.cpp`,
  `tests/ui/shadows.cpp`, `tests/platform/macos/shadows.mm` and fixtures. Native tests
  include 1x, fractional and 2x rendering and fresh/incremental comparisons.

No duplicate shadow implementation belongs in delivery order. Preserve existing
paint order, typed values, contour behavior, resource limits and coverage when
adding toggle/text/focus paint. Test-source presence is not a claim that the native
suite ran during this review.

### Configurable focus appearance — Open

Current scene projection uses a fixed 2-point accent stroke in place of the normal
border when logically focused. Rich box contours exist but independent ring style,
modality and active-window suppression do not.

Add reactive focus-ring style using typed width/offset/optional radius and optional
host-resolved color. Default 2-point width/2-point gap follows the surface contour.
Paint a separate overlay preserving normal border, without layout/hit expansion.
Offset is edge-to-inner-ring gap; centerline expands by offset plus half width.
Honor rich contours rather than reducing existing elliptical corners to a scalar.

Keyboard/programmatic focus shows the ring; pointer acquisition suppresses it.
Keyboard activity can reveal it without reacquisition. Inactive windows suspend
active paint, not logical target. Omitted style retains visible keyboard focus;
reject zero width/transparent custom rings, honor high contrast, and keep own
content clipping from erasing the overlay while respecting ancestor clips.

**Verification:** borders preserved, custom/rich rounding, modality changes,
inactivity, high contrast, clipping, invalid style and unchanged geometry/work domains.

## P2 — Accessibility and platform integration

### Native accessibility tree — Partial labels, open projection/bridge

`AccessibilityProperties::label` is currently `optional<string>` and reaches
`NodeSnapshot::accessibilityLabel`. Omitted and explicitly empty are distinct.
There is no current portable role/semantic snapshot/action model or macOS native
node-element bridge. Window facilities do not complete this requirement.

- Extend properties with portable role override and reactive label/description.
  Publish owning semantic identity/hierarchy/role/name/value/bounds/eligibility/
  logical-active-focus/actions at the same scene revision. Native types stay out.
- Explicit label, including empty, wins over visible label text. Placeholder is a
  hint, not value/name; diagnostic lifecycle names never become accessible names.
  Flatten unnamed structural nodes, preserve labelled groups/order and one root.
  Omit hidden/collapsed/disposed subtrees. Disabled/inert nodes remain inspectable
  but do not advertise unsupported actions. A role override does not create behavior.
- Cache native elements by full document/node identity, not index/label. Diff complete
  publications; notify structure, name/description, value/checked, selection, then
  focus with coalescing. Unchanged publications do not create notifications.
- Actions enter ordinary document dispatch after current eligibility validation.
  Query confirmed bounds and convert to screen coordinates at query time; unavailable
  geometry is not pending layout. Retirement releases native associations without
  retaining mutable document nodes.

**Verification:** labels/hierarchy/default roles, reactive updates, two-window
identity, stale actions, disabled/inert inspection, retirement, confirmed geometry
and real native discovery/traversal/activation with VoiceOver/accessibility inspector.

### Editable-text and checkbox semantics — Open

Add typed activate/focus/set-value/replace-text/set-selection actions. Text actions
carry expected store stamp and committed-coordinate/grapheme-safe ranges; validate
before cancelling preedit/mutating, then use the shared editor helper. Full set-value
is one undoable edit, not external `setText()` history reset. Selection-only actions
add no history; checkbox activation shares the normal bool binding path.

Expose committed value/selection, displayed/marked query data and range/index geometry
from matching confirmed layout/store/clips. Unavailable geometry returns `cancelled`;
stale ranges return `invalidTextRange`, not geometry for different text. Composition
updates must not announce committed-value changes. Publish checked independently of
visual indicator. Disabled/inert actions fail without partial changes.

**Verification:** emoji/preedit edits, selection, undo, echoes, stale range/session,
disposal, unavailable actions, checkbox values and actual native text/selection/
checked notifications and screen-reader actions.

### Links and external URL opening — Open

Add `ui::Link` with reactive text/destination, shared groups/`.events`, and copied
open-error callback. Add `application::openUrl()` returning `expected<void, Error>`.
Validation/service dispatch belongs in document/shared private code so UI does not
import application. Pointer/key/handle/accessibility use one activation capturing
current destination once, honoring eligibility/default prevention.

Require valid UTF-8 absolute URI with syntactic scheme; reject NUL and unescaped
ASCII whitespace/control. Do not restrict schemes to HTTP(S). Link uses its owning
document service; free application opening uses executor-local runtime registration
for `Application::run`, independent of current focus, removed on every exit. A test
host must not overwrite another host or process runtime service. macOS delegates
to system workspace handling. Acceptance is not a claim of remote load success.

Default link appearance uses underline/host link color, focus eligibility and
semantic destination. Invalid/missing/failed services return explicit errors;
callback disposal must be safe. Tests record requests without launching a browser;
real external opening belongs only in explicit opt-in verification.

## P2 — Verification infrastructure

### Production-path input injection — Partial

[`testing::TestHost`](../modules/testing/testing.cppm) already provides normal
mount/close, flush, nodes/scene/metrics/errors, handles/activation, press/release/
click/cancel, Tab/key activation, key down/up, scroll, pointer move/leave, resize,
environment changes and deterministic `advanceTime()`. It also exposes frame/
symbol/GPU diagnostics and explicit presentation simulation including surface
replacement, GPU completion and display evidence. Preserve those seams.

Add editor integration alongside each feature, not after production work:

- Explicit begin/end input sequence, current text-input state, stamped text/command
  dispatch and convenience insertion/preedit/commit/cancel commands.
- Rich pointer metadata/click count and active-window control through the same
  document operations, keeping `Point` convenience overloads as adapters.
- Test-only platform configuration: deterministic layout provider, interaction
  preferences, memory clipboard, recording URL service and semantic publications,
  one-shot failure injection. Fresh state per host unless sharing is explicit.
- Default text provider uses explicit deterministic metrics (8-point scalar advance,
  20-point line height plus Unicode boundaries); recorded provider geometry covers
  bidi-specific cases. Neither provider claims production font fidelity.
- Owning editor/checked/hover/active-focus inspection and semantic action/query seams.
  Tests may configure services, never mutate retained nodes or bindings to impersonate
  input. Convenience edits acquire fresh targets and one sequence; explicit dispatch
  permits multiple native-style callbacks in that same sequence.

### Editor and control state inspection — Partial

Current `NodeSnapshot` has identity, kind, text, bounds, effective enabled, logical
focus and optional accessible label. It lacks checked, hover, active focus and editor
state. `TestHost::nodes()` reads the last complete publication, including inside
transactions; retain that contract and lazy materialization.

Add optional checked only for toggles, derived hover, logical-vs-active focus, and
an optional owning editor snapshot with stamped committed/composition/selection
state, horizontal scroll and undo/redo availability. Editor snapshot text is
committed; displayed preedit text remains separate. Use `NodeId` for occurrence
lifetime, not another mutable editor handle. Old snapshots own values without
keeping scopes/native views alive. Semantic/text-layout inspection must likewise
use immutable values/resources rather than retained record pointers.

Extend the existing persistent presentation history with editor geometry/resource
ownership; eviction cannot invalidate in-flight scene owners. Include old/new
content clips when text interaction geometry becomes uncertain at equal outer bounds.

### Regression and native verification coverage — Partial

Existing suites cover primitives, reactivity/thread-safe writes, tasks, mounted UI,
routed events, text buffer, application/window configuration/requests, incremental
rendering, presentation evidence, symbols, CPU/GPU drawing, shaders, shadows and
tiles. Register additions in
[`build_support/project.zig`](../build_support/project.zig), preserving current suites
and `stana.<feature>` contract naming. Do not describe all input/layout/native
coverage as absent merely because editor integration is still missing.

Minimum new mounted-editor regression: start with an empty focused bound field;
update CJK preedit and assert model still empty; commit through one explicit input
sequence; dispatch submit in that same sequence with a freshly acquired target and
assert no application submission; end sequence; issue an independent submit and
assert exactly one callback with committed value. Pair with cancellation, stale
store/session, callback removal/closure and service failures.

Portable contracts must also cover identity across keyed moves/unrelated updates,
selection/history/echoes, failure/reentrancy, timer cancellation, eligibility,
allocation/wrapping/responsive publication, semantics and incremental/full recompute
equivalence. Keep consumer smoke cases for new aggregates/property groups.

Adapter tests separately cover UTF conversions, native text-client selectors/
queries, shortcut/menu mapping, clipboard, screen conversion, semantic notifications
and service results. Fake external state where useful; synthetic dispatch does not
prove actual IME, window-server or screen-reader integration.

Native/visual gates remain explicit: real IME/dead keys/candidate placement,
clipboard/commands, drag/autoscroll, accessible discovery/actions, resizing and
backing-scale changes; reproducible 1x/2x typography/indicator/focus output. Reuse
existing fixtures rather than add application examples. Existing shadow native
coverage is a regression to retain, not a feature still awaiting its first fixture.
Record manual evidence where reliable automation is unavailable; missing graphical
access is an outstanding gate, not passing evidence.

### Verification commands and scope

Use Zig 0.17.1, matching LLVM 22.1.8 tools, ICU4C 78.3 and Python 3.11+.
The current `justfile` and build graph separate staging, testing, analysis and
packaging; do not infer verification from a successful ordinary build or archive.

| Gate                            | Command / requirement                                                                                                                         |
| ------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| Debug build/staging only        | `just build` or `zig build -j3`; does not run analysis/tests                                                                                  |
| Debug contracts                 | `just test` or `zig build test -j3`                                                                                                           |
| Release contracts               | `just test release` or `zig build test --release=fast -j3`                                                                                    |
| Standalone source audits        | `just audit` or `zig build audit -j3`                                                                                                         |
| Source audits and clang-tidy    | `just analyze` or `zig build analyze -j3`                                                                                                     |
| First-party C++/Zig formatting  | `just format-check`                                                                                                                           |
| Complete aggregate verification | `just verify` / `just verify release`, or `zig build verify -j3` / `zig build verify --release=fast -j3`                                      |
| Sanitizers                      | `just test asan` / `just test asan-ubsan`; matching provisioned Tint build required                                                           |
| Graphical macOS contracts       | `zig build test-native -Dnative-tests=true -j3`                                                                                               |
| Offscreen Metal contracts       | `zig build test-native -Dnative-offscreen-tests=true -j3`; Metal device required                                                              |
| SDK staging/archive only        | `just package` defaults to Release; `zig build package --release=fast -j3` does not run verification. Run `verify` separately before release. |

These are implementation verification gates, not commands claimed to have run for
this backlog-only update. Completion of a feature requires its portable contract,
current adapter, deterministic tests and named native evidence where applicable.
Preserve consumer/install/relocation checks when extending SDK interfaces; do not
claim the archive-only target proves consumer compatibility.
No new examples are authorized by this backlog. Do not edit other Markdown files
as part of this review.

## Existing foundations to reuse

- Scope-owned observations, cancellable tasks, deterministic clock and executor
  confinement; no second timer, service/runtime or application-owned focus engine.
- Mount-once typed authoring, properties, routed `.events`, handles and published
  snapshots. `ForEach` preserves unchanged keyed rows/shared reference identities;
  changed value items replace descendants. Use stable row objects/signals when
  editor state must survive unrelated row changes; no list-engine rewrite is needed.
- Incremental layout/projection, immutable shared branches/resources, persistent
  geometry and explicit presentation evidence. Preserve work-domain regressions.
- Callback-copy/revalidation and complete lookup retirement before cleanup.
- Unicode buffer/UTF conversion/ICU boundaries; extend rather than compete with it.
- Existing rich contours, outer/inset shadows, CPU/GPU scenes, symbol resources and
  native window session/configuration infrastructure.

## Boundaries and implementation order

Keep testing/inspection seams in each increment. Only unfinished work is scheduled:

1. Extend text store/composition protocol and shared layout/resources; add per-document
   native/test services before mounting. Preserve current retirement and render tree.
2. Coordinate preparation/transition/publication where new state requires it; add
   immutable editor/control inspection without replacing existing lazy projections.
3. Implement mounted field, stamped dispatch, native text client, selection/caret,
   clipboard/history and composition-aware submit/cancel, with focus/session validity.
4. Extend existing pointer transport for click counts/capture and close hover/focus
   reconciliation gaps, retaining current routed events and presentation gates.
5. Fix shared constraints/sibling allocation, then wrapping/typography and responsive
   inputs; add checked indicators and independent focus paint. Shadows are already
   implemented and remain regression coverage, not another implementation phase.
6. Add semantics/native accessibility and editable/checkbox actions; integrate links/
   URL services and close explicit native/visual evidence plus existing quality gates.

Independent work such as constraint fixes, toggle paint or URL validation need not
wait for all editor internals, provided it uses these ownership/dispatch/publication
boundaries. Windows/Linux native backends, a router/web view/network stack, a new
application example and the Todo application's data model remain outside scope.
