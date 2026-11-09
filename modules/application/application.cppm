module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

export module stana:application;
export import :ui;
export import :application_window_types;
export import :application_window_requests;
export import :application_window_events;
export import :platform_macos;
export import :application_window_options;
import :document;
import :rendering_diagnostics;

export namespace stana::application {

/// @brief Describes one native window and its statically typed root component.
///
/// Construct with designated `.id`, `.content`, `.options`, and `.events` fields,
/// then pass to `Application::run`. CTAD deduces the content type, so unrelated
/// windows can be supplied as a tuple. This value is configuration, not the native
/// window owner; the application session owns the mounted document and native resources.
///
/// ## Breaking migration from the prior flat fields
/// | Existing field | Replacement |
/// |---|---|
/// | `.title` | `.options.appearance.title` |
/// | `.size` | `.options.geometry.contentSize` |
/// | `.renderingColorMode` | `.options.creation.renderingColorMode` |
/// | `.content` | remains `.content` |
///
/// Do not keep duplicate legacy fields or competing precedence rules. Defaults:
/// content size 800×600 logical points, centered placement, native decorations,
/// system background, opacity `1`, normal state, visible/enabled/focusable.
///
/// @tparam Content The static component type representing the root UI tree.
template <::stana::ui::Component Content> struct Window {
	WindowId id =
		WindowId::create();	 ///< Opaque identity; creating it allocates no native resources.
	Content content;		 ///< Root description mounted once when the application starts.
	WindowOptions options{}; ///< Grouped creation, geometry, appearance, and facilities.
	WindowEvents events{};	 ///< UI-executor observation and close-decision callbacks.
};

namespace detail {

/// @brief C++ composition-root state passed to the compiled native application adapter.
///
/// The root component description is already mounted and is not retained here.
/// Live `options` remain owned by the session so reactive updates can re-sample
/// properties without reconstructing the declaration value.
struct WindowSession {
	WindowId id{};			 ///< Portable identity correlating events and native attachments.
	WindowOptions options{}; ///< Live grouped configuration, including the request stream.
	WindowEvents events{};	 ///< Observation callbacks for this session.
	std::shared_ptr<::stana::document::detail::Document>
		document{}; ///< Authoritative retained window state.
};

void installNativeTaskDispatcher();
[[nodiscard]] auto runWindows(std::vector<WindowSession> &sessions) -> int;

class NativeExecutorSession {
  public:
	NativeExecutorSession();
	NativeExecutorSession(const NativeExecutorSession &) = delete;
	NativeExecutorSession(NativeExecutorSession &&) = delete;
	auto operator=(const NativeExecutorSession &) -> NativeExecutorSession & = delete;
	auto operator=(NativeExecutorSession &&) -> NativeExecutorSession & = delete;
	~NativeExecutorSession();
	[[nodiscard]] auto complete(int code) const -> int;

  private:
	struct State;
	std::unique_ptr<State> state_;
};

} // namespace detail

/// @brief Owns the main-thread native event loop and mounted window sessions.
///
/// A synchronous `main()` calls `run(window)` or `run(tupleOfWindows)`. Each content
/// description mounts once before the native host starts; signal updates mutate
/// that retained document. Merely importing this API does not create `NSApplication`.
/// Windows/Linux can use the portable engine, but native run reports an unsupported
/// platform rather than pretending a window was opened.
class Application {
  public:
	/// @brief Mounts a single window and runs until closure or stop().
	/// @tparam Content The type of the root component.
	/// @param window The window configuration to instantiate.
	/// @return Zero for normal shutdown, nonzero if startup or native execution fails.
	template <::stana::ui::Component Content>
	[[nodiscard]] static auto run(const Window<Content> &window) -> int {
		const ::stana::application::detail::NativeExecutorSession session;
		// Clang 22 restores the end cursor captured by vector::emplace_back, so
		// push_back constructs the session and then drops it. Assign into a sized
		// vector instead. Same constraint as symbol path compilation.
		auto prepared = prepare(window);
		if (!prepared) {
			return session.complete(1);
		}
		std::vector<::stana::application::detail::WindowSession> sessions(1);
		sessions.at(0) = *std::move(prepared);
		return session.complete(::stana::application::detail::runWindows(sessions));
	}

	/// @brief Mounts heterogeneous window descriptions into independent documents.
	/// @tparam Windows Parameter pack of Window types.
	/// @param windows Tuple containing `Window` values; no common content type is required.
	/// @return Zero for normal shutdown, nonzero if startup or native execution fails.
	template <typename... Windows>
	[[nodiscard]] static auto run(const std::tuple<Windows...> &windows) -> int {
		const ::stana::application::detail::NativeExecutorSession session;
		// Same Clang 22 emplace_back constraint as the single-window overload.
		std::vector<::stana::application::detail::WindowSession> sessions(sizeof...(Windows));
		std::size_t index = 0;
		std::optional<Error> failure{};
		std::apply(
			[&](const auto &...window) -> void {
				const auto tryPrepare = [&](const auto &entry) -> void {
					if (failure.has_value()) {
						return;
					}
					auto prepared = prepare(entry);
					if (!prepared) {
						failure = prepared.error();
						return;
					}
					sessions.at(index++) = *std::move(prepared);
				};
				(tryPrepare(window), ...);
			},
			windows
		);
		if (failure.has_value()) {
			return session.complete(1);
		}
		return session.complete(::stana::application::detail::runWindows(sessions));
	}

	/// @brief Returns the currently attached displays in logical points.
	///
	/// Empty before a native session exists or when the platform backend is unsupported.
	/// Screen identities may later become invalid; callers must not assume reuse.
	[[nodiscard]] static auto screens() -> std::vector<Screen>;

	/// @brief Requests native loop shutdown and disposal of all remaining window sessions.
	static void stop();

	/// @brief Returns last-known progress for active windows on the UI executor.
	///
	/// No application is created when called before `run()`; the result is then empty.
	/// @return A vector of `WindowDiagnostics` for all active windows.
	[[nodiscard]] static auto renderingDiagnostics()
		-> std::vector<::stana::rendering::WindowDiagnostics>;

	/// @brief Enqueues work on the UI executor without extending a component scope.
	/// @param delay The time duration to wait before executing the callback.
	/// @param callback The function to execute on the UI thread.
	/// @note Capture state handles by value; use a request token if a component may disappear.
	static void postAfter(std::chrono::milliseconds delay, const std::function<void()> &callback);

  private:
	/// @template Content The root component type.
	/// @param window The window configuration structure.
	/// @return A prepared session, or `stana::Error` when configuration validation fails.
	template <::stana::ui::Component Content>
	[[nodiscard]] static auto prepare(const Window<Content> &window)
		-> std::expected<detail::WindowSession, Error> {
		::stana::application::detail::installNativeTaskDispatcher();
		const auto snapshot = detail::sampleConfiguration(window.id, window.options);
		if (const auto validation = detail::validateConfiguration(snapshot); !validation) {
			return std::unexpected(validation.error());
		}
		const auto contentSize = snapshot.contentSize;
		auto document = ::stana::document::detail::Document::create(contentSize);
		::stana::reactive::Transaction::perform([&] -> void {
			::stana::ui::MountContext context{.document = document, .parent = document->root()};
			using ::stana::ui::mount;
			static_cast<void>(mount(window.content, context));
		});
		return detail::WindowSession{
			.id = window.id,
			.options = window.options,
			.events = window.events,
			.document = std::move(document)
		};
	}
};

} // namespace stana::application
