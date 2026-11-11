module;

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

export module stana:application_window_requests;
export import :application_window_types;
import :primitives;

export namespace stana::application {

/// @brief Requests key focus using platform-default activation rules.
struct FocusWindow {
	std::optional<InteractionToken> interaction{}; ///< Required when the host gates focus.
};

/// @brief Requests an explicit high-level window state transition.
struct SetWindowState {
	WindowState state = WindowState::normal; ///< Target portable state.
};

/// @brief Requests window closure; still flows through close-decision callbacks.
struct CloseWindow {};

/// @brief Begins an interactive move with a user-gesture token.
struct BeginInteractiveMove {
	InteractionToken interaction{}; ///< Opaque gesture authorization from a Stana event.
};

/// @brief Edge selected for an interactive resize gesture.
enum class ResizeEdge : std::uint8_t {
	top,
	bottom,
	leading,
	trailing,
	topLeading,
	topTrailing,
	bottomLeading,
	bottomTrailing
};

/// @brief Begins an interactive resize with a user-gesture token.
struct BeginInteractiveResize {
	InteractionToken interaction{}; ///< Opaque gesture authorization from a Stana event.
	ResizeEdge edge = ResizeEdge::bottomTrailing; ///< Edge or corner being dragged.
};

/// @brief Shows the system/window menu at a content-local point.
struct ShowWindowMenu {
	InteractionToken interaction{}; ///< Opaque gesture authorization from a Stana event.
	Point location{};				///< Content-local logical point for menu presentation.
};

/// @brief Pixel format for an owned window capture result.
enum class CapturePixelFormat : std::uint8_t {
	rgba8Unorm ///< Straight-alpha SDR sRGB RGBA8 in top-left row-major order; owned by the result
			   ///< payload.
};

/// @brief Requests an owned raster capture of the window contents.
struct CaptureWindow {
	CapturePixelFormat format = CapturePixelFormat::rgba8Unorm; ///< Output pixel layout.
};

/// @brief Requests the platform print flow for the window contents.
struct PrintWindow {};

/// @brief Completes a previously deferred close request.
///
/// Only `CloseDecision::allow` and `CloseDecision::deny` are valid here.
/// Returning `defer` again is invalid.
struct RespondDeferredClose {
	CloseRequestId request{};					   ///< Identity from the original `CloseRequest`.
	CloseDecision decision = CloseDecision::allow; ///< Final allow/deny decision.
};

/// @brief Discrete window command that must not be coalesced by reactive updates.
using WindowAction = std::variant<
	FocusWindow,
	SetWindowState,
	CloseWindow,
	BeginInteractiveMove,
	BeginInteractiveResize,
	ShowWindowMenu,
	CaptureWindow,
	PrintWindow,
	RespondDeferredClose>;

/// @brief Unique identity for one accepted emission on a request stream.
struct WindowRequestId {
	std::uint64_t value = 0; ///< Never-reused token; zero is empty.
	[[nodiscard]] auto operator<=>(const WindowRequestId &) const = default;
};

/// @brief Why a request emission was rejected before queuing.
enum class WindowRequestError : std::uint8_t {
	notAttached,	///< The stream is not attached to an open window.
	windowClosed,	///< The attached window has already closed.
	invalidAction,	///< The action failed structural validation.
	invalidToken,	///< A required interaction token was missing or stale.
	duplicateStream ///< A second stream attachment was attempted for one window.
};

/// @brief Owned capture pixels returned to the application; no native resources escape.
struct CapturePixels {
	CapturePixelFormat format = CapturePixelFormat::rgba8Unorm; ///< Pixel layout.
	std::uint32_t width = 0;									///< Pixel width.
	std::uint32_t height = 0;									///< Pixel height.
	std::vector<std::uint8_t> bytes{}; ///< Owning tightly packed pixel storage.
};

/// @brief Action-specific success payload for a completed request.
using WindowRequestPayload = std::variant<std::monostate, CapturePixels>;

/// @brief Result delivered for one previously accepted `WindowRequestId`.
struct WindowRequestResult {
	WindowRequestId id{};						   ///< Identity returned by `emit`.
	WindowOutcome outcome = WindowOutcome::failed; ///< Terminal or pending outcome.
	WindowAction action = CloseWindow{};		   ///< Copy of the accepted action.
	WindowRequestPayload payload{};				   ///< Owned action-specific data when present.
};

class WindowRequestStream;

namespace detail {
struct WindowRequestChannel;
[[nodiscard]] auto requestStreamChannel(const WindowRequestStream &stream)
	-> std::shared_ptr<WindowRequestChannel>;
} // namespace detail

/// @brief Attachable handle for ordered discrete window actions.
///
/// A default-constructed stream is inert until produced by `WindowRequestSource::stream`
/// and attached to exactly one window. Multiple attachments are rejected.
class WindowRequestStream {
  public:
	WindowRequestStream() = default;

	[[nodiscard]] auto operator==(const WindowRequestStream &other) const -> bool {
		return channel_ == other.channel_;
	}

	[[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(channel_); }

  private:
	friend class WindowRequestSource;
	friend struct detail::WindowRequestChannel;
	friend auto detail::requestStreamChannel(const WindowRequestStream &stream)
		-> std::shared_ptr<detail::WindowRequestChannel>;

	explicit WindowRequestStream(std::shared_ptr<detail::WindowRequestChannel> channel)
		: channel_(std::move(channel)) {}

	std::shared_ptr<detail::WindowRequestChannel> channel_{};
};

/// @brief Input channel that mints streams and accepts ordered action emissions.
///
/// This is not a native window owner or imperative controller. Emissions before
/// attachment or after closure fail explicitly. Accepted requests are delivered
/// in order; teardown cancels outstanding work.
class WindowRequestSource {
  public:
	WindowRequestSource();
	WindowRequestSource(const WindowRequestSource &) = delete;
	WindowRequestSource(WindowRequestSource &&) noexcept = default;
	auto operator=(const WindowRequestSource &) -> WindowRequestSource & = delete;
	auto operator=(WindowRequestSource &&) noexcept -> WindowRequestSource & = default;
	~WindowRequestSource();

	/// @brief Returns an attachable stream bound to this source's channel.
	[[nodiscard]] auto stream() const -> WindowRequestStream;

	/// @brief Queues one distinct action; identical actions remain distinct requests.
	[[nodiscard]] auto emit(WindowAction action) const
		-> std::expected<WindowRequestId, WindowRequestError>;

  private:
	std::shared_ptr<detail::WindowRequestChannel> channel_{};
};

namespace detail {

/// @brief One accepted request waiting for the owning session to drain it.
struct PendingWindowRequest {
	WindowRequestId id{};				 ///< Identity returned by `emit`.
	WindowAction action = CloseWindow{}; ///< Owned copy of the queued action.
};

/// @brief Shared request channel state implemented out of line.
struct WindowRequestChannel {
	WindowRequestChannel();
	WindowRequestChannel(const WindowRequestChannel &) = delete;
	WindowRequestChannel(WindowRequestChannel &&) = delete;
	auto operator=(const WindowRequestChannel &) -> WindowRequestChannel & = delete;
	auto operator=(WindowRequestChannel &&) -> WindowRequestChannel & = delete;
	~WindowRequestChannel();

	[[nodiscard]] auto emit(WindowAction action)
		-> std::expected<WindowRequestId, WindowRequestError>;

	/// @brief Marks the channel attached to exactly one open window session.
	[[nodiscard]] auto attach() -> std::expected<void, WindowRequestError>;

	/// @brief Marks the channel closed; further emissions fail with `windowClosed`.
	void close();

	/// @brief Installs a producer-safe wakeup; invoked outside the channel mutex.
	void setWakeup(std::function<void()> wakeup);

	/// @brief Moves queued requests out for ordered session dispatch.
	[[nodiscard]] auto drain() -> std::vector<PendingWindowRequest>;

	struct State;
	std::unique_ptr<State> state;
};

/// @brief Returns the shared channel behind a stream handle, or empty when inert.
[[nodiscard]] auto requestStreamChannel(const WindowRequestStream &stream)
	-> std::shared_ptr<WindowRequestChannel>;

/// @brief Attaches a stream's channel to one window session.
[[nodiscard]] auto attachRequestStream(const WindowRequestStream &stream)
	-> std::expected<void, WindowRequestError>;

/// @brief Closes a stream's channel when present.
void closeRequestStream(const WindowRequestStream &stream);

/// @brief Validates a discrete action against structural contracts before queuing.
[[nodiscard]] auto validateWindowAction(const WindowAction &action)
	-> std::expected<void, WindowRequestError>;

} // namespace detail

} // namespace stana::application
