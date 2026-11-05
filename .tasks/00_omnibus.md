---
status: todo
---

# Native application framework backlog

This is an implementation specification for remaining framework capabilities,
not the later Todo application. The architecture and declarations below are chosen
target contracts, not optional sketches or claims that the current API exports them.
Keep their ownership, module boundaries, and event/publication ordering when
implementing; resolve inconsistencies in this specification rather than silently
introducing another runtime or service mechanism.

- **P0 — Functional blocker:** required for normal text entry and editing interactions.
- **P1 — Visual-fidelity gap:** required for faithful controls, typography, and layout.
- **P2 — Supporting completeness:** accessibility, platform integration, and verification.

Reading order: [shared architecture](#shared-architecture-and-implementation-rules),
[P0 interaction](#p0--core-interaction-features),
[P1 layout/rendering](#p1--layout-and-rendering-features),
[P2 platform integration](#p2--accessibility-and-platform-integration),
[P2 verification](#p2--verification-infrastructure), and
[delivery order/boundaries](#boundaries-and-implementation-order).

## Shared architecture and implementation rules

### Cross-platform ownership

Stana is a cross-platform framework. Public APIs must not expose AppKit, Core Text,
native key codes, UTF-16-only ranges, or an operating system's object model. The
retained engine owns component identity, editing, focus, selection, history,
layout, event routing, and accessibility semantics. Host adapters translate
platform services into those contracts.

The macOS backend is the first concrete implementation. Its adapter work is
specified below, but implementing Windows/Linux windowing backends is not part of
this backlog. Portable contracts must build and run headlessly on all supported
build targets. Unavailable native services return `Error::unsupportedPlatform`,
not simulated success. Do not add empty backend trees or placeholder backends.

Preserve the dependency direction: primitives/reactive state feed retained core;
authoring descriptions mount through core; projections produce owning snapshots
for adapters. Core imports `stana.text` for editor storage; `stana.text` must not
depend on UI authoring or native frameworks.

### Platform services and production dispatch

Introduce document-installed internal services for text layout, text-input
activation/queries, plain-text clipboard access, interaction preferences,
accessibility publication, and URL opening. Use typed requests, owning strings,
and explicit `std::expected` failures. Keep service installation separate from
public authoring; `TestHost` exposes deliberate test configuration, not mutable
retained records.

Native hosts and `TestHost` invoke the same document operations. Shortcut/menu
translation belongs to adapters; editing behavior belongs to the engine. Importing
modules or using headless services must not create a native application.

Mutable document/editor state is UI-executor-confined. Native rendering consumes
owning immutable snapshots. Delayed callbacks identify their document, target, and
relevant revision rather than blindly operating on the currently focused node.

### Text layout and presented geometry

Replace the size-only measurement callback with an internal text-layout service.
Requests include UTF-8 text, typography, direction, wrap mode, and available width.
Owning results include measured size, source ranges and baselines for each line,
visual runs, caret stops/affinity, and selection fragments. Cache keys compare
complete layout requests; presentation records pair the resulting layout identity
with the editor's current store stamp.

Measurement, painting, pointer hit testing, accessibility geometry, and candidate
placement use this shared contract. Rendering honors resolved line ranges and
baselines rather than wrapping independently. Shared native font resolution and
line construction must produce matching geometry; caches cannot discard resources
still needed by an owning render submission. The macOS service uses Core Text.
Deterministic headless metrics are test services, not a production font engine.

Extend presentation history to retain editor text geometry alongside node bounds.
Pointer caret placement uses the confirmed layout and validates its content and
composition revisions against current state. Reject stale hits instead of applying
old offsets to newer text. Candidate queries use confirmed visible geometry; with
none available, report geometry unavailable and invalidate native queries after
presentation. Keyboard edits do not wait for drawable acknowledgement.

### Transactions, callbacks, and disposal

An input operation validates its target, updates retained state, writes committed
binding content, settles reactive/layout work, and publishes coherent projections.
Equal model echoes never become another edit.

Copy callbacks and owning event values before invoking application code. Resolve
identities again afterward; never retain node/editor references or iterators across
callbacks, reentrant service calls, or cleanup.

For removal, detach and retire subtree identities before cleanup can reenter.
Keep removed records in an owning removal batch through scope disposal. Cleanup
sees stale handles and cannot erase the same records twice. Capture required
focus/hover notifications before retirement, then deliver owning values after the
structural change is coherent. Removed storage need not remain document-addressable.

### Committed module and implementation boundaries

The declarations in this document are the canonical target contracts. Code blocks
labelled as additions extend existing types; other declaration blocks define new
types. Includes, module prologues, and Doxygen comments are omitted from snippets,
not from the implementation. Unless a namespace is shown, public declarations
are in `stana`; member-only blocks explicitly name their receiving type. Do not
replace these contracts with an unrelated service locator, second editor runtime,
or application-owned focus mechanism.

| Module / implementation                   | Owns                                                                                  | May depend on                                            |
| ----------------------------------------- | ------------------------------------------------------------------------------------- | -------------------------------------------------------- |
| `stana.primitives`                        | Font/style values, geometry, IDs, portable errors                                     | Standard library only                                    |
| `stana.text` / `src/text/`                | Unicode, buffer/history/composition, text-layout value/provider contracts             | Primitives, ICU in implementation                        |
| `stana.graphics` / `src/scene/`           | Paint records and immutable text-layout references                                    | Primitives, text; scene implementation belongs to core   |
| `stana.core` / `src/core/`, `src/layout/` | Document, editor records, input/focus/hover, services, semantics, layout              | Primitives, reactive, tasks, text, graphics              |
| `stana.ui` / `src/ui/`                    | Aggregates, mounting, property observation, responsive factories                      | Core and existing public dependencies; re-export text    |
| `stana.application` / `src/application/`  | Native service installation, explicit transport conversion, runtime URL service       | UI/core/graphics and private native contracts            |
| `src/platform/native_contracts.hpp`       | Module-free owning transport and native service declarations                          | Standard library; no imports or public module types      |
| `src/platform/macos/`                     | OS event translation, Core Text resources, clipboard/accessibility/URL implementation | Private native contracts and native frameworks           |
| `stana.testing` / `tests/`                | Input wrappers and explicit deterministic providers                                   | Same core dispatch; no native application initialization |

Keep templates in module interfaces, non-generic algorithms in compiled sources.
Extend existing modules rather than introduce a new public module per feature.
Split editor dispatch into `src/core/editor.cpp`, semantic projection into
`src/core/accessibility.cpp`, and shaping into `src/platform/macos/text.mm` when
implementing those responsibilities. Register these sources in existing targets;
this document does not create them.

### Text-store protocol and sole ownership of composition

`TextBuffer` is the **only** owner of committed content, committed selection,
composition, and undo/redo history. `EditorState` owns view/interaction state and a
buffer, not another composition string. For editors, `Node::text` is a derived
committed-value mirror for projection, never another writable text store.

The following new values belong to `stana.text`. `TextStoreStamp::content` equals
`TextBuffer::revision()`; `store` advances on any observable content, selection,
or composition change, using never-reused identities. No-op/equal updates advance
neither. This prevents a reentrant clipboard call from applying to a different
selection even when committed content has not changed.

```cpp
struct Utf8Range {
	std::size_t begin = 0;
	std::size_t end = 0; // Half-open, scalar-aligned byte offsets.
	auto operator==(const Utf8Range &) const -> bool = default;
};

struct TextStoreStamp {
	std::uint64_t content = 0;
	std::uint64_t store = 0;
	auto operator==(const TextStoreStamp &) const -> bool = default;
};

enum class TextSpace : std::uint8_t { committed, displayed };

struct TextStoreRange {
	TextStoreStamp stamp{};
	TextSpace space = TextSpace::displayed;
	Utf8Range bytes{};
};

struct SelectionOffsets {
	std::size_t anchor = 0;
	std::size_t head = 0;
	auto operator==(const SelectionOffsets &) const -> bool = default;
};

struct CompositionSnapshot {
	Utf8Range committedReplacement{};
	TextSelection originalSelection{};
	std::string text{};
	SelectionOffsets selection{}; // Relative to provisional UTF-8 text.
	std::uint64_t revision = 0;
};

struct TextStoreSnapshot {
	TextStoreStamp stamp{};
	std::string committed{};
	TextSelection selection{};
	std::optional<CompositionSnapshot> composition{};
	std::string displayed{};
	SelectionOffsets displayedSelection{};
};

struct ReplaceStoreText {
	std::optional<TextStoreRange> replacement{};
	std::string text{};
};

struct UpdateComposition {
	std::optional<TextStoreRange> replacement{};
	std::string text{};
	SelectionOffsets selection{}; // Relative to this request's text.
};

struct CommitComposition {};
struct CancelComposition {};

struct SelectStoreText {
	TextSpace space = TextSpace::displayed;
	SelectionOffsets selection{};
};

using StoreEdit = std::variant<ReplaceStoreText, UpdateComposition,
	CommitComposition, CancelComposition, SelectStoreText>;

enum class UndoGrouping : std::uint8_t { newGroup, joinPrevious };

struct BufferChange {
	bool contentChanged = false;
	bool selectionChanged = false;
	bool compositionChanged = false;
};

// Add to TextBuffer; existing editing methods delegate to the same mutation path.
auto snapshot() const -> TextStoreSnapshot;
auto apply(TextStoreStamp expected, const StoreEdit &edit, UndoGrouping grouping)
	-> std::expected<BufferChange, Error>;
auto canUndo() const noexcept -> bool;
auto canRedo() const noexcept -> bool;
```

Validation precedes all mutation, including history writes. Range length is
`end - begin` only after checking ordering/bounds. Selection-only edits ignore
undo grouping. `setText`, undo, and redo update both revisions when they actually
change the store. Composition revision changes with its provisional text or
selection; its committed replacement is always stamped by the containing snapshot.

Use this mapping, where composition replaces committed `[a,b)` with `P` and occupies
displayed `[a,a + P.size())`:

| Request                                                       | Required normalization in the buffer                                                                                                                                                                                                                   |
| ------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Replacement omitted, no composition                           | Use current committed selection.                                                                                                                                                                                                                       |
| Replacement omitted, composition active                       | Replace the whole provisional region; keep `[a,b)` as its committed replacement.                                                                                                                                                                       |
| Explicit displayed range inside/intersecting provisional text | Expand the replacement's committed extent to include `[a,b)` and any touched committed prefix/suffix. Preserve displayed text outside the explicit range inside that expanded extent. Provisional selection is shifted by the preserved prefix length. |
| Explicit displayed range disjoint from composition            | Map it to committed coordinates, cancel the previous composition, then edit/start composition at that mapped range. Do not implicitly commit discarded preedit.                                                                                        |
| Explicit committed range while composition active             | Cancel preedit and use the requested committed range.                                                                                                                                                                                                  |
| Commit text intersecting composition                          | Resolve the expanded provisional region, including preserved prefix/suffix, as one committed replacement and one undo entry.                                                                                                                           |

All mapping is in `stana.text`, not duplicated in native adapters. Adapters only
convert their encoding/unit conventions and pass missing ranges as `std::nullopt`.
`CommitComposition` is a no-op without preedit; an empty provisional string is
still an active composition and may commit deletion of its replacement range.

`SelectStoreText` in committed coordinates validates grapheme endpoints before
cancelling preedit and moving selection. A displayed selection with both endpoints
between the provisional start and end, inclusive, updates provisional selection;
one wholly outside maps to
committed coordinates and cancels preedit after validation. Reject a displayed
selection crossing a provisional boundary as `invalidTextRange` without mutation;
ordinary pointer/accessibility selection resolves composition first and then uses
committed selection. This avoids guessing an offset inside a replaced string.

### Input addressing and the single editing entry point

Declare these internal contracts in core. `EditingCommand` is defined in the
command section below. `InputSequence` is allocated by `Document::beginInput()`
for each physical key/menu/test action; all callbacks generated while interpreting
that action share it. Native callbacks acquire a fresh target stamp before each
operation, but must not invent another sequence for the same key event.

```cpp
namespace stana::detail {
struct InputSequence {
	std::uint64_t serial = 0;
	bool isRepeat = false;
};

struct TextInputTarget {
	NodeId node{};
	std::uint64_t focusSession = 0;
	TextStoreStamp stamp{};
};

struct TextInputRequest {
	InputSequence sequence{};
	TextInputTarget target{};
	StoreEdit edit{};
};

struct EditingCommandInput {
	InputSequence sequence{};
	TextInputTarget target{};
	EditingCommand command{};
};

enum class InputDisposition : std::uint8_t { handled, unhandled };
enum class EditOrigin : std::uint8_t { keyboard, inputMethod, clipboard, accessibility };

struct TextInputState {
	TextInputTarget target{};
	TextStoreSnapshot store{};
};

// Add to Document.
auto beginInput(bool isRepeat = false) -> InputSequence;
void endInput(InputSequence sequence);
auto textInputState() const -> std::optional<TextInputState>;
auto dispatchText(const TextInputRequest &request)
	-> std::expected<InputDisposition, Error>;
auto dispatchEditing(const EditingCommandInput &request)
	-> std::expected<InputDisposition, Error>;
auto canDispatchEditing(EditingCommand command) const -> bool;
} // namespace stana::detail
```

A focus session changes on focus acquisition and native text-input reactivation,
not on each keystroke. Validate document/target, eligibility, active focus session,
and store stamp in that order. Accessibility addresses an eligible editor directly
and enters the same private `applyEditorEdit(NodeId, TextStoreStamp, StoreEdit,
EditOrigin, UndoGrouping)` helper; it need not first steal keyboard focus.

Track whether an input sequence entered, changed, committed, or cancelled
composition. Submit/cancel/traversal from that sequence is then handled without a
second action. Keep this consumed state in the document's active sequence stack
until `endInput`, not in a global flag or in the buffer. A nested native event gets
its own stack entry; RAII in the adapter closes entries even on exceptions. Clear
repeat-suppression on native key release; synthetic independent test commands use
nonrepeat sequences.

The shared edit helper applies the buffer change, refreshes derived node text,
updates geometry/caret invalidation, then writes `Binding::set()` only if committed
content changed. Capture that value before writing; afterward reacquire the node.
A subscription echo compares actual buffer content, not a blanket `isWriting`
flag: a genuinely different nested model write must still be applied.

### Text-layout values, provider, and native resource lifetime

Declare these in the `stana.text` module's `stana::detail` namespace.
`ResolvedTextStyle` contains scaled font, foreground, optional scaled line height,
strike, and wrap values; scaling happens once in core, never again in the provider
or renderer. A missing maximum width means unconstrained, not infinity encoded as
a floating-point value.

```cpp
namespace stana::detail {
enum class CaretAffinity : std::uint8_t { upstream, downstream };

struct ResolvedTextStyle {
	Font font{};
	Color foreground{};
	std::optional<double> lineHeight{};
	bool strikethrough = false;
	TextWrap wrap = TextWrap::word;
	auto operator==(const ResolvedTextStyle &) const -> bool = default;
};

struct TextLayoutRequest {
	std::string text{};
	ResolvedTextStyle style{};
	Direction direction = Direction::leftToRight;
	std::optional<double> maximumWidth{};
	std::uint64_t fontEnvironmentRevision = 0;
	auto operator==(const TextLayoutRequest &) const -> bool = default;
};

struct TextLine {
	Utf8Range source{};
	Rect bounds{};
	double baseline = 0;
};

struct TextRun {
	Utf8Range source{};
	std::size_t line = 0;
	bool rightToLeft = false;
	Rect bounds{};
	double strikeY = 0;
	double decorationThickness = 1;
};

struct TextCaret {
	std::size_t utf8Offset = 0;
	std::size_t line = 0;
	CaretAffinity affinity = CaretAffinity::downstream;
	Rect bounds{};
	bool isGraphemeStop = false;
};

class TextPaintResource {
  public:
	virtual ~TextPaintResource() = default;
};

struct TextLayout {
	std::uint64_t identity = 0;
	TextLayoutRequest request{};
	Size measured{};
	std::vector<TextLine> lines{};
	std::vector<TextRun> runs{};
	std::vector<TextCaret> carets{};
	std::shared_ptr<const TextPaintResource> paintResource{};
};

class TextLayoutProvider {
  public:
	virtual ~TextLayoutProvider() = default;
	virtual auto layout(const TextLayoutRequest &request) const
		-> std::expected<std::shared_ptr<const TextLayout>, Error> = 0;
	virtual auto selectionRects(const TextLayout &layout, Utf8Range range) const
		-> std::expected<std::vector<Rect>, Error> = 0;
};
} // namespace stana::detail
```

Coordinates are layout-local logical points; line source ranges refer to the
owned request text. Carets are ordered by line then visual x, with a stop for each
scalar boundary needed by native queries and a flag for valid user grapheme
stops. Preserve distinct affinities at bidi boundaries. Selection rectangles come
from resolved visual runs, not an assumed interval between two x coordinates.

Allocate a layout identity with `nextIdentity()` on a cache miss. Use a 256-entry
LRU of weak layout references per provider; prune expired entries during lookup.
A cache hit may reuse layout identity across editor store revisions because the
presentation record carries the separate store stamp. Zero-size fonts produce
zero-size geometry without a paint resource; omit their text paint command.

Add a `std::shared_ptr<const detail::TextLayout> textLayout` field to text paint
commands; ordinary label/editor scene painting always supplies it. Keep legacy
text-command inputs working by resolving missing layout once at native scene
conversion with the same provider, not on each paint worker. Do not change the
existing drawing API as part of this work.

The application bridge implements `NativeTextResource final : TextPaintResource`,
whose sole payload is `std::shared_ptr<const native::ShapedText>`. Forward-declare
`native::ShapedText` in private native contracts; its macOS definition owns immutable
Core Text lines/fonts and their resolved metrics. Layout construction and native
paint use that **same shaped resource**, not independently reconstructed lines.
At conversion, a checked `std::dynamic_pointer_cast` obtains the native wrapper;
a wrong provider/resource is `nativeFailure`, never an unchecked cast or re-shape.
The native text command owns a copy of its `shared_ptr<const native::ShapedText>`.

Build native glyph lines with foreground supplied from the drawing context, not
baked into the immutable resource. The scene command's resolved color is the
foreground; apply its alpha once through context alpha (including color-font
opacity). Paint selected foreground by clipping to selection fragments and drawing
the same layout with the selection-text color. Compute run `strikeY` from baseline
minus half the resolved font x-height and `decorationThickness` from its underline
thickness, clamped to at least 0.5 logical points. Scene line commands draw strike
using those metrics; do not depend on native strikethrough-attribute behavior.
Composition uses a 1-point underline at baseline + 2 points over rectangles for
the entire provisional text range, not only its selected subrange. All overlay
text/decoration commands reuse the same layout and clip/scroll transform; they
must not trigger another shaping request.

Thus buffer/node removal, provider-cache eviction, and document close cannot
invalidate in-flight text. Resource destruction may release immutable Core Text
objects on the final owning thread; UI-only AppKit objects must not be stored in
these resources. Test providers use a separate resource subclass and never enter
native painting. No rendering thread calls back into a document/provider.

Private native request/result records mirror these fields through explicit
conversion in the application bridge. Do not import module-defined types into the
module-free header or duplicate wrapping/range-policy algorithms there.

### Host service installation and URL-service resolution

Use one owning service value per document; these are internal declarations in
core. `SemanticSnapshot` is declared in the accessibility section. Define
`InteractionPreferences` as blink enable/half-period plus accent, secondary-text,
link, active/inactive selection, selection-text, and high-contrast focus colors.
Default blink half-period is 500 ms; the host supplies its resolved palette for
the current environment. No service is selected by whichever window was last focused.

```cpp
namespace stana::detail {
struct InteractionPreferences {
	bool caretBlinkEnabled = true;
	std::chrono::milliseconds caretBlinkHalfPeriod{500};
	Color accent = Color::accent();
	Color secondaryText = Color::rgb(0.45, 0.45, 0.45);
	Color link = Color::accent();
	Color activeSelection = Color::accent();
	Color inactiveSelection = Color::rgb(0.75, 0.75, 0.75);
	Color selectionText = Color::white();
	Color focus = Color::accent();
	Color highContrastFocus = Color::black();
	Color fieldBackground = Color::white();
	Color fieldForeground = Color::black();
	Color fieldBorder = Color::rgb(0.70, 0.70, 0.70);
	double disabledOpacity = 0.5;
};

struct TextInputServices {
	std::function<std::expected<void, Error>(std::optional<TextInputTarget>)> activate{};
	std::function<std::expected<void, Error>(TextInputTarget)> finishComposition{};
	std::function<void(TextInputTarget)> discardComposition{};
	std::function<void(NodeId)> invalidateGeometry{};
};

struct ClipboardServices {
	std::function<std::expected<std::optional<std::string>, Error>()> readText{};
	std::function<std::expected<void, Error>(const std::string &)> writeText{};
};

struct HostServices {
	std::shared_ptr<const TextLayoutProvider> textLayout{};
	TextInputServices textInput{};
	ClipboardServices clipboard{};
	std::function<std::expected<InteractionPreferences, Error>(Environment)>
		resolvePreferences{};
	std::function<std::expected<void, Error>(
		std::shared_ptr<const SemanticSnapshot>)> publishSemantics{};
	std::function<std::expected<void, Error>(const std::string &)> openUrl{};
};

// Replace/add these member declarations inside Document.
static auto create(Size size, Environment environment, HostServices services,
	bool initiallyActive) -> std::shared_ptr<Document>;
auto refreshHostPreferences(std::uint64_t fontEnvironmentRevision)
	-> std::expected<void, Error>;
} // namespace stana::detail
```

Install a complete native service bundle **before mounting**, not after initial
lifecycle effects. Create an owning per-window bridge state before the native view
exists; font services work without a native application, while input activation
waits for attachment. Native documents start inactive; the real key-window event
activates them. Headless documents explicitly start active with test providers.
Service callbacks capture bridge state or weak documents as appropriate, never
strongly capture their owning document. `textLayout` is required at document
creation; absence is invalid initial configuration. Other absent callbacks produce
`unsupportedPlatform` when invoked, not `std::bad_function_call`. Keep the provider
fixed for the document lifetime; font-environment changes invalidate its cache via
the request revision. An activated native client remembers node/session, but queries
a fresh store stamp before every callback. It must not reuse the activation-time
stamp after the first keystroke. Clipboard/URL calls are synchronous on the
UI executor for the current desktop service contract; revalidate state afterward.

Resolve and retain preferences on initial configuration and each `setEnvironment()`;
use the declared defaults only if no resolver exists. If resolution fails, retain
the previous environment/preferences and report the error without partial change.
Add `Document::refreshHostPreferences(std::uint64_t fontEnvironmentRevision)` for
native settings/font notifications: resolve current preferences, update the font
revision, and invalidate layout/paint transactionally. Disabled opacity must be in
[0,1]; a blinking half-period must be positive. The test resolver returns its
`TestPlatform::preferences`; it never consults the operating system.

Free public `openUrl()` resolves an executor-local runtime host service installed
for `Application::run`, independent of focus. It returns `unsupportedPlatform`
without that runtime. `Link` and `TestHost::openUrl()` use the owning document's
service. A test host must never silently override the process/runtime service or
another host's clipboard. A scope guard removes the runtime registration on every
normal/exceptional run exit.

Reuse `TaskScheduler::post()` for caret/autoscroll; do not introduce another clock.
Scheduled callbacks capture weak document, node identity, and separate timer
generation counters. Increment those counters on reset/blur/removal and validate
before rescheduling. Do not reuse `Scope::beginRequest()` for multiple independent
editor timers because its latest-request semantics would cancel each other.

### Native transport and bridge lifetime

The native header must have field-identical module-free mirrors of `InputSequence`,
`TextInputTarget`, text-store snapshots/ranges/edits, command input/disposition,
`PointerInput`, semantic snapshots/actions, and text-layout requests/geometry. Copy
enums with exhaustive switches and strings/vectors as owning values in the
application bridge. Do not serialize JSON, expose erased `void *` payloads, or
share mutable retained records to bypass the module boundary.

Use the following callback fields on `native::Window`, retaining its existing
resize/presentation/close callbacks and replacing scene retrieval with a result.
Types below are the native mirrors, not imports from public modules:

```cpp
namespace stana::native {
// Native transport failure, distinct from portable Error.
enum class FailureKind : std::uint8_t {
	wrongDocument, staleHandle, notFocusable, invalidRange,
	invalidValue, unsupported, cancelled, nativeFailure
};
struct Failure {
	FailureKind kind = FailureKind::nativeFailure;
	std::string diagnostic{};
};
template <typename T> using Result = std::expected<T, Failure>;

// Fields added/replaced inside native::Window.
std::function<Result<std::shared_ptr<const Scene>>()> scene{};
std::function<InputSequence(bool)> beginInput{};
std::function<void(InputSequence)> endInput{};
std::function<std::optional<TextInputState>()> textInputState{};
std::function<Result<InputDisposition>(const TextInputRequest &)> textInput{};
std::function<Result<InputDisposition>(const EditingCommandInput &)> editing{};
std::function<bool(EditingCommand)> canEdit{};
std::function<void(PointerInput)> pointerDown{};
std::function<void(PointerInput)> pointerUp{};
std::function<void(PointerInput)> pointerMove{};
std::function<void()> pointerLeave{};
std::function<void(bool)> windowActive{};
std::function<Result<std::vector<Rect>>(NodeId, TextStoreRange)> textRangeGeometry{};
std::function<Result<TextStoreRange>(NodeId, Point)> textIndexAt{};
std::function<Result<InputDisposition>(const SemanticAction &)> semanticAction{};
} // namespace stana::native
```

The module-free shaping result owns native resources explicitly instead of trying
to mirror core's opaque `TextPaintResource` base:

```cpp
namespace stana::native {
struct ShapedText;

struct TextLayoutResult {
	Size measured{};
	std::vector<TextLine> lines{};
	std::vector<TextRun> runs{};
	std::vector<TextCaret> carets{};
	std::shared_ptr<const ShapedText> resource{};
};

auto layoutText(const TextLayoutRequest &request) -> Result<TextLayoutResult>;
auto selectionRects(const std::shared_ptr<const ShapedText> &text, Utf8Range range)
	-> Result<std::vector<Rect>>;
} // namespace stana::native
```

The application provider assigns portable layout identity, copies geometry, and
wraps the native resource. Keep the resource definition in `macos/text.mm` and
provide a backend-private `macos/text.hpp` drawing helper used by `native_host.mm`.
Only that backend-private helper may mention `CGContextRef`; the module-free
transport remains standard-library-only. Scene conversion returns `Result`; host
failure diagnostics record conversion/raster failures without a presentation
acknowledgement, and a closed/no-longer-available scene returns `cancelled`.

The native mirror `NodeId` is the same two integer fields, not an OS object.
Translate failure categories one-to-one to the portable error table; keep native
diagnostic strings in host diagnostics, not in public event payloads. Native
parameter conversion failure is `invalidRange` before dispatch; a provider/OS
failure is `nativeFailure`.

Create one `native::WindowBridge` state per prepared window, store its owning
pointer in both `WindowSession` and `native::Window`, and capture that state in
host-service callbacks. It contains attachment state and the latest owning native
semantic snapshot; it never owns a document. Window callbacks capture only a weak
document. Text resources are separate immutable owners, not members of live views.

```text
Application WindowSession --strong--> Document --strong--> HostServices
          |                                                |
          +--strong--> native::WindowBridge <--strong-------+
                             |
                             +--callbacks with weak Document references

SceneSnapshot --strong--> TextLayout --strong--> NativeTextResource
native::Scene --strong--------------------------> native::ShapedText
```

Prepare bridge/services, create document, bind weak callbacks, then mount content.
Attach native views later in `run`; native activation starts only after attachment.
Close first marks the bridge inactive, retires document input targets, clears
callbacks/semantic elements, and disposes the document; asynchronous presentation
callbacks still carry existing epoch-qualified evidence. A scoped guard calls
`endInput()` even when callbacks throw or close; closing an already-ended sequence
is a safe no-op, not access to cleared stack storage.

### Settlement phases and publication guarantees

Preparation/publication registration and draft ownership are explicit:

```cpp
namespace stana::detail {
enum class Preparation : std::uint8_t {
	unchanged, ready, needsReactivePass, blocked
};

struct DocumentCommitCallbacks {
	std::function<Preparation()> prepare{};
	std::function<void()> publish{};
};

struct PreparedDocument {
	std::vector<NodeSnapshot> nodes{};
	std::shared_ptr<const SceneSnapshot> scene{};
	std::shared_ptr<const SemanticSnapshot> semantics{};
	PresentationGeometry geometry{};
};

// Add to Document.
auto prepareCommit() -> Preparation;
void publishPrepared();

// Replace ReactiveRuntime::commits' mapped type.
// std::map<std::uint64_t, DocumentCommitCallbacks> commits;
} // namespace stana::detail
```

`ReactiveRuntime` cannot import core to name these records: put `Preparation` and
`DocumentCommitCallbacks` in the `stana.reactive` module's `stana::detail`
namespace. Only `PreparedDocument` belongs to core. Its `PresentationGeometry`
type is defined under state inspection below. One optional draft lives on each
document; it is replaced, not partially published, while settling. `commit()`
requests normal runtime settlement rather than bypassing it to publish immediately.
A prepared document may publish only if every dirty document is ready, unchanged,
or blocked and no observer/transition/size-source work remains.

Replace the runtime's single document-commit callback with paired preparation and
publication callbacks. Documents retain draft layout privately until the global
reactive turn is stable. Use this order, sharing the existing 10,000-step failure
budget across observer batches, preparation retries, and transition callbacks:

```text
1. Prepare/deliver pending observations (structural first, then properties).
2. Reconcile eligibility and drain copied focus/hover transition notifications.
3. Measure/arrange dirty documents into unpublished drafts; collect size offers.
4. Apply changed reader-size sources outside recursive measure/arrange.
5. If steps 1–4 enqueued work, repeat from 1 without publishing.
6. Publish ready node, scene, semantic, and presentation-geometry snapshots together.
7. Run one ready lifecycle effect; if it enqueues work, settle/publish before the next.
8. Finish only when observations, transitions, dirty drafts, and ready effects are empty.
```

Focus callbacks see the last published snapshots plus their owning event values;
they do not observe a half-built draft. `onMount` runs only after its document's
first successful publication. Host publication callbacks must not run application
callbacks inline; enqueue actions caused by publication for the next input sequence.
This does not prohibit synchronous text-client callbacks from `finishComposition()`;
those remain inside the current guarded input sequence and must be revalidated.

If layout/resource preparation fails, publish none of that document's projections,
retain its last complete publication, and report the error. Block retry of the
same failed state until an input/property/host-service revision changes, rather
than spin to the settlement limit or substitute approximate production text.
Other documents can still publish. Native paint failures never confirm presentation.
A semantic sink failure reports an error but does not roll back an already
committed portable semantic snapshot.

Wrong-document identities return `wrongDocument`, retired identities `staleHandle`,
ineligible targets `notFocusable`, stale/malformed text ranges `invalidTextRange`,
invalid text/style/URI values `invalidValue`, missing host operations
`unsupportedPlatform`, and OS/provider failures `nativeFailure`. Invalid initial
aggregate configuration throws `std::invalid_argument`; nonsettling reentrancy is
`std::logic_error`. Report recoverable input failures once through the caller's
result; native/test convenience wrappers append unhandled failures to document
errors. Do not log the same returned failure again inside each lower layer.

## P0 — Core interaction features

### Single-line text-field component

Public declarations in `stana.ui` (event values are declared in core and
re-exported by UI):

```cpp
struct TextEditEvent {
	NodeId target{};
	std::string value{};
};

struct TextField {
	Binding<std::string> value;
	Property<std::string> placeholder{};
	TextProperties placeholderStyle{};
	std::function<void(const TextEditEvent &)> onSubmit{};
	std::function<void(const TextEditEvent &)> onCancel{};
	LayoutProperties layout{};
	TextProperties textStyle{};
	AppearanceProperties appearance{};
	InteractionProperties interaction{};
	AccessibilityProperties accessibility{};
	LifecycleProperties lifecycle{};
};

auto mount(const TextField &description, MountContext &context) -> NodeHandle;
```

Add `NodeKind::textField`, `NodeKind::link`, and `NodeKind::sizeReader` as those
features land. Their diagnostic snapshot kinds are exactly `"textField"`, `"link"`,
and `"sizeReader"`. Do not encode editor identity through a display-text node kind.

Retained ownership in core is fixed as follows; add
`std::unique_ptr<EditorState> editor` to `Node` and leave it empty for noneditors:

```cpp
namespace stana::detail {
enum class UndoGroupKind : std::uint8_t { none, typing, backspace, forwardDelete };

struct UndoGroupState {
	UndoGroupKind kind = UndoGroupKind::none;
	std::uint64_t contentRevision = 0;
	std::size_t expectedHead = 0;
};

struct SelectionDrag {
	TextPosition anchor{};
	Point lastPosition{};
	bool selectsWords = false;
};

struct EditorState {
	explicit EditorState(Binding<std::string> valueBinding);
	Binding<std::string> value;
	TextBuffer buffer;
	std::shared_ptr<const TextLayout> displayLayout{};
	double horizontalScroll = 0;
	CaretAffinity caretAffinity = CaretAffinity::downstream;
	std::optional<SelectionDrag> drag{};
	UndoGroupState undoGroup{};
	std::uint64_t caretTimerGeneration = 0;
	std::uint64_t dragTimerGeneration = 0;
	bool caretVisible = true;
};
} // namespace stana::detail
```

The constructor validates `valueBinding.get()` before mounting its observer.
`Node::scope` owns that observer; `EditorState` must not own another scope. Reset
drag state whenever a content change invalidates its anchor revision. Initial
editor padding is 6 points vertically and 8 points logically leading/trailing;
default background/foreground use the host field palette, with a 1-point neutral
border and 4-point corner radius.

Consumer usage remains ordinary aggregate authoring:

```cpp
const Signal<std::string> name{""};
auto field = TextField{
	.value = name.binding(),
	.placeholder = "Name",
	.textStyle = {.font = Font::system(16)}
};
```

- **Current limitation:** `Text` is display-only; `TextBuffer` has no mounted editor.
- **Outcome/API:** add `TextField` with required `Binding<std::string> value`,
  `Property<std::string> placeholder`, `TextProperties placeholderStyle`, shared property groups,
  and submit/cancel callbacks. Do not pair a display label with a hidden native
  editor whose text store competes with the retained engine.
- **Implementation:** add an editor node kind and occurrence-owned record containing
  `TextBuffer`, horizontal scroll, pointer-selection state,
  caret timing, and undo-group state. Mount once; the scope owns subscriptions and
  scheduled work. Ordinary property changes update this record without replacing it.
- **Synchronization/edges:** committed user edits write the binding once per input
  transaction. Equal model observations preserve selection, composition, revision,
  and history. Different valid external values use `TextBuffer::setText()`, cancel
  composition, clear history, reset selection to the start, and reset horizontal
  scroll. Invalid UTF-8/multiline initial values fail mounting; invalid later model
  values report `invalidValue` and preserve the last valid editor state. Never
  silently rewrite the application's model to hide invalid input.
  Extend buffer validation to reject the full line-break set listed under paste,
  consistently for initial/model values, direct insertion, and provisional text.
  Only paste normalizes those breaks; Enter remains a command, not a text insertion.
- **Defaults:** one unwrapped line. Placeholder appears only with empty committed
  content and no visible composition; it is neither value nor accessible name.
  Placeholder overrides inherit omitted editor text properties. Preferred content
  width is 160 logical points, independent of value length, plus padding; height
  is one resolved line plus padding. Explicit frame sizing overrides this preference.
- **Dependencies/verification:** build on editor dispatch, text layout, and binding
  observation. Test typing, external replacement, equal echoes during composition,
  invalid values, unrelated updates, and stable keyed-row moves. Assert editor
  identity, selection, binding content, and mount count, not only painted text.

### Native text input and IME

- **Current limitation:** native keys only drive traversal/activation; marked text
  lacks replacement-range and provisional-selection metadata.
- **Outcome/API:** define portable internal committed-insertion, composition-update,
  commit/cancel, selection, surrounding-text, and range-geometry operations. Each
  request carries target identity and the relevant text-store revision.
- **Composition:** retain the committed replacement range/original selection,
  provisional UTF-8 string, selection within that string, and composition revision.
  Display committed prefix + provisional text + committed suffix. Updating preedit
  changes neither binding nor undo history. Commit replaces the saved range once
  as one undo operation; cancel restores committed selection without a model write.
- **Ranges/edges:** preserve public committed `TextPosition`/`TextSelection` as
  revisioned, grapheme-safe positions. Use distinct scalar-aligned internal store
  ranges with explicit committed/displayed coordinate spaces. Native provisional
  selection may be scalar-aligned. Convert code-unit ranges in the adapter; reject
  overflow, split scalars/surrogates, out-of-bounds or stale ranges before mutation.
  Explicit replacement inside existing marked text maps through the displayed
  store, not directly into committed bytes. Land the resulting committed caret on
  a grapheme stop without weakening grapheme-safe user commands.
- **macOS adapter:** implement `NSTextInputClient` insertion, marked/unmark operations,
  selected/marked ranges, attributed substring, point-to-index, and first-rectangle
  queries on the host view. Native text interpretation runs before unconsumed
  semantic commands. No `NSTextField` text store is introduced. Convert logical
  document points to screen coordinates here, and invalidate candidate coordinates
  after focus/scroll/layout/presentation changes. External model replacement
  invalidates native composition as well as retained state.
- **Dependencies/verification:** requires editor storage and shared layout geometry.
  Test CJK preedit over selections, marked subrange replacement, cancellation,
  combining marks, dead keys, emoji, supplementary scalars, stale ranges, external
  updates, and candidate movement. Real IME verification is required in addition
  to deterministic injection. A composition-consumed event cannot also submit,
  cancel the application edit, or traverse focus.

### Caret, selection, and editing commands

Declare this portable enum in core and re-export it through UI. Selection-extending
variants are explicit so adapters do not pass native key masks into editor logic:

```cpp
enum class EditingCommand : std::uint8_t {
	moveLeft, moveRight, moveWordBackward, moveWordForward,
	moveLineStart, moveLineEnd,
	extendLeft, extendRight, extendWordBackward, extendWordForward,
	extendLineStart, extendLineEnd,
	deleteBackward, deleteForward, deleteWordBackward, deleteWordForward,
	selectAll, cut, copy, paste, undo, redo, submit, cancel
};
```

`dispatchEditing()` is the only switch that interprets these values. Buffer
mutation, menu actions, native text selectors, and tests may call it but must not
implement parallel command behavior. Word boundaries are computed in text code;
visual left/right stops come from the resolved layout.

Autoscroll uses a 16 ms task interval. Distance outside the left/right content
edge selects direction and speed: `min(1200, 12 * distance)` logical points/second.
Advance scroll by speed times 0.016 seconds, clamp it, then hit-test the clamped
edge in the current confirmed line and extend from the saved anchor. Stop when
released/cancelled, inactive/ineligible, revision-stale, or clamped with no possible
selection change. Test time advances this same calculation.

For a press in the composing editor, capture the confirmed displayed hit before
resolving composition. If the host consumes that press, perform no editor default.
Otherwise finish composition before changing committed selection. Reuse that
captured byte hit only when this resolution left the displayed string and complete
layout request unchanged, with the same live node/focus session; apply it using
the new store stamp. Any unrelated content/style/session change cancels the
pending pointer edit. This is an explicit local transition, not permission to
apply arbitrary stale offsets after model changes.

- **Current limitation:** buffer selection/replacement lacks rendering and input wiring.
- **Outcome/API:** add semantic `EditingCommand` values for visual left/right,
  word backward/forward, line start/end, selection-extending variants, character/
  word deletion, select-all, clipboard/history, submit, and cancel. Adapters map
  platform bindings to these commands; public behavior is not tied to Command/Ctrl.
- **Navigation:** use shaped visual caret order/affinity for bidirectional movement,
  extended graphemes for character deletion, and ICU word segmentation for word
  commands. Treat intervening whitespace/punctuation runs as boundaries. Without
  Shift, character movement collapses a selection toward the requested visual
  edge; Shift preserves its anchor. Line commands use visual line edges.
- **Pointer:** primary press places the caret from confirmed shaped geometry;
  Shift-click extends selection. Double-click selects a word or contiguous
  punctuation/whitespace run. Capture drag input until release/cancel. Dragging
  outside the content box autoscrolls horizontally and extends selection through
  the same clock-driven engine path, even outside the node.
- **Paint/timing:** clip content to the padded editor box; paint selection fragments,
  glyphs/composition decoration, then caret. Keep the moving caret visible and clamp
  horizontal scroll after edits, selection, typography, and size changes. Use host
  selection colors/caret preferences, with a 500 ms blink half-period fallback.
  Caret is solid during input/dragging and inactive while the window is inactive.
  Test time drives blink/autoscroll; scheduled work is scope-owned.
- **Edges/dependencies/verification:** do not split or shrink an oversized grapheme
  to fit. Share text geometry with IME/accessibility. Test reversed and mixed-direction
  selections, emoji/combining deletion, word moves, Shift-click/drag, empty/long text,
  viewport shrink, capture cancellation, deterministic blink, and stale layout hits.

### Clipboard and undo/redo integration

The canonical service interfaces are `ClipboardServices` above; command availability
is a pure query and must not read the OS clipboard or mutate selection/history.
Use this eligibility table after checking active focused-editor eligibility:

| Command               | Available when                                                           |
| --------------------- | ------------------------------------------------------------------------ |
| Copy / cut            | Committed selection is nonempty.                                         |
| Paste                 | A read-text service exists; clipboard contents are checked on execution. |
| Undo / redo           | Corresponding buffer history is nonempty.                                |
| Select all            | Committed text is nonempty.                                              |
| Navigation / deletion | A focused editor exists; boundary no-ops remain handled.                 |
| Submit / cancel       | A focused editor exists, even without an application callback.           |

Pending preedit is cancelled before executing an available explicit clipboard/
history command. Capture the resulting store stamp and selection before a service
call, then require both to remain current before applying its result. A changed
selection is `invalidTextRange`; a lost focus session is `notFocusable`. Never
delete a newly selected range because a cut began against an older one.

- **Current limitation:** buffer history has no clipboard/native command integration.
- **Outcome/API:** focused eligible editors receive cut/copy/paste/select-all/undo/
  redo commands and report availability for native menu validation. Standard host
  shortcuts and menu actions invoke the same path. macOS Command bindings are an
  adapter mapping, not the framework's universal keyboard convention.
- **Clipboard:** services read/write plain text with explicit errors; tests use
  memory-backed services with failure injection. Copy exact committed selection;
  copy/cut are unavailable for empty selection. Cut deletes only after write
  success. Paste replaces each CRLF, CR, LF, NEL, U+2028, or U+2029 sequence with
  one space, preserving other characters including tabs. Empty/absent clipboard
  text is a no-op; malformed UTF-8 does not replace the selection.
- **History:** group contiguous plain typing at the advancing caret and repeated
  same-direction character deletion. Navigation/selection, word delete, focus
  change, clipboard commands, submit/cancel, composition, and external replacement
  close a group. Cut, paste, and IME commit each form one group. A new edit clears
  redo; undo/redo restore text/selection and publish the binding once. No-op edits
  add neither history nor binding writes.
- **Edges:** cancel pending preedit before an explicit history/clipboard command,
  then operate on restored committed selection; native IME consumption still wins
  before command dispatch. Failed services report through document errors without
  changing text/history. Revalidate target/revision after any reentrant service call.
- **Dependencies/verification:** requires semantic commands and binding synchronization.
  Test Unicode round-trips, line-break policy, failed cut, history grouping,
  selection restoration, redo invalidation, unavailable commands, and binding
  echoes after undo/redo. Exercise real clipboard integration in native checks.

### Submit and cancel callbacks

Use the `TextEditEvent` declaration from the text-field section. Each event is a
fresh owning value; there is no retained reference to the current buffer.

```text
native key/menu action -> beginInput -> native interpretation
    -> composition edit/commit/cancel records sequence consumption
    -> unconsumed EditingCommand::submit or ::cancel
    -> capture {target, committed value}, close undo group, copy callback
    -> invoke callback once inside the input transaction
    -> reacquire identity, settle focus/tree changes, endInput
```

When composition consumed the sequence, the submit/cancel branch returns `handled`
without invoking the callback. Neither command forwards to control activation,
even when its callback is empty.

- **Current limitation:** Return activates controls; editors have no completion events.
- **Outcome/API:** use the owning `TextEditEvent` and callback declarations above.
  Values are committed text captured for that dispatch, not borrowed editor storage.
- **Behavior:** an unconsumed Enter/Return submits once; unconsumed Escape cancels
  once. Neither inserts a newline, activates an ancestor, moves focus, disposes
  the editor, or restores the model. Live binding and submission are separate;
  applications own drafts and rollback. Ignore repeat-generated submit/cancel
  until key release, while retaining normal typing/navigation repeat.
- **Composition/blur:** native input gets the event first. Enter confirming preedit
  cannot also submit; Escape cancelling preedit cannot also invoke `onCancel`.
  A later independent command may do so. Blur emits focus loss only, never implicit
  submit. Submit/cancel callbacks that remove or blur the editor do not manufacture
  another submission. Applications may use blur to commit their own application edit.
- **Dependencies/verification:** composition-aware dispatch and safe removal/focus.
  Test one callback per command, captured committed values, no composition
  fallthrough/ancestor activation, and callbacks that replace content, remove the
  editor, request another focus target, or close the document.

### Editor focus and focus-change events

Public event declarations in core:

```cpp
enum class FocusReason : std::uint8_t {
	pointer, traversal, programmatic, eligibility, removal, closure
};

struct FocusEvent {
	NodeId target{};
	std::optional<NodeId> relatedTarget{};
	FocusReason reason = FocusReason::programmatic;
	std::optional<std::string> value{}; // Committed editor value on blur only.
};
```

Add these fields to `InteractionProperties`:

```cpp
std::function<void(const FocusEvent &)> onFocus{};
std::function<void(const FocusEvent &)> onBlur{};
```

The concrete coordinator state is internal to core:

```cpp
namespace stana::detail {
enum class InputModality : std::uint8_t { keyboard, pointer };

struct FocusRequest {
	std::optional<NodeId> destination{}; // Empty means explicitly clear focus.
	FocusReason reason = FocusReason::programmatic;
};

struct TraversalBookmark {
	std::vector<NodeId> order{};
	std::size_t lostIndex = 0;
};

struct FocusState {
	std::optional<NodeId> logicalTarget{};
	std::uint64_t session = 0;
	bool windowActive = false;
	InputModality modality = InputModality::keyboard;
	std::optional<FocusRequest> pending{};
	std::optional<TraversalBookmark> bookmark{};
	bool transitioning = false;
};
} // namespace stana::detail
```

`Document` owns logical focus, active focus-session identity, input modality, and
one optional pending focus request. Request destinations may be empty to clear
focus; an empty pending-request slot is distinct from a pending clear request.
Reentrant requests overwrite that slot. A new acquisition allocates a fresh session
identity only after old blur completes; queued native callbacks using the old
session cannot act on the new target.

Do not keep a node pointer while asking the host to finish composition: a native
commit can synchronously update the model and remove that node. Revalidate before
capturing blur, assigning a destination, and calling `activate()` on the host service.
If host composition finishing fails, report its failure once, revalidate the
target, and commit any still-valid preedit through the normal buffer path before
continuing transfer/deactivation. Never abandon a real window-deactivation event
or publish provisional bytes merely because the host failed to emit a final commit.

- **Current limitation:** handles only focus buttons/toggles; public focus events are absent.
- **Outcome/API:** add shared `InteractionProperties::onFocus` / `onBlur` receiving
  owning `FocusEvent` values: target, optional related target, reason (pointer,
  traversal, programmatic, eligibility, removal, closure), and optional committed
  editor value for blur actions. Related identity does not guarantee a live handle.
- **Coordinator:** clear old logical focus before blur; after callbacks revalidate
  the destination before assigning/notifying focus. Same-target requests do
  nothing. A request inside a callback supersedes the pending destination; process
  the latest request after the callback returns, not recursively in its stack.
- **Traversal/pointer:** editors join buttons, toggles, and links in published
  tree-order Tab/Shift-Tab traversal with wrapping. Initial focus uses `onMount`
  plus `NodeHandle::requestFocus()` after first publication. Eligible primary press
  focuses the nearest focusable ancestor; nonfocusable content/background clears
  focus. Prevented defaults do neither. Presentation-gated input is not an outside click.
- **Activation/composition:** window deactivation retains the logical target but
  suspends active text input, caret, and active focus paint; reactivation restores
  only an eligible target. Do not synthesize blur/focus pairs for window activation.
  Before ordinary transfer/deactivation, ask the host to finish composition, then
  commit any remaining preedit once before blur/suspension. Removal or different
  external model values cancel preedit instead. Headless resolution is deterministic.
- **Dependencies/verification:** centralize transitions before callbacks depend on
  them. Test initial/programmatic focus, traversal, outside/prevented clicks,
  inactive windows, pre-blur committed values, and callbacks that redirect focus,
  remove either target, or close the document.

### Focus validity during tree changes

Define one owning removal batch in core:

```cpp
namespace stana::detail {
struct RemovalBatch {
	std::vector<std::unique_ptr<Node>> retired{};
	std::vector<std::function<void()>> notifications{};
};
} // namespace stana::detail
```

The algorithm is: collect subtree IDs and notification values; detach its root;
move **all** records out of the lookup map; clear focus/press/capture/hover state;
then dispose retired scopes child-first. Copy cleanup callbacks before invoking
them and keep records owned until all cleanup returns. Nested removal finds no
records for retired IDs. Queue owning blur/hover notifications into the transition
phase; document closure drains its captured loss notifications once after teardown
because a closed document will not participate in another commit.

A traversal bookmark is an ordered copy of the focusable IDs at the last loss,
plus the lost index. Scan that order forward/backward, skipping IDs no longer
eligible; if no bookmarked candidate survives, use the first/last currently
focusable node. Clear the bookmark on successful explicit focus acquisition.

- **Current limitation:** removal silently clears focus; eligibility transitions lack a contract.
- **Outcome/API:** a published document cannot focus hidden, collapsed, disabled,
  inert, detached, or disposed controls, including through ancestor policy. Make
  `InteractionProperties::inert` reactive via `Property<bool>`.
- **Reconciliation:** after structural/property updates settle and before projection,
  clear ineligible focus and cancel invalid press/pointer capture. Do not choose
  an unrelated control automatically. Retain the previous traversal position so
  the next forward/backward Tab finds the surviving successor/predecessor, wrapping
  if necessary; explicit requests override that bookmark.
- **Removal/reentrancy:** capture one owning blur event and committed editor value,
  cancel marked state, retire identities through the shared removal batch, clear
  native association, then notify with removal/closure reason. Recheck pending
  destinations after callbacks. Repeated eligibility changes/removal and close from
  blur cannot double-notify. Bound transition settlement using the reactive runtime's
  nonsettling-work diagnostic rather than unbounded recursion.
- **Dependencies/verification:** requires coordinator and callback-safe removal.
  Test all local/ancestor eligibility changes, old/new target removal, blur-driven
  removal/closure, and submit/cancel followed by removal without duplicate submit.
  Assert no published focus on an ineligible identity and no stale-storage access.

### Double-click dispatch

Public pointer values in core and shared-property additions in UI:

```cpp
enum class PointerButton : std::uint8_t {
	primary, secondary, middle, auxiliary1, auxiliary2
};

struct ModifierKeys {
	bool shift = false;
	bool control = false;
	bool alt = false;
	bool meta = false; // Command on macOS; the host's meta key elsewhere.
};

struct PointerInput {
	Point position{};
	PointerButton button = PointerButton::primary;
	ModifierKeys modifiers{};
	std::uint32_t clickCount = 1;
};

struct PointerEvent {
	Point position{};
	PointerButton button = PointerButton::primary;
	ModifierKeys modifiers{};
	std::uint32_t clickCount = 1;
	bool defaultPrevented = false;
	void preventDefault() noexcept { defaultPrevented = true; }
};

// Add to InteractionProperties.
std::function<void(PointerEvent &)> onDoubleClickCapture{};
std::function<void(PointerEvent &)> onDoubleClick{};

// Document pointer-member replacements.
auto pointerDown(PointerInput input) -> std::expected<void, Error>;
auto pointerUp(PointerInput input) -> std::expected<void, Error>;
```

Replace document pointer down/up transport with `PointerInput`; preserve convenience
wrappers that fill its defaults only in `TestHost`. Reject zero click count as
`invalidValue`. Store press-route target separately from the nearest activatable
ancestor so handler-only labels can receive double-click without acquiring a
control action. Native module-free transport copies the same fields explicitly.

Reject nonfinite positions and zero click count before dispatch. No hit is a
normal successful pointer event, not an eligibility error. Native and void test
wrappers append a returned pointer error once; lower-level handlers do not also
log it. Preserve the first completed primary release's target identity to qualify
a second release for double-click; if the target changed, retain the native count
in events but do not manufacture a double-click on the replacement occurrence.

- **Current limitation:** native click counts are dropped; only down callbacks are exposed.
- **Outcome/API:** extend `PointerEvent` with button, modifiers, and positive
  `clickCount` (default one); add shared `onDoubleClickCapture` / `onDoubleClick`.
  Positions remain document-relative logical points.
- **Dispatch:** preserve host click counts rather than synthesize double-click
  timing. On the second eligible primary release inside the pressed target,
  dispatch capture root-to-target, then bubble target-to-root, before that release's
  default activation. Prevention suppresses its default, not callbacks already
  delivered. Do not delay or undo ordinary clicks; the first activation may already
  have occurred. Counts above two do not generate another double-click for that sequence.
- **Defaults/edges:** editor word selection happens on the second press only when
  its target matches the first completed primary release, subject to prevented
  press defaults; release prevention does not retroactively undo it.
  Labels need not become buttons/focusable to handle a double-click. Respect
  disabled/inert/presentation policy, drag cancellation, and release-inside rules.
  Copy the route and skip removed/newly ineligible identities during dispatch.
- **Dependencies/verification:** extend transport and `TestHost` together. Test label
  handlers, capture/bubble order, prevention, ordinary activation plus double-click,
  word selection, count three, disabled ancestors, and mid-dispatch tree changes.

### Hover and pointer tracking

Shared-property declarations and document entry points:

```cpp
// InteractionProperties additions/replacement.
Property<bool> inert = false;
std::function<void(PointerEvent &)> onPointerMove{};
std::function<void(bool)> onHoverChange{};

// Document additions.
auto pointerMove(PointerInput input) -> std::expected<void, Error>;
void pointerLeave();
void setWindowActive(bool active);
```

Store one desktop pointer's presence/last position, hovered ancestry, pressed
route, and optional editor capture in `Document`. Do not add a global hover signal
or per-native-view hover state. `Node::isHovered` is a derived retained bool used
by snapshots. Captured movement also dispatches the editor's drag update, but
ordinary movement callbacks still route through actual hover ancestry.

- **Current limitation:** hosts do not forward movement/exit; occurrences cannot observe hover.
- **Outcome/API:** add shared `onPointerMove` and `onHoverChange` callbacks, the
  latter `std::function<void(bool)>`. Hover means the occurrence or an eligible
  descendant is on the current hit path; true/false transitions represent enter/leave.
- **Implementation:** retain pointer position/presence and hit-test ancestry. Diff
  paths, delivering leaves deepest-first and enters ancestor-first. Movement bubbles
  on the current path. Moving among descendants preserves parent hover. Pointer
  capture for drag does not redefine hover.
- **Recomputation:** after scrolling, eligibility changes, removal, or confirmed
  geometry changes, recompute even without motion. Clear on host exit, window
  deactivation, or presentation uncertainty; recompute after confirmation. macOS
  supplies tracking areas/moved/dragged/exited events. Engine coordinates alone do
  not imply native window presence.
- **Edges:** update retained hover before callbacks, copy notifications for removal,
  and settle callback-driven geometry changes in a bounded loop. To reveal an action
  without changing row geometry, reserve its space using `hidden`, not `collapsed`
  or conditional disposal. No separate hover-layout primitive is needed.
- **Dependencies/verification:** pointer transport, presentation gates, and safe
  callbacks. Test descendant moves, exit while captured, stationary-pointer scroll,
  removal/disable, geometry acknowledgement, and hover-revealed controls with stable bounds.

## P1 — Layout and rendering features

### Visible toggle state

Value declarations belong to primitives; the property belongs to `Toggle`:

```cpp
enum class ToggleIndicator : std::uint8_t { checkbox, circularCheckbox, chevron };

struct ToggleIndicatorStyle {
	ToggleIndicator kind = ToggleIndicator::checkbox;
	double size = 18;
	double labelGap = 8;
	std::optional<Color> checkedColor{};
	std::optional<Color> uncheckedColor{};
	std::optional<Color> markColor{};
	auto operator==(const ToggleIndicatorStyle &) const -> bool = default;
};

// Add to Toggle.
Property<ToggleIndicatorStyle> indicatorStyle{};
```

Missing checked/unchecked/mark colors resolve to host accent, secondary text, and
selection-text colors respectively. Keep indicator geometry in the node's layout
result, not embedded in the label string. Checkbox/circle outline width is 1 point;
check/chevron stroke is 2 points. Apply disabled opacity to the whole control once.
The checked mark uses normalized points `(0.22,0.52) -> (0.43,0.72) -> (0.78,0.28)`;
chevron points are `(0.22,0.38) -> (0.50,0.66) -> (0.78,0.38)`, scaled by size.

- **Current limitation:** retained `checked` state produces no indicator.
- **Outcome/API:** add reactive `Toggle::indicatorStyle`: kind (`checkbox`,
  `circularCheckbox`, `chevron`), logical size/gap, and checked/unchecked/mark colors.
  Default to an 18-point square checkbox and 8-point label gap. Keep `Binding<bool>`;
  do not introduce tri-state state for application-computed bulk bool actions.
- **Measurement/paint:** reserve indicator width independently of label, omit gap
  for empty labels, and wrap only the label. Align the indicator with the first
  text line, at logical leading in either direction. Use rectangle/line commands
  for outlines/checks, half-size corner radius for circles, and line geometry rather
  than a font glyph for chevrons. Chevron bool state changes muted/checked color.
- **Behavior/edges:** replace button-like toggle defaults with a neutral labelled
  checkbox treatment. Pointer/keyboard/handle/accessibility share binding activation.
  Checked paint is distinct from label, hover, disabled, and focus state; state
  changes do not alter geometry. Shared appearance still styles the outer control.
- **Dependencies/verification:** shared layout/scene and checked snapshots. Test all
  styles/states, external updates, empty/wrapped labels, RTL, disabled activation,
  stable identity, and scene differences independent of label text.

### Multiline text layout

Declare `TextWrap` in primitives and use it in `TextProperties` and layout requests:

```cpp
enum class TextWrap : std::uint8_t { none, word, grapheme };

// Add to TextProperties.
std::optional<Property<TextWrap>> wrap{};
```

`TextLayoutProvider::layout()` produces all lines in one immutable result. Its
`TextLine::source` range includes that line's consumed hard-break bytes; break
characters themselves have no glyph ink. The final empty line has an empty source
range at text end. Run ranges stay inside their line. Normalize CRLF as one hard
break for layout without rewriting source bytes, so native range queries retain
original offsets. The provider must prove forward progress by consuming at least
one complete grapheme whenever positive width requires a forced break.

- **Current limitation:** measurement/paint each build one native line; capped width does not wrap.
- **Outcome/API:** add reactive `TextProperties::wrap` with `none`, `word`, and
  `grapheme`. Display labels default to `word`; editors force `none`. Honor hard
  breaks and preserve an empty final line after a trailing break.
- **Implementation:** layout chooses lines at effective content width after frame
  constraints/padding. Word mode uses Unicode line-break opportunities, falling
  back to grapheme breaks for oversized tokens; grapheme mode uses fitting grapheme
  boundaries. Paint exact resolved ranges/baselines. macOS uses Core Text typesetter
  suggestions with grapheme-safe fallback, never byte-count width estimates.
- **Edges:** empty text occupies one line. At zero width, return one-line height
  without repeated line production. An indivisible oversized grapheme occupies
  one line and clips; never split it. Height constraints clip extra lines without
  introducing ellipsis. Unbounded width has no soft wrapping. Cache the full text/
  typography/direction/scale/wrap/width request.
- **Dependencies/verification:** shared layout and remeasurement at allocated widths.
  Test long tokens/URLs, spaces, CJK, emoji, hard/trailing breaks, bidi, zero/tiny
  width, and resize reflow. Check measured/painted bounds and adjacent controls;
  scene-command counts alone do not verify native wrapping.

### Text decoration and typography

Replace the font declaration in primitives and extend shared text properties:

```cpp
enum class FontWeight : std::uint16_t {
	thin = 100, extraLight = 200, light = 300, regular = 400,
	medium = 500, semibold = 600, bold = 700, extraBold = 800, black = 900
};

enum class FontStyle : std::uint8_t { normal, italic };

struct Font {
	double size = defaults::fontSize;
	FontWeight weight = FontWeight::regular;
	std::string family{};
	FontStyle style = FontStyle::normal;
	static auto system(double size, FontWeight weight = FontWeight::regular) -> Font;
	auto operator==(const Font &) const -> bool = default;
};

// Add to TextProperties; retain existing font/foreground properties.
std::optional<Property<FontStyle>> fontStyle{};
std::optional<Property<bool>> strikethrough{};
std::optional<Property<double>> lineHeight{};
```

Weights are portable requests, not native floating-point constants. Map named
weights to each platform's corresponding named font weight; macOS conversion uses
AppKit weight constants inside its adapter. Resolve text style in this order:
component default/inherited placeholder base, optional full `font`, optional
`fontStyle` override, remaining properties, then one environment scaling pass.
This permits an italic placeholder without guessing/copying its editor's font size:

```cpp
auto field = TextField{
	.value = name.binding(),
	.placeholder = "What needs to be done?",
	.placeholderStyle = {.fontStyle = FontStyle::italic},
	.textStyle = {.font = Font::system(20)}
};
```

The merge is one helper used by mount/layout/placeholder resolution, not separate
native and editor implementations. Font size zero or text scale zero yields
zero-size text geometry with no native font creation; negative/nonfinite sizes
and nonpositive explicit line heights fail validation.

- **Current limitation:** style exposes only system font size/weight and foreground.
- **Outcome/API:** extend `Font` with owned family name (empty means system) and
  italic style; add reactive `TextProperties::strikethrough` and optional explicit
  `lineHeight`. Placeholder overrides inherit editor typography, defaulting only
  their foreground to host secondary-text color.
- **Resolution:** host services resolve family/weight/italic together and supply
  missing-family/glyph fallback. Use the complete request for layout, transport,
  native drawing, and cache invalidation. macOS uses font descriptors/traits and
  Core Text fallback, not application-specific font-name branches.
- **Metrics/paint:** absent line height uses host metrics; explicit finite positive
  height and font size are logical points scaled once by environment text scale.
  Center natural ascent/descent within that line box; a deliberately small height
  can overlap ink and is not silently enlarged. Strikethrough follows shaped run
  baseline metrics/foreground and changes paint, not line count.
- **Edges/dependencies/verification:** validate invalid family strings and nonfinite
  dimensions before assignment; preserve zero-scale behavior without invalid native
  font creation. Font/line-height changes invalidate layout; decoration/color alone
  do not change breaks. Test italic placeholders, strike, fallback/mixed scripts,
  line heights, scaling, and updates without remounting against native paint.

### Remaining-space allocation in horizontal stacks

Use these core layout records; an absent proposed axis is unconstrained:

```cpp
namespace stana::detail {
struct LayoutProposal {
	std::optional<double> width{};
	std::optional<double> height{};
};

struct ChildSizing {
	NodeId id{};
	Frame frame{};
	Size intrinsic{};
	bool collapsed = false;
};

struct ChildAllocation {
	NodeId id{};
	Size size{};
};

auto allocateHorizontal(const std::vector<ChildSizing> &children,
	LayoutProposal contentProposal, double spacing)
	-> std::expected<std::vector<ChildAllocation>, Error>;
} // namespace stana::detail
```

Padding is already removed from `contentProposal`; subtract it only once. Build
non-fill intrinsic inputs first, allocate widths, then remeasure fill children
using those widths and derive row height from their final heights. `ChildAllocation`
is retained in the draft for arrangement; do not rerun an equal-share calculation
against a different width after wrapping. A collapsed child gets zero size and
creates no gap. Hidden children use normal sizing.

- **Current limitation:** children measure/fill against full row width regardless of siblings.
- **Outcome:** one constrained allocation drives measurement and arrangement;
  this is not a new flexbox/weighted-shrink engine.
- **Algorithm:** exclude collapsed children, subtract padding and participating
  gaps, then reserve non-fill children. Points use constrained requests, percentages
  resolve against full padded content width, and automatic children reserve
  constrained intrinsic width. Hidden children participate normally. Reserve fill
  minimum widths, split remaining space equally, freeze max-constrained children,
  and redistribute excess among the rest. If minimums exceed space, retain them
  and overflow rather than create negative widths or shrink fixed siblings.
- **Remeasurement:** measure fill contents at assigned width to determine wrapped
  height; retain this allocation for arrangement. Unbounded rows measure fills
  intrinsically with constraints rather than splitting infinity. Leave surplus at
  logical trailing after all fill maxima. RTL mirrors placement, not identity or allocation.
- **Dependencies/verification:** shared constraints and text layout. Test fixed
  indicator/fill label/fixed action, multiple fills, min/max redistribution,
  percent-plus-fill, hidden/collapsed, zero width, overflow, unbounded proposals,
  nested wrapping, and RTL. No later arrange pass may re-expand each fill to full width.

### Constraint preservation during arrangement

One resolver in `src/layout/layout.cpp` is used on both axes:

```cpp
namespace stana::detail {
auto resolveAxis(Length request, double intrinsic, std::optional<double> offered,
	std::optional<double> minimum, std::optional<double> maximum)
	-> std::expected<double, Error>;
} // namespace stana::detail
```

The resolution table is part of the contract:

| Request                             | Bounded offer                             | Unconstrained offer |
| ----------------------------------- | ----------------------------------------- | ------------------- |
| automatic                           | Intrinsic extent measured with that offer | Intrinsic extent    |
| points                              | Requested points                          | Requested points    |
| percent                             | Percentage of offered extent              | Intrinsic extent    |
| fill outside a horizontal main axis | Offered extent                            | Intrinsic extent    |
| horizontal fill                     | Stored `allocateHorizontal()` result      | Intrinsic extent    |

Validate constraints, resolve the request, then apply min/max. Arrange uses the
resolved allocation and only recomputes alignment/position, not another stretch.
If final allocated width differs from a measured text width, invalidate and
remeasure before publication rather than draw text using stale line breaks.

- **Current limitation:** root/fill arrangement overwrites constrained measured sizes.
- **Outcome:** final bounds honor explicit sizing and min/max on both axes,
  including root descendants and centered max-width content.
- **Implementation:** share axis resolution/validation across measure/arrange.
  Resolve offer, clamp final size, then calculate alignment. Never re-expand a
  max-constrained fill. Reuse the horizontal allocation rather than resolving row
  fill again. Wrapping must see the final constrained width.
- **Defaults/edges:** internal root fills viewport; user children follow their own
  frames, without implicit stretch that defeats constraints. Center capped content
  using an existing center-aligned `Box` and max-constrained fill child. Diagnose
  min greater than max and nonfinite/negative constraints. A minimum beyond parent
  space may overflow; parent clip policy determines visible overflow.
- **Dependencies/verification:** land with horizontal allocation. Test capped
  panels across resize, explicit root-child sizes, nested fill, both axes, percent
  requests, invalid min/max, and minimum overflow with/without clipping.

### Responsive composition

Public declarations in UI:

```cpp
// Add to ComponentContext.
auto viewportSize() const -> Computed<Size>;

template <typename ContentFactory> struct SizeReader {
	ContentFactory content;
	LayoutProperties layout{};
	AppearanceProperties appearance{};
	LifecycleProperties lifecycle{};
};

template <typename ContentFactory>
auto mount(const SizeReader<ContentFactory> &description, MountContext &context)
	-> NodeHandle;
```

The factory signature is `Component(Computed<Size>)`: its return type satisfies
the existing `Component` concept. It runs once during mounting. `Document` owns
its viewport `Signal<Size>`; a reader node owns its local offer `Signal<Size>`.
Factories/context receive `computed([source] { return source.get(); })`, never the
writable source. Reader scope disposal cancels observations normally.

Seed a reader from the finite parent proposal known at mount, falling back per
unconstrained axis to the viewport. First layout corrects the seed before effects.
Measure/arrange only collect new offers into a draft vector. Apply that vector
between passes; equality suppresses repeats. Reader content height contributes
to its measured height, but never feeds its own offered-height signal.

Example footer composition (the size factory is not rerun on resize):

```cpp
auto footer = SizeReader{
	.content = [](Computed<Size> available) {
		return If{
			.condition = available.map([](Size size) { return size.width < 480; }),
			.thenContent = VStack{
				.children = children(Text{.text = "Items remaining"}, Text{.text = "Filters"})
			},
			.elseContent = HStack{
				.children = children(Text{.text = "Items remaining"}, Text{.text = "Filters"})
			}
		};
	}
};
```

- **Current limitation:** mount-time environment is not a live viewport/container size input.
- **Outcome/API:** provide both `ComponentContext::viewportSize() -> Computed<Size>`
  and `SizeReader<Content>` with mount-once `.content` factory receiving local
  available `Computed<Size>`. The reader accepts shared layout/appearance groups.
  Expose read-only handles, not writable window-size signals.
- **Size contract:** viewport is content size in logical points, excluding chrome.
  Local size is parent-offered space after reader frame constraints/padding, not
  descendant intrinsic output. Internally represent unconstrained proposals
  explicitly; expose a finite offer, not infinity or child-derived height. Substitute viewport extent only for an unconstrained axis; retain finite
  parent offers even when larger than the viewport. Reader defaults to fill width
  and automatic content height so a footer can grow when its narrow branch gains a line.
- **Settlement:** resize updates viewport inside a transaction. A layout-preparation
  phase determines local offers, queues changed sources, then settles reactive
  properties/structural branches between measurement passes. Do not invoke content
  factories or mutate signals inside recursive measurement. Publish snapshots and
  run lifecycle effects only after size/layout settle together. Extend bounded
  runtime settlement to repeated preparation/commit cycles, not observers alone.
- **Identity/edges:** content factory runs once per occurrence, not per pixel.
  Same-branch property changes preserve descendants. Explicit `If` changes retain
  ordinary disposal semantics; durable editor state belongs outside replaced
  branches. Equal offers do not notify; initial responsive work settles before
  `onMount`. No new wrapping-stack primitive is required in this pass.
- **Dependencies/verification:** constraint-aware proposals and runtime settlement.
  Test independent viewport/nested capped sizes, footer one/two-line transitions,
  below/at/above breakpoints, padding, unchanged-branch editor identity, disposal,
  bounded feedback, and no effects/scenes observing intermediate responsive branches.

### Drop and inset shadows

Value types belong to primitives; appearance/paint additions use them directly:

```cpp
enum class ShadowMode : std::uint8_t { outset, inset };

struct Shadow {
	Point offset{};
	double blur = 0;
	double spread = 0;
	Color color = Color::clear();
	ShadowMode mode = ShadowMode::outset;
	auto operator==(const Shadow &) const -> bool = default;
};

// Add to AppearanceProperties.
std::optional<Property<std::vector<Shadow>>> shadows{};

// Add PaintKind::shadow and this payload to PaintCommand.
std::optional<Shadow> shadow{};
```

For a shadow command, existing bounds/radius describe the original surface;
`shadow->color` is authoritative and already includes inherited opacity.
`PaintCommand::color` is ignored for that opcode. Mirror the opcode/payload in
private native contracts. Rasterize masks with a separable normalized Gaussian
kernel of radius `ceil(3 * sigma * backingScale)`; use checked vector extents and
the existing native bitmap byte/dimension limits. Clamp spread-adjusted corner
radius to half the smaller adjusted extent. Fully vanished shapes yield an empty
outset mask; inset still uses the original surface's clipping bounds.

- **Current limitation:** appearance/paint transport lacks shadows.
- **Outcome/API:** add `Shadow` with logical offset, nonnegative blur, signed spread,
  color, inset/outset mode, and reactive ordered `AppearanceProperties::shadows`.
  Empty is default; multiple shadows support layered elevation without extra nodes.
- **Implementation:** explicit scene/native shadow operations derive rounded-rect
  masks, grow/shrink by spread, offset, and blur. Define Gaussian sigma = blur / 2
  with three-sigma support; zero blur gives a hard mask. Clamp shrunken extents/
  radii. Inset uses complementary mask clipped inside the original surface, not
  an outward shadow drawn over it.
- **Paint order:** outset shadows in list order, background, inset shadows in list
  order, content, normal border, focus. Later entries within a group paint above
  earlier ones. Own content clipping does not erase outset shadows; ancestor clips
  still apply. Multiply accumulated opacity once.
- **Edges/dependencies:** layout/hit targets ignore shadow overflow. Allocate masks
  at backing scale including spread/blur support, using existing checked bitmap
  resource limits. Reject nonfinite parameters; report resource failure, not silent
  omission. Retain resources through owning submissions.
- **Verification:** hard/blurred, positive/negative spread, rounded, inset/outset,
  layered, clipped, transparent/reactive effects at 1x/2x. Assert order and unchanged
  layout/hit regions, plus native pixels rather than scene structure alone.

### Configurable focus appearance

The style is a primitive value; scene projection remains a separate stroke:

```cpp
struct FocusRingStyle {
	std::optional<Color> color{}; // Omitted selects current host focus color.
	double width = 2;
	double offset = 2;
	std::optional<double> radius{}; // Omitted follows surface radius.
	auto operator==(const FocusRingStyle &) const -> bool = default;
};

// Add to AppearanceProperties.
std::optional<Property<FocusRingStyle>> focusRing{};
```

Define offset as the gap from surface edge to the ring's inner edge. Expand the
stroke centerline rectangle by `offset + width / 2`; expand the surface-derived
radius by the same amount. An explicit radius applies to that centerline. Track
input modality at document level, not on individual controls. Keyboard activity
makes an already focused eligible control's ring visible without reacquiring focus.

- **Current limitation:** forced focus outline replaces the normal border.
- **Outcome/API:** reactive `AppearanceProperties::focusRing` with color, positive
  width, nonnegative offset, and optional radius override. Default is a 2-point
  host-accent ring offset 2 points, following the surface radius.
- **Implementation:** preserve the normal border and paint focus separately,
  without layout/hit expansion. Keyboard traversal and programmatic focus show
  the ring; pointer focus suppresses it. Editor caret/selection still indicate
  text focus. Window inactivity suspends active ring painting.
- **Edges:** omission means default, not invisible keyboard focus. Reject zero-width
  or fully transparent custom rings. Honor high-contrast host colors. Ancestor
  clipping applies, but own content clipping must not erase the overlay. Checked
  or selected state never substitutes for keyboard focus.
- **Dependencies/verification:** focus/input modality and appearance projection.
  Test custom rounding, borders preserved, pointer/keyboard transitions, inactive
  windows, high contrast, clipping, invalid styles, and unchanged geometry.

## P2 — Accessibility and platform integration

### Native accessibility tree

`AccessibilityRole` belongs to primitives, `AccessibilityProperties` to UI, and
semantic values/actions to core. This prevents core from importing UI to name roles:

```cpp
enum class AccessibilityRole : std::uint8_t {
	group, staticText, button, checkbox, textField, link
};

struct AccessibilityProperties {
	std::optional<AccessibilityRole> role{};
	std::optional<Property<std::string>> label{};
	std::optional<Property<std::string>> description{};
};

namespace detail {
enum class SemanticActionKind : std::uint8_t {
	activate, focus, setValue, replaceText, setSelection
};

struct SemanticNode {
	NodeId id{};
	std::optional<NodeId> parent{};
	std::vector<NodeId> children{};
	AccessibilityRole role = AccessibilityRole::group;
	std::string name{};
	std::string description{};
	std::optional<std::string> value{};
	Rect bounds{};
	bool isEnabled = true;
	bool inert = false;
	bool isFocused = false;
	bool hasActiveFocus = false;
	std::optional<bool> checked{};
	std::optional<TextStoreSnapshot> editor{};
	std::vector<SemanticActionKind> actions{};
};

struct SemanticSnapshot {
	std::uint64_t revision = 0; // Same publication revision as the scene.
	NodeId root{};
	std::vector<SemanticNode> nodes{}; // Preorder, native-object-free.
};
} // namespace detail
```

Preserve one semantic document-root group even when all other structural groups
flatten. Role overrides change semantic role only: they do not grant unsupported
editor/control actions. Adapter element caches key by full document/node identity,
not vector position or visible label. Diff native notifications in structure,
name/description, value/checked, selection, then focus order; coalesce each category
per identity/revision. Unchanged snapshots emit no change notification.

Native bounds queries map semantic identity to confirmed document geometry and
perform screen conversion at query time. If there is no confirmed geometry, return
the native unavailable/empty-frame result rather than fabricate pending bounds.
Use the full `NodeId` in both caches and callbacks so two windows with identical
labels cannot share semantic identity or actions.

- **Current limitation:** explicit labels have no semantic projection/native bridge.
- **Outcome/API:** extend `AccessibilityProperties` with role override and reactive
  label/description. Built-ins default to static text, button, checkbox, text field,
  or link. Publish owning semantic snapshots with identity, hierarchy, role/name/
  value, bounds, availability, focus, and supported actions.
- **Projection:** explicit label wins, including explicitly empty label; otherwise
  derive a control name from visible label text. Placeholder is a hint, never value
  or automatic name. Diagnostic names never become accessible names. Flatten
  unnamed structural nodes but preserve labelled groups/order. Omit hidden,
  collapsed, and disposed subtrees. Keep disabled controls as unavailable; inert
  content remains readable without actionable/focusable operations.
- **Adapter:** diff complete snapshots by identity and preserve native element
  identity while occurrences survive. Retire removed elements without retaining
  mutable nodes. Native actions enter document operations with current eligibility
  checks. Screen frames use confirmed geometry, not pending layout. macOS maps
  this projection to native elements/actions/notifications; portable categories
  must not depend on VoiceOver or `NSAccessibility` types.
- **Dependencies/verification:** stable identity, focus, action paths, semantic
  diffing; editable capabilities follow below. Test names/hierarchy, reactive
  updates, stale actions, disabled/inert, disposal, and geometry. Verify real
  VoiceOver discovery/traversal/activation on the current adapter.

### Editable-text and checkbox semantics

Use typed action payloads, not an unvalidated operation name plus arbitrary fields:

```cpp
namespace stana::detail {
struct SemanticActivate {};
struct SemanticFocus {};
struct SemanticSetValue {
	TextStoreStamp expected{};
	std::string value{};
};
struct SemanticReplaceText {
	TextStoreRange range{};
	std::string text{};
};
struct SemanticSetSelection {
	TextStoreStamp expected{};
	SelectionOffsets selection{}; // Committed coordinates, grapheme-safe.
};
using SemanticOperation = std::variant<SemanticActivate, SemanticFocus,
	SemanticSetValue, SemanticReplaceText, SemanticSetSelection>;

struct SemanticAction {
	NodeId target{};
	SemanticOperation operation{};
};

// Add to Document.
auto dispatchSemantic(const SemanticAction &action)
	-> std::expected<InputDisposition, Error>;
auto textRangeGeometry(NodeId target, TextStoreRange range) const
	-> std::expected<std::vector<Rect>, Error>;
auto textIndexAt(NodeId target, Point documentPoint) const
	-> std::expected<TextStoreRange, Error>;
} // namespace stana::detail
```

Only advertise actions currently supported by the occurrence and eligibility.
`SemanticReplaceText` requires committed coordinate space. Convert incoming native
units against a captured store, validate its stamp, cancel preedit, then enter the
same editor mutation helper using the freshly validated committed range. Range
geometry uses presentation-retained layout, its origin/scroll/clips, and checks
that its store still matches the request. Unavailable geometry returns `cancelled`;
a mismatched store returns `invalidTextRange`, never a caret for different content.
`textIndexAt()` returns a collapsed displayed-store range with the matching stamp.

- **Current limitation:** native interfaces cannot read/edit text/selection or checkbox state.
- **Outcome:** typed semantic text capabilities expose committed value, revisioned
  selection, text/range geometry queries, and set-value/replace-range/set-selection
  actions. Toggle semantics expose checked bool regardless of visual indicator.
- **Actions:** validate identity/ranges, cancel provisional composition, and use
  normal editor replacement and binding publication. Full set-value is one undoable
  full-range edit, not an external model reset. Selection-only actions do not add
  text history. Checkbox actions invoke normal activation.
- **Notifications/edges:** diff after coherent publication for text-value, selection,
  checked, focus, and structure changes. Composition-only updates do not announce
  committed-value changes; native displayed-store queries may still expose marked
  state using the same range mapping as input services. Disabled/inert nodes allow
  inspection but reject actions. Stale/invalid requests fail without partial mutation.
- **Dependencies/verification:** editor commands/ranges, semantic tree, presentation
  geometry. Test accessibility-driven edit/selection with emoji, preedit, undo,
  echoes, disposal, disabled actions, checkbox state, and real native notifications
  through the host accessibility inspector/screen reader.

### Links and external URL opening

Public UI/application declarations:

```cpp
struct Link {
	Property<std::string> text{};
	Property<std::string> destination{};
	std::function<void(Error)> onOpenError{};
	LayoutProperties layout{};
	TextProperties textStyle{};
	AppearanceProperties appearance{};
	InteractionProperties interaction{};
	AccessibilityProperties accessibility{};
	LifecycleProperties lifecycle{};
};

auto mount(const Link &description, MountContext &context) -> NodeHandle;
// Export from stana.application and therefore the stana umbrella.
auto openUrl(const std::string &destination) -> std::expected<void, Error>;
```

Core owns the URI/service operation; UI must not import application to activate a
link. Declare these internal core entry points and implement them in
`src/core/services.cpp` alongside host-preference/service resolution:

```cpp
namespace stana::detail {
auto validateExternalUrl(std::string_view destination) -> std::expected<void, Error>;
// Add to Document.
auto openUrl(const std::string &destination) -> std::expected<void, Error>;
} // namespace stana::detail
```

`Document::openUrl()` validates then invokes its service. The free application
function uses the same validator before invoking the runtime service. Keep URI
validation in that one helper, not separate native/link checks. Store destination and error callback
on the link record; one copied default activation invokes its document service.
Do not retain a pointer to the temporary `Link` description or resolve URL service
through current focus. The runtime/document service distinction is fixed above.

- **Current limitation:** no link component or public platform URL opener.
- **Outcome/API:** add `Link` with reactive text/destination, shared groups, and
  `onOpenError(std::function<void(Error)>)`; add public
  `openUrl(const std::string &) -> std::expected<void, Error>`. Use installed host
  services, not private headers, a router, browser view, or network stack.
- **Behavior:** pointer/keyboard/handle/accessibility share activation, capturing
  current destination once. Default link has underline, host link color, focus
  eligibility, and semantic destination. Disabled/inert/default prevention follow
  other controls.
- **Validation/failure:** require valid UTF-8 and an absolute URI with syntactic
  scheme; reject embedded NUL/unescaped ASCII whitespace/control. Do not restrict
  valid schemes to HTTP(S). Return invalid input, unsupported service, or native
  failure explicitly. Success means platform-handler acceptance, not remote load.
  Link invokes copied error callback or reports through document errors.
- **Adapter/dependencies:** macOS uses system workspace opening. Without host
  service return `unsupportedPlatform`; tests record requests rather than launch
  a browser. Depends on activation/focus/semantics, not editor internals. This is
  supporting completeness, not a task-management-model blocker.
- **Verification:** all activation paths, reactive destination, invalid/custom
  schemes, absent service, handler failure, and callback disposal. Real external
  opening is confined to explicit opt-in native verification.

## P2 — Verification infrastructure

### Production-path input injection

Expose aliases to the canonical protocol values from the separately imported
`stana.testing` module; aliases do not define another transport or runtime:

```cpp
using TestInputSequence = detail::InputSequence;
using TestTextInputState = detail::TextInputState;
using TestTextInputRequest = detail::TextInputRequest;
using TestEditingCommandInput = detail::EditingCommandInput;
using TestInputDisposition = detail::InputDisposition;
using TestTextLayout = detail::TextLayout;
using TestTextLayoutRequest = detail::TextLayoutRequest;
using TestTextLayoutProvider = detail::TextLayoutProvider;
using TestTextPaintResource = detail::TextPaintResource;
using TestSemanticSnapshot = detail::SemanticSnapshot;
using TestSemanticAction = detail::SemanticAction;
using TestInteractionPreferences = detail::InteractionPreferences;

struct TestPlatform {
	std::shared_ptr<const TestTextLayoutProvider> textLayout{};
	TestInteractionPreferences preferences{};
	std::optional<std::string> clipboardText{};
	std::optional<Error> nextClipboardReadError{};
	std::optional<Error> nextClipboardWriteError{};
	std::optional<Error> nextOpenUrlError{};
	std::vector<std::string> openedUrls{};
	std::vector<std::shared_ptr<const TestSemanticSnapshot>> semanticPublications{};
};

// TestHost constructor and member declarations; existing methods remain.
explicit TestHost(Size size = {.width = 800, .height = 600},
	Environment environment = {}, std::shared_ptr<TestPlatform> platform = {});
auto beginInput(bool isRepeat = false) -> TestInputSequence;
void endInput(TestInputSequence sequence);
auto textInputState() const -> std::optional<TestTextInputState>;
auto dispatchText(const TestTextInputRequest &request)
	-> std::expected<TestInputDisposition, Error>;
auto dispatchEditing(const TestEditingCommandInput &request)
	-> std::expected<TestInputDisposition, Error>;
auto insertText(std::string text, std::optional<TextStoreRange> replacement = {})
	-> std::expected<TestInputDisposition, Error>;
auto setComposition(std::string text, SelectionOffsets selection,
	std::optional<TextStoreRange> replacement = {})
	-> std::expected<TestInputDisposition, Error>;
auto commitComposition() -> std::expected<TestInputDisposition, Error>;
auto cancelComposition() -> std::expected<TestInputDisposition, Error>;
auto editingCommand(EditingCommand command, bool isRepeat = false)
	-> std::expected<TestInputDisposition, Error>;
void pointerDown(PointerInput input);
void pointerUp(PointerInput input);
void click(PointerInput input);
void pointerMove(PointerInput input);
void pointerLeave();
void setWindowActive(bool active);
auto openUrl(const std::string &destination) -> std::expected<void, Error>;
auto semantics() const -> std::shared_ptr<const TestSemanticSnapshot>;
auto dispatchSemantic(const TestSemanticAction &action)
	-> std::expected<TestInputDisposition, Error>;
```

With no supplied platform, allocate a fresh `TestPlatform` per host. Sharing it
between hosts is explicit opt-in clipboard/service sharing. A null test layout
provider selects a built-in deterministic one: 8-point advance per scalar,
20-point line height, and ICU line/grapheme boundaries. Geometry-specific bidi/
font tests supply recorded layouts through `TestTextLayoutProvider`; default test
metrics are not used to assert native font fidelity. Providers return immutable
resources from the test resource subclass, not native resources.

Each injected service error is consumed once. Successful URL calls append exactly
one destination; failed calls append none. Successful semantic publication appends
the complete snapshot; clipboard reads preserve its text. These are explicit test
services, not unsupported native services pretending to work.

Convenience edit methods acquire fresh focus state and create/end one input
sequence. To test composition-consuming Enter/Escape within one action, use the
explicit begin/dispatch/end methods, reusing the sequence and refreshing the target
stamp between callbacks. Return results without also appending those same errors;
only void convenience wrappers append errors they cannot return. Pointer `Point`
convenience overloads remain one-line adapters to `PointerInput` defaults.

- **Current limitation:** headless input only covers basic clicks/focus/activation/scroll.
- **Outcome/API:** extend `TestHost` with `insertText`, `setComposition`,
  `commitComposition`, `cancelComposition`, `editingCommand`, `pointerMove`,
  `pointerLeave`, and `setWindowActive`. Pointer methods accept button/modifiers/
  explicit click count; composition accepts replacement range/provisional selection.
  Include consumed/unhandled results and the same text-store/range/geometry queries
  used by host adapters.
- **Services:** test installation supports deterministic layout, memory clipboard,
  interaction preferences, recording URL service, and injected failures. Existing
  `advanceTime()` drives caret and autoscroll without wall-clock waits.
- **Implementation/edges:** forward to production operations and settle transactions;
  never mutate records, write bindings to simulate input, or duplicate the editing
  command switch. Preserve native-style presentation gates in presentation-testing
  mode. Closing invalidates targets/cancels work. Add seams alongside each feature,
  not after production implementation is finished.
- **Verification:** pair native/headless scenarios for insertion, composition
  consumption, commands, capture, click count, window state, services, scheduled
  work, and stale presentation callbacks. Adapter conversions need separate tests;
  portable injection cannot prove native protocol integration.

### Editor and control state inspection

Public snapshot declarations in core, re-exported through UI/testing:

```cpp
struct EditorSnapshot {
	NodeId id{};
	TextStoreSnapshot store{};
	double horizontalScroll = 0;
	bool canUndo = false;
	bool canRedo = false;
};

// Add to NodeSnapshot; keep existing fields and their meanings.
std::optional<bool> checked{};
bool isHovered = false;
bool hasActiveFocus = false;
std::optional<EditorSnapshot> editor{};
```

`checked` is absent for non-toggles; `editor` is absent for noneditors. Active focus
means logical focus plus active window/input attachment, not whether a focus ring
happens to be painted. Snapshot text for an editor is committed content; the
composed display string is `editor->store.displayed`.

Define the presentation-history payload in core separately from public snapshots:

```cpp
namespace stana::detail {
struct PresentedEditorGeometry {
	TextStoreStamp stamp{};
	std::shared_ptr<const TextLayout> layout{};
	Point contentOrigin{};
	double horizontalScroll = 0;
	Rect clip{}; // Effective intersection of editor and ancestor clips.
};

struct PresentationGeometry {
	std::uint64_t sceneRevision = 0;
	std::map<NodeId, Rect> bounds{};
	std::map<NodeId, PresentedEditorGeometry> editors{};
};
} // namespace stana::detail
```

Store this owning value in the existing bounded presentation history and use it
for confirmation instead of a rectangle-only map. History eviction drops geometry
ownership but cannot invalidate a native scene's independent resource ownership.
Text movement/content changes count as changed interaction geometry even if outer
node bounds are equal; uncertain regions include the old/new editor content clips.

- **Current limitation:** snapshots omit checked, hover, active focus, and editing state.
- **Outcome/API:** add optional checked state, subtree hover, and active focus to
  `NodeSnapshot`; add optional owning `EditorSnapshot` with occurrence identity,
  content revision, committed text/selection, provisional string/replacement range/
  selection/revision, horizontal scroll, and undo/redo availability. Reuse `NodeId`
  for lifetime rather than introduce a second mutable editor handle.
- **Implementation:** publish with scene/semantics at the same commit boundary.
  Keep provisional text distinct from committed node text. Old snapshots are
  immutable values that retain no scopes/native objects. Do not expose mutable
  history, pointers, subscriptions, or retained records. Inspect resolved layouts
  through scene commands' `textLayout` fields and semantic values through
  `TestHost::semantics()`.
- **Edges/dependencies:** reads inside transactions still observe the last complete
  publication. Logical focus can remain true while active focus is false. Hidden
  nodes remain absent from visible snapshots; handles can inspect retained lifetime.
- **Verification:** distinguish preedit/committed text, preserve identity across
  updates/reorders/resizes, restore undo selections, inspect checked state, and keep
  old snapshots after disposal without keeping handles alive. Mount metrics support
  identity assertions but do not replace them.

### Regression and native verification coverage

The mounted-editor regression must include this sequence using the existing test
suite's `require(condition, message)` helper. `host` has mounted/focused an empty
`TextField`, `value` is its string signal, and `submissions` counts its callback:

```cpp
require(host.setComposition("候補", {.anchor = 6, .head = 6}).has_value(), "preedit");
require(value.get().empty(), "provisional text is not the binding value");

const auto sequence = host.beginInput();
const auto before = host.textInputState().value();
require(host.dispatchText({
	.sequence = sequence,
	.target = before.target,
	.edit = CommitComposition{}
}).has_value(), "commit through production dispatch");

const auto committed = host.textInputState().value();
require(host.dispatchEditing({
	.sequence = sequence,
	.target = committed.target,
	.command = EditingCommand::submit
}).has_value(), "same key action is handled");
host.endInput(sequence);

require(value.get() == "候補" && submissions == 0, "IME confirm did not submit");
require(host.editingCommand(EditingCommand::submit).has_value(), "independent submit");
require(submissions == 1, "next Enter submits once");
```

`CommitComposition` is the canonical `stana.text` operation, not a test-only
substitute. Add equivalent scenarios for cancellation, stale store/session input,
callback-driven removal, and service failures before marking editor integration
complete. Keep declaration/consumer smoke cases for every new public aggregate and
property group so documentation API examples stay compilable after implementation.

- **Current limitation:** mounted editors and richer platform/layout behavior have no coverage.
- **Outcome:** provide repeatable portable regressions and explicit current-host
  native/visual gates for every capability, without conflating synthetic evidence
  with operating-system integration. Add coverage alongside each implementation.
- **Portable suites:** extend text contracts and add focused mounted-editor/input,
  layout/scene, and semantic suites through production dispatch. Cover all feature
  verification bullets, including stale identity/ranges, failure and reentrancy.
  Portable suites run without native application/window-server initialization.
- **Adapter suites:** cover range conversion, text-client queries, shortcut/menu
  translation, clipboard, screen coordinates, accessibility notifications, and URL
  results. Fake external state where appropriate, but keep native end-to-end gates
  explicit rather than claiming test services prove them.
- **Native/visual gate:** extend the opt-in graphical test pattern with a small
  fixture for editors, wrapped labels, toggle styles, capped/responsive panels,
  shadows and focus. Verify real IME/dead keys/candidates, commands/clipboard,
  screen-reader actions, long-text scrolling/dragging, resize and backing-scale
  changes. Compare reproducible 1x/2x renders for typography/wrapping/indicators/
  shadows/focus. Record manual IME/screen-reader evidence where reliable automation
  is unavailable; synthetic events are not a substitute.
- **Existing gates:** retain policy, formatting, consumer source/install/relocation,
  reactive/task, presentation-evidence, Debug/Release and sanitizer checks. Register
  new modules/implementation/tests in CMake and existing policy/format workflows.
- **Completion:** portable contract, current adapter, deterministic coverage, and
  named native evidence must all be complete. Missing graphical access leaves an
  explicit outstanding native gate, not a passing or silently skipped capability.

## Existing foundations to reuse

- `TextBuffer`: validated Unicode text, revisioned selection, grapheme boundaries,
  provisional/committed separation, and undo/redo. Extend its range/composition
  operations rather than build another competing text buffer.
- Signals, computed values, transactions, bindings, and scope-owned subscriptions.
- Custom components, conditional composition, keyed `ForEach`, and lifecycle hooks.
- Stacks, boxes, scrolling, reactive styling/visibility, and immutable scene snapshots.
- Programmatic handles, basic focus traversal, and production-backed `TestHost`.
- Presentation evidence and last-confirmed geometry, extended for text hit testing.

`ForEach` already preserves unchanged keyed rows and shared reference identities.
Value-item changes replace descendants, so future editors should use stable row
objects/signals when state must survive unrelated updates. A list-engine rewrite
is not a prerequisite.

## Boundaries and implementation order

Add tests and input/inspection seams in each increment, not as a final phase:

1. Portable services/text layout, deterministic services, callback-safe removal,
   and the snapshot fields needed to verify subsequent work.
2. Mounted editor/composition storage, semantic command/focus coordination, current
   native text client/candidate geometry, caret/selection, clipboard/history, and
   composition-aware submit/cancel.
3. Click-count transport, pointer capture/dragging, hover, eligibility/reentrancy
   regressions, and preserved presentation gates.
4. Shared constraint resolution/horizontal allocation, wrapping/typography, both
   responsive inputs, toggle indicators, shadows, and independent focus painting.
5. Portable semantics/native accessibility and editable/checkbox actions, link/URL
   integration, and closure of native/visual evidence plus existing quality gates.

## Reference contracts

These guide adapter implementation and established interaction models; they are
not portable-engine dependencies or blanket parity commitments.

- [Qt input-method events](https://doc.qt.io/qt-6/qinputmethodevent.html): provisional
  versus committed input, replacement ranges, and provisional selection.
- [Qt input-method queries](https://doc.qt.io/qt-6/qinputmethod.html): surrounding
  text and cursor/anchor geometry updates for the active editor.
- [Apple text-input clients](https://developer.apple.com/documentation/appkit/nstextinputclient):
  protocol duties of the current macOS adapter.
- [Core Text typesetting](https://developer.apple.com/documentation/coretext/cttypesetter):
  native line construction and width-dependent breaks.
