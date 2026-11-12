module;

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

export module stana:application_window_events;
export import :application_window_types;
export import :application_window_requests;
import :primitives;

export namespace stana::application {

/// @brief Observed open/closed status for a window session.
enum class WindowLifecycle : std::uint8_t {
	open,  ///< Native window exists and is owned by the session.
	closed ///< Window has closed; identities remain for event correlation only.
};

/// @brief Actual observed window state; never merely an echo of requested values.
///
/// Observations are produced on the UI executor. Geometry uses logical points in
/// Stana's top-left, y-down convention after native conversion.
struct WindowSnapshot {
	WindowId id{}; ///< Window identity assigned at declaration time.
	WindowLifecycle lifecycle = WindowLifecycle::closed; ///< Open versus closed.
	Rect contentBounds{};			  ///< Global content bounds excluding native chrome when known.
	Size contentSize{};				  ///< Actual content dimensions in logical points.
	std::optional<ScreenId> screen{}; ///< Current display; absent when unknown/invalidated.
	WindowState state = WindowState::normal; ///< Actual high-level state.
	bool isVisible = false;					 ///< Actual visibility, not the requested property.
	bool isKey = false;						 ///< Whether the window currently accepts key focus.
	bool isActive = false;					 ///< Whether the window is active in the desktop sense.
	ScaleFactor backingScale{1};			 ///< Actual surface scale used by the host.
	[[nodiscard]] auto operator<=>(const WindowSnapshot &) const = default;
};

/// @brief Capability matrix reported after readiness and when host support changes.
struct WindowCapabilities {
	std::vector<WindowFeatureCapability> features{}; ///< Typed feature rows for this window.
};

/// @brief Monotonic configuration transaction identity for correlation.
struct ConfigurationRevision {
	std::uint64_t value = 0; ///< Never-reused revision token; zero is empty.
	[[nodiscard]] auto operator<=>(const ConfigurationRevision &) const = default;
};

/// @brief Effective value accompanying an adjusted/applied configuration outcome.
using EffectiveFeatureValue = std::variant<
	std::monostate,
	Size,
	WindowPlacement,
	std::optional<Size>,
	std::optional<double>,
	std::string,
	WindowDecorations,
	WindowBackground,
	double,
	std::optional<Theme>,
	bool,
	WindowControls,
	WindowState,
	WindowActivation,
	WindowPointerPolicy,
	WindowLevel,
	TaskSwitcherParticipation,
	std::optional<WindowToolbar>,
	std::optional<WindowDocument>>;

/// @brief Per-feature outcome inside one configuration transaction.
struct WindowFeatureResult {
	WindowFeature feature = WindowFeature::contentSize; ///< Feature addressed by this row.
	WindowOutcome outcome = WindowOutcome::failed;		///< Outcome for this feature.
	EffectiveFeatureValue effective{}; ///< Host-effective value when applied/adjusted.
};

/// @brief Aggregated result of applying a configuration transaction.
struct WindowConfigurationResult {
	ConfigurationRevision revision{};			///< Identity of the applied transaction attempt.
	std::vector<WindowFeature> changed{};		///< Features touched by the transaction.
	std::vector<WindowFeatureResult> results{}; ///< Per-feature outcomes and effective values.
};

/// @brief Native window finished construction and published initial observation.
struct WindowReady {
	WindowSnapshot snapshot{};		   ///< Actual initial state.
	WindowCapabilities capabilities{}; ///< Initial capability matrix.
};

/// @brief Actual state changed due to user, host, or accepted configuration/action.
struct WindowStateChanged {
	WindowSnapshot snapshot{}; ///< Latest authoritative observation.
};

/// @brief Host capability matrix changed after readiness.
struct WindowCapabilitiesChanged {
	WindowCapabilities capabilities{}; ///< Updated capability rows.
};

/// @brief Configuration transaction completed with typed per-feature outcomes.
struct WindowConfigurationCompleted {
	WindowConfigurationResult result{}; ///< Owning transaction result.
};

/// @brief Discrete request reached a terminal or pending-reported outcome.
struct WindowRequestCompleted {
	WindowRequestResult result{}; ///< Owning request result payload.
};

/// @brief Facility-specific observation that does not fit a dedicated alternative yet.
struct WindowFacilityEvent {
	WindowFeature feature = WindowFeature::toolbar; ///< Facility that produced the event.
	std::string message{}; ///< Owning diagnostic/detail string; never a native object.
};

/// @brief Window reached closed lifecycle; outstanding requests are cancelled.
struct WindowClosed {
	WindowId id{}; ///< Identity of the closed window.
};

/// @brief Phase of a window-scoped pointer interaction.
enum class WindowInteractionPhase : std::uint8_t { press, move, release };

/// @brief Content-local pointer event with single-use gesture authorization.
/// Tokens expire on release or closure and cannot authorize another window.
struct WindowInteraction {
	WindowId window{};											 ///< Originating window.
	WindowInteractionPhase phase = WindowInteractionPhase::move; ///< Pointer phase.
	Point location{};											 ///< Content-local logical point.
	std::optional<InteractionToken> token{};					 ///< Present on press only.
};

/// @brief Typed window observation delivered on the UI executor.
using WindowEvent = std::variant<
	WindowReady,
	WindowStateChanged,
	WindowCapabilitiesChanged,
	WindowConfigurationCompleted,
	WindowRequestCompleted,
	WindowFacilityEvent,
	WindowInteraction,
	WindowClosed>;

/// @brief Observation callbacks for one window; invoked on the UI executor.
///
/// `onCloseRequested` may return allow/deny/defer. Deferred identities must later
/// be completed through `RespondDeferredClose`. Observations report actual state.
struct WindowEvents {
	std::function<void(const WindowEvent &)> onEvent{}; ///< General typed event sink.
	std::function<CloseDecision(const CloseRequest &)> onCloseRequested{}; ///< Close gate.
};

} // namespace stana::application
