module;

#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

export module stana:application_window_types;
import :primitives;

/// @file
/// @brief Portable window configuration vocabulary for `stana::application`.
///
/// ## Facility inventory (portable core vs platform extension)
///
/// This inventory records application-facing window facilities across macOS,
/// Windows, and Linux window systems. Entries marked **portable** belong to the
/// grouped `WindowOptions` surface. Entries marked **extension** belong to a
/// typed `platform::*::WindowOptions` group. Windows/Linux native backends are
/// not claimed by this issue; mappings document intent and availability only.
///
/// Legend for lifecycle columns:
/// - C: creation-only preference
/// - L: live/reactive setting
/// - A: discrete action (`WindowAction`)
/// - O: observable event/state (`WindowEvent` / `WindowSnapshot`)
///
/// ### Appearance
/// | Facility | Abstraction | macOS | Windows | Linux (Wayland/X11) | C/L/A/O |
/// |---|---|---|---|---|---|
/// | Title | portable `appearance.title` | `NSWindow.title` | `SetWindowText` | xdg-toplevel title / `_NET_WM_NAME` | L, O |
/// | Native chrome | portable `appearance.decorations` | titled/borderless style mask | WS_CAPTION / chrome | SSD vs CSD; compositor may override | C/L, O |
/// | Background | portable `appearance.background` | opaque/clear window + layer | DWM/backdrop brush | surface opaque region | L, O |
/// | Opacity | portable `appearance.opacity` | `alphaValue` | layered alpha | alpha in surface/compositor | L, O |
/// | Theme | portable `appearance.theme` | appearance override | immersive dark mode | portal/settings; often app-side | L, O |
/// | Shadow | portable `appearance.hasShadow` | `hasShadow` | DWM shadow | compositor decoration | L; unsupported when CSD |
/// | Title-bar integration | **extension** macOS | transparent titlebar / full-size content | n/a portable | n/a | C/L |
/// | Material/backdrop | **extension** (future) | vibrancy/NSVisualEffect | Acrylic/Mica | blur protocols vary | L |
///
/// ### Geometry and placement
/// | Facility | Abstraction | macOS | Windows | Linux | C/L/A/O |
/// |---|---|---|---|---|---|
/// | Content size | portable `geometry.contentSize` | content layout size | client rect | window geometry; Wayland may adjust | L, O |
/// | Placement | portable `geometry.placement` | cascade/center/frame origin | `ShowWindow`/placement | xdg positioner / X11 gravity | C/L, O |
/// | Screen selection | portable `Screen`/`ScreenId` | `NSScreen` | monitor handle | wl_output / Xinerama | C/L, O |
/// | Min/max content size | portable constraints | `contentMin/MaxSize` | min/max track | xdg min/max; may be ignored | L, O |
/// | Aspect ratio | portable `geometry.aspectRatio` | content aspect | aspect hints | size hints; compositor override | L, O |
/// | Resize increment | portable `geometry.resizeIncrement` | resize increments | uncommon | size hints | L |
/// | Interactive move/resize | portable actions | performWindowDrag | `WM_SYSCOMMAND` | xdg move/resize | A, O |
///
/// ### State, visibility, and focus
/// | Facility | Abstraction | macOS | Windows | Linux | C/L/A/O |
/// |---|---|---|---|---|---|
/// | Window state | portable `behavior.state` | miniaturize/zoom/fullscreen | min/max/fullscreen | xdg states | L/A, O |
/// | Visibility | portable `behavior.isVisible` | `orderOut`/`makeKeyAndOrderFront` | Show/Hide | map/unmap | L, O |
/// | Enabled | portable `behavior.isEnabled` | ignore mouse + key | `EnableWindow` | input inhibit patterns | L, O |
/// | Accepts focus | portable `behavior.acceptsFocus` | `canBecomeKey` | WS_EX_NOACTIVATE | activation policy | C/L, O |
/// | Activation policy | portable `behavior.activation` | activate vs order front | flash/foreground rules | xdg activation token | C/L/A, O |
/// | Resizable | portable `behavior.isResizable` | style mask | thickframe | resize edges | L, O |
/// | Controls | portable `behavior.controls` | traffic lights / collection | system menu buttons | SSD buttons / CSD | C/L, O |
/// | Pointer policy | portable `behavior.pointerPolicy` | ignoresMouseEvents | WS_EX_TRANSPARENT | input region | L, O |
///
/// ### Desktop integration
/// | Facility | Abstraction | macOS | Windows | Linux | C/L/A/O |
/// |---|---|---|---|---|---|
/// | Level (portable) | `desktop.level` normal/floating | floating panel intent | TOPMOST intent | above layer intent | L, O |
/// | Native levels | **extension** macOS | `NSWindow.Level` | Win32 HWND_z | stacking; limited Wayland | L |
/// | Task switcher | portable `desktop.taskSwitcher` | policy / LSUIElement limits | WS_EX_TOOLWINDOW | skip-taskbar; often global | L; may be unsupported per-window |
/// | Spaces / virtual desktop | **extension** | collectionBehavior / Spaces | virtual desktop APIs | xdg workspace protocols | C/L, O |
/// | Fullscreen Spaces | **extension** macOS | fullscreen behaviors | n/a same form | n/a | C/L, O |
/// | Tabbing | **extension** macOS | `NSWindowTabbing*` | n/a | n/a | C/L, O |
///
/// ### Roles, ownership, modality
/// | Facility | Abstraction | macOS | Windows | Linux | C/L/A/O |
/// |---|---|---|---|---|---|
/// | Standard window | `StandardWindow` | `NSWindow` / `NSPanel` standard | overlapped | toplevel | C |
/// | Dialog | `DialogWindow` owner/modality | sheet/modal session | owner + modal loop | xdg dialog / transient | C, O |
/// | Utility | `UtilityWindow` | utility / nonactivating panel | tool window | utility type | C |
/// | Popup | `PopupWindow` anchor/owner | child window / anchoring | owned popup | xdg popup/positioner | C, O |
/// | Panel behaviors | **extension** macOS | becomesKeyOnlyIfNeeded, worksWhenModal | n/a | n/a | C/L |
///
/// ### Toolbars, documents, drag-and-drop, restoration
/// | Facility | Abstraction | macOS | Windows | Linux | C/L/A/O |
/// |---|---|---|---|---|---|
/// | Toolbar | portable `facilities.toolbar` | `NSToolbar` mapping | toolbar host | app chrome | L, O |
/// | Represented document | portable `facilities.document` | represented file + edited | AppUserModelID / title | portal document state | L, O |
/// | Drag and drop | portable `facilities.dragAndDrop` | NSDragging* | OLE/DragDrop | xdnd / data-device | L/A, O |
/// | State restoration | portable `facilities.restoration` | restorable identifier | session APIs | compositor-dependent | C + hooks |
///
/// ### Capture, printing, menus
/// | Facility | Abstraction | macOS | Windows | Linux | C/L/A/O |
/// |---|---|---|---|---|---|
/// | Window menu | portable action | system window menu | system menu | window menu protocols | A |
/// | Capture | portable action; owns pixels | window snapshot APIs | PrintWindow/DXGI | screencopy; permissioned | A, O |
/// | Printing | portable action | `NSPrintOperation` bridge | print dialog | print portals | A, O |
/// | Close request | portable close + `CloseDecision` | `windowShouldClose` | `WM_CLOSE` | close request | A/O |
///
/// ### Restrictions and verification
/// - Compositors (especially Wayland) may adjust or ignore size, maximization, and
///   placement; outcomes must report `adjusted`, `unsupported`, or `unconfirmed`
///   rather than inventing success.
/// - Invalidated `ScreenId` values must not silently resolve to another display.
/// - Required structural choices such as `WindowRole` must not silently degrade to
///   a different role; emit failure instead.
/// - Target-compatible extensions are not proof a runtime backend exists.
/// - Verify with portable contracts for validation/outcomes and native macOS tests
///   for AppKit-mapped facilities; Windows/Linux remain mapping-only until backends exist.

export namespace stana::application {

/// @brief Opaque, comparable window identity that does not allocate native resources.
///
/// Creating an identity only reserves a process-local token. Duplicate identities
/// across windows prepared for one `Application::run` are rejected before native
/// creation. Zero denotes an empty/unspecified identity.
struct WindowId {
	std::uint64_t value = 0; ///< Never-reused process-local token; zero is empty.

	/// @brief Reserves a fresh identity without creating a native window.
	[[nodiscard]] static auto create() -> WindowId;

	[[nodiscard]] auto operator<=>(const WindowId &) const = default;
};

/// @brief Opaque display identity for explicit placement and observation.
///
/// Identities may become stale when displays disconnect. Consumers must treat
/// invalidated ids as a typed failure, never as an accidental alias of another screen.
struct ScreenId {
	std::uint64_t value = 0; ///< Process-local token; zero is empty/unspecified.
	[[nodiscard]] auto operator<=>(const ScreenId &) const = default;
};

/// @brief Portable description of one attached display in logical points.
///
/// Coordinates use Stana's top-left, y-down convention. Native adapters convert
/// to bottom-left or physical pixel spaces. `backingScale` is observational.
struct Screen {
	ScreenId id{};			///< Stable while the display remains attached.
	std::string name{};		///< Owning UTF-8 label; empty when the host has none.
	Rect bounds{};			///< Full display bounds in global logical points.
	Rect workArea{};		///< Usable area excluding system chrome where known.
	ScaleFactor backingScale{1}; ///< Logical-to-device scale reported by the host.
	bool isPrimary = false; ///< True when this display is the platform primary.
	[[nodiscard]] auto operator<=>(const Screen &) const = default;
};

/// @brief Selects which display participates in placement.
struct AutomaticScreen {
	[[nodiscard]] auto operator<=>(const AutomaticScreen &) const = default;
};

/// @brief Requests the platform primary display explicitly.
struct PrimaryScreen {
	[[nodiscard]] auto operator<=>(const PrimaryScreen &) const = default;
};

/// @brief Requests an exact previously observed display identity.
struct SpecificScreen {
	ScreenId id{}; ///< Must refer to a currently valid `Screen`.
	[[nodiscard]] auto operator<=>(const SpecificScreen &) const = default;
};

/// @brief Typed screen chooser for placement and geometry operations.
using ScreenSelection = std::variant<AutomaticScreen, PrimaryScreen, SpecificScreen>;

/// @brief Default document-style window with no owner relationship.
struct StandardWindow {
	[[nodiscard]] auto operator<=>(const StandardWindow &) const = default;
};

/// @brief Dialog role with optional owner and modality on the alternative itself.
struct DialogWindow {
	std::optional<WindowId> owner{}; ///< Owning parent window when presented as a child dialog.
	bool modal = false; ///< When true, requests application/window modality for this dialog.
	[[nodiscard]] auto operator<=>(const DialogWindow &) const = default;
};

/// @brief Utility/tool-window role with optional owner.
struct UtilityWindow {
	std::optional<WindowId> owner{}; ///< Optional owner for stacking and lifetime coupling.
	[[nodiscard]] auto operator<=>(const UtilityWindow &) const = default;
};

/// @brief Anchoring edge for popup presentation relative to a parent rectangle.
enum class PopupAnchorEdge : std::uint8_t {
	topLeading,	   ///< Above the anchor, leading-aligned.
	top,		   ///< Above the anchor, centered horizontally.
	topTrailing,   ///< Above the anchor, trailing-aligned.
	leading,	   ///< Leading side of the anchor, vertically centered.
	trailing,	   ///< Trailing side of the anchor, vertically centered.
	bottomLeading, ///< Below the anchor, leading-aligned.
	bottom,		   ///< Below the anchor, centered horizontally.
	bottomTrailing ///< Below the anchor, trailing-aligned.
};

/// @brief Popup role that requires an owner and typed anchoring.
struct PopupWindow {
	WindowId owner{}; ///< Required owner; rejected when empty or unknown.
	Rect anchor{};	  ///< Owner-local anchor rectangle in logical points.
	PopupAnchorEdge edge = PopupAnchorEdge::bottomLeading; ///< Preferred popup edge.
	[[nodiscard]] auto operator<=>(const PopupWindow &) const = default;
};

/// @brief Structured window role; ownership/modality/anchoring live on alternatives.
using WindowRole = std::variant<StandardWindow, DialogWindow, UtilityWindow, PopupWindow>;

/// @brief Defers initial placement to the platform cascade/policy.
struct SystemPlacement {
	ScreenSelection screen = AutomaticScreen{}; ///< Optional display hint for system placement.
	[[nodiscard]] auto operator<=>(const SystemPlacement &) const = default;
};

/// @brief Centers the window on the selected display's work area.
struct CenteredPlacement {
	ScreenSelection screen = AutomaticScreen{}; ///< Display used for centering.
	[[nodiscard]] auto operator<=>(const CenteredPlacement &) const = default;
};

/// @brief Places the content top-left at an explicit global logical point.
struct ExplicitPlacement {
	Point origin{}; ///< Global top-left of the content area in logical points.
	ScreenSelection screen = AutomaticScreen{}; ///< Preferred display; origin remains authoritative.
	[[nodiscard]] auto operator<=>(const ExplicitPlacement &) const = default;
};

/// @brief Portable placement preference with typed screen selection.
using WindowPlacement = std::variant<SystemPlacement, CenteredPlacement, ExplicitPlacement>;

/// @brief Requests platform-native window chrome.
struct NativeDecorations {
	[[nodiscard]] auto operator<=>(const NativeDecorations &) const = default;
};

/// @brief Requests a borderless content surface without native title chrome.
struct BorderlessDecorations {
	[[nodiscard]] auto operator<=>(const BorderlessDecorations &) const = default;
};

/// @brief Native versus borderless decoration preference.
using WindowDecorations = std::variant<NativeDecorations, BorderlessDecorations>;

/// @brief Fills the window with the platform default surface background.
struct SystemBackground {
	[[nodiscard]] auto operator<=>(const SystemBackground &) const = default;
};

/// @brief Fills the window with an explicit portable color.
struct SolidBackground {
	Color color = Color::white(); ///< Owning fill color, including opacity.
	[[nodiscard]] auto operator<=>(const SolidBackground &) const = default;
};

/// @brief Requests a transparent window background for custom content.
struct TransparentBackground {
	[[nodiscard]] auto operator<=>(const TransparentBackground &) const = default;
};

/// @brief Structured window background alternatives.
using WindowBackground = std::variant<SystemBackground, SolidBackground, TransparentBackground>;

/// @brief Typed native control affordances; defaults preserve current host behavior.
struct WindowControls {
	bool closes = true;		 ///< Shows/enables the close affordance when supported.
	bool minimizes = true;	 ///< Shows/enables minimize when supported.
	bool maximizes = true;	 ///< Shows/enables zoom/maximize when supported.
	bool fullscreens = true; ///< Shows/enables fullscreen affordance when supported.
	[[nodiscard]] auto operator<=>(const WindowControls &) const = default;
};

/// @brief Portable high-level window state.
enum class WindowState : std::uint8_t {
	normal,		///< Restored non-minimized content state.
	minimized,	///< Iconified / dock-collapsed.
	maximized,	///< Zoomed or maximized within the work area.
	fullscreen	///< Occupies a fullscreen space or exclusive fullscreen.
};

/// @brief Uses the platform default activation policy when ordering front.
struct PlatformDefaultActivation {
	[[nodiscard]] auto operator<=>(const PlatformDefaultActivation &) const = default;
};

/// @brief Presents the window without stealing key/activation focus.
struct PresentWithoutActivation {
	[[nodiscard]] auto operator<=>(const PresentWithoutActivation &) const = default;
};

/// @brief Activation preference for presentation and focus requests.
using WindowActivation = std::variant<PlatformDefaultActivation, PresentWithoutActivation>;

/// @brief Ordinary hit-testing and pointer delivery.
struct NormalPointerPolicy {
	[[nodiscard]] auto operator<=>(const NormalPointerPolicy &) const = default;
};

/// @brief Pointer events pass through the window to windows behind it.
struct PointerPassThrough {
	[[nodiscard]] auto operator<=>(const PointerPassThrough &) const = default;
};

/// @brief Pointer hit-testing policy for the window surface.
using WindowPointerPolicy = std::variant<NormalPointerPolicy, PointerPassThrough>;

/// @brief Portable stacking intent; native-only levels belong in platform extensions.
enum class WindowLevel : std::uint8_t {
	normal,	 ///< Ordinary document/utility stacking.
	floating ///< Floating above normal peers where the platform allows.
};

/// @brief Participation in task switchers / window lists.
enum class TaskSwitcherParticipation : std::uint8_t {
	automatic, ///< Platform default for the window role.
	included,  ///< Prefer inclusion when per-window control exists.
	excluded   ///< Prefer exclusion; report unsupported when not controllable.
};

/// @brief Item shown in a portable toolbar description.
struct WindowToolbarItem {
	std::string identifier{}; ///< Stable item identity within the toolbar.
	std::string label{};	  ///< Owning UTF-8 label.
	bool enabled = true;	  ///< Whether the item accepts activation.
	[[nodiscard]] auto operator<=>(const WindowToolbarItem &) const = default;
};

/// @brief Owned toolbar description mapped by native adapters.
struct WindowToolbar {
	std::string identifier{}; ///< Restoration/identity key for the toolbar configuration.
	std::vector<WindowToolbarItem> items{}; ///< Ordered toolbar items; owns strings.
	bool visible = true; ///< Whether the toolbar chrome is shown.
	[[nodiscard]] auto operator<=>(const WindowToolbar &) const = default;
};

/// @brief Document-window integration: represented URL/path and edited state.
struct WindowDocument {
	std::optional<std::string> representedPath{}; ///< Owning filesystem path or URI when present.
	bool isEdited = false; ///< Marks unsaved edits for native close/proxy affordances.
	[[nodiscard]] auto operator<=>(const WindowDocument &) const = default;
};

/// @brief Permitted drag operations for drop targets.
enum class DragOperation : std::uint8_t {
	none, ///< Reject the drag.
	copy, ///< Copy payload into the destination.
	move, ///< Move payload when the source allows.
	link  ///< Create a link/reference when supported.
};

/// @brief One accepted drag payload format (UTI, MIME type, or portable alias).
struct DragFormat {
	std::string identifier{}; ///< Owning format token interpreted by the native adapter.
	[[nodiscard]] auto operator<=>(const DragFormat &) const = default;
};

/// @brief Snapshot of an inbound drag session without borrowed native objects.
struct DragInfo {
	std::vector<DragFormat> formats{}; ///< Formats offered by the drag source.
	Point location{}; ///< Location in window content coordinates.
	DragOperation proposedOperation = DragOperation::copy; ///< Source-proposed default.
};

/// @brief Typed drag-and-drop acceptance policy and lifecycle handlers.
///
/// Handlers run on the UI executor. They must not retain native drag objects.
struct WindowDragAndDrop {
	std::vector<DragFormat> acceptedFormats{}; ///< Formats this window can accept.
	DragOperation permittedOperations = DragOperation::copy; ///< Operations the target allows.
	std::function<DragOperation(const DragInfo &)> onEnter{}; ///< Updates the proposed operation.
	std::function<DragOperation(const DragInfo &)> onMove{};  ///< Called as the drag moves.
	std::function<void()> onLeave{};						  ///< Clears transient drag feedback.
	std::function<bool(const DragInfo &)> onDrop{}; ///< Returns true when the drop is accepted.
};

/// @brief Creation-only restoration identity; changing it after creation is invalid.
struct WindowRestorationIdentity {
	std::string key{}; ///< Owning opaque restoration key interpreted by the host.
	[[nodiscard]] auto operator<=>(const WindowRestorationIdentity &) const = default;
};

/// @brief Whether automatic interface restoration is requested.
enum class WindowRestorationPolicy : std::uint8_t {
	automatic, ///< Participate in platform restoration when available.
	disabled   ///< Do not restorable-encode this window.
};

/// @brief Restoration identity, policy, and save/restore hooks.
///
/// `identity` is creation-only. Hooks run on the UI executor and own their payloads.
struct WindowRestoration {
	WindowRestorationIdentity identity{}; ///< Creation-only key; empty disables identity.
	WindowRestorationPolicy policy = WindowRestorationPolicy::automatic; ///< Participation policy.
	std::function<std::vector<std::uint8_t>()> onSave{}; ///< Returns owned opaque state bytes.
	std::function<void(std::vector<std::uint8_t>)> onRestore{}; ///< Consumes owned opaque bytes.
};

/// @brief Outcome of a configuration change or discrete request.
enum class WindowOutcome : std::uint8_t {
	applied,	 ///< The host confirmed the requested effect.
	pending,	 ///< Accepted and waiting for asynchronous confirmation.
	adjusted,	 ///< Applied with host-modified values; see effective payload.
	unsupported, ///< Preference retained current/system behavior; not a hard failure.
	invalid,	 ///< Rejected validation; previous configuration remains intact.
	denied,		 ///< Rejected by policy, security, or user decision.
	failed,		 ///< Native failure while attempting the change.
	cancelled,	 ///< Cancelled during teardown or superseded work.
	unconfirmed	 ///< No confirmation channel exists; success was not invented.
};

/// @brief Identifiers for capability and configuration reporting.
enum class WindowFeature : std::uint16_t {
	role,
	renderingColorMode,
	contentSize,
	placement,
	minimumContentSize,
	maximumContentSize,
	aspectRatio,
	resizeIncrement,
	title,
	decorations,
	background,
	opacity,
	theme,
	hasShadow,
	isResizable,
	controls,
	state,
	isVisible,
	isEnabled,
	acceptsFocus,
	activation,
	pointerPolicy,
	level,
	taskSwitcher,
	toolbar,
	document,
	dragAndDrop,
	restoration,
	focus,
	close,
	interactiveMove,
	interactiveResize,
	windowMenu,
	capture,
	printing,
	titleBarIntegration,
	tabbing,
	spaces,
	panelBehavior,
	nativeLevel
};

/// @brief Whether a feature can be satisfied on the current host.
enum class FeatureSupport : std::uint8_t {
	supported,	 ///< Fully available for the current role/backend.
	partial,	 ///< Available with documented limitations.
	unsupported	 ///< Not available; preferences report unsupported outcomes.
};

/// @brief Whether a feature is fixed at creation or mutable afterward.
enum class FeatureMutability : std::uint8_t {
	creationOnly, ///< Changing after native creation is invalid.
	runtime		  ///< May be updated through properties or actions.
};

/// @brief One capability row for `WindowCapabilities`.
struct WindowFeatureCapability {
	WindowFeature feature = WindowFeature::contentSize; ///< Feature being described.
	FeatureSupport support = FeatureSupport::supported; ///< Host support classification.
	FeatureMutability mutability = FeatureMutability::runtime; ///< Creation-only vs live.
	[[nodiscard]] auto operator<=>(const WindowFeatureCapability &) const = default;
};

/// @brief Opaque authorization for actions that require a user gesture.
///
/// Tokens are supplied by Stana events. Raw native event identifiers never appear
/// in the public API. Empty tokens are rejected for gesture-gated actions.
struct InteractionToken {
	std::uint64_t value = 0; ///< Process-local token; zero is empty.
	[[nodiscard]] auto operator<=>(const InteractionToken &) const = default;
};

/// @brief Application response to a close request.
enum class CloseDecision : std::uint8_t {
	allow, ///< Proceed with closure.
	deny,  ///< Keep the window open.
	defer  ///< Keep open until a later typed deferred-close response.
};

/// @brief Identity of one outstanding close request, including deferred decisions.
struct CloseRequestId {
	std::uint64_t value = 0; ///< Never-reused token; zero is empty.
	[[nodiscard]] auto operator<=>(const CloseRequestId &) const = default;
};

/// @brief Close attempt delivered to `WindowEvents::onCloseRequested`.
///
/// Callbacks run on the UI executor. Returning `defer` requires a later
/// `RespondDeferredClose` action that references `id`.
struct CloseRequest {
	CloseRequestId id{}; ///< Identity for deferred responses.
	WindowId window{};	 ///< Window that received the close attempt.
};

} // namespace stana::application
