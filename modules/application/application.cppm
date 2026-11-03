module;

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

export module stana.application;
export import stana.ui;
import stana.core;

export namespace stana {

/// @brief Describes one native window and its statically typed root component.
///
/// Construct with designated `.title`, `.size`, and `.content` fields, then pass to
/// `Application::run`. CTAD deduces the content type, so unrelated windows can be
/// supplied as a tuple. This value is configuration, not the native window owner;
/// the application session owns the mounted document and native resources.
///
/// @tparam Content The static component type representing the root UI tree.
template <Component Content> struct Window {
	std::string title{}; ///< Initial native title; owns its UTF-8 characters.
	Size size{};		 ///< Initial content dimensions in logical points, excluding native chrome.
	Content content;	 ///< Root description mounted once when the application starts.
};

namespace detail {

/// @brief C++ composition-root state passed to the compiled native application adapter.
///
/// The root component description is already mounted and is not retained here.
struct WindowSession {
	std::string title{}; ///< Owning title configuration for native construction.
	Size size{};		 ///< Initial logical viewport matching document construction.
	std::shared_ptr<Document> document{}; ///< Authoritative retained window state.
};

void installNativeTaskDispatcher();
[[nodiscard]] auto runWindows(const std::vector<WindowSession> &sessions) -> int;

} // namespace detail

/// @brief Last-known rendering progress for one active native window.
///
/// `Application::renderingDiagnostics()` returns these values for debugging and
/// native verification. A submitted scene is not necessarily displayed: only a
/// confirmed revision has both successful GPU completion and a positive native
/// drawable timestamp. This is acknowledged last-known geometry, not a promise
/// of scanout-atomic input or a performance qualification.
struct WindowDiagnostics {
	std::uint64_t surfaceEpoch = 0; ///< Current native geometry/surface generation.
	std::optional<std::uint64_t> submittedSceneRevision{}; ///< Last GPU submission, if any.
	std::optional<std::uint64_t>
		confirmedSceneRevision{}; ///< Last joined display confirmation, if any.
	std::optional<std::string>
		lastFailure{}; ///< Native failure message, absent when none was recorded.
};

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
	template <Component Content>
	[[nodiscard]] static auto run(const Window<Content> &window) -> int {
		std::vector<detail::WindowSession> sessions;
		sessions.push_back(prepare(window));
		return detail::runWindows(sessions);
	}

	/// @brief Mounts heterogeneous window descriptions into independent documents.
	/// @tparam Windows Parameter pack of Window types.
	/// @param windows Tuple containing `Window` values; no common content type is required.
	/// @return Zero for normal shutdown, nonzero if startup or native execution fails.
	template <typename... Windows>
	[[nodiscard]] static auto run(const std::tuple<Windows...> &windows) -> int {
		std::vector<detail::WindowSession> sessions;
		std::apply(
			[&](const auto &...window) -> void {
				(sessions.push_back(prepare(window)), ...);
			},
			windows
		);
		return detail::runWindows(sessions);
	}

	/// @brief Requests native loop shutdown and disposal of all remaining window sessions.
	static void stop();

	/// @brief Returns last-known progress for active windows on the UI executor.
	///
	/// No application is created when called before `run()`; the result is then empty.
	/// @return A vector of `WindowDiagnostics` for all active windows.
	[[nodiscard]] static auto renderingDiagnostics() -> std::vector<WindowDiagnostics>;

	/// @brief Enqueues work on the UI executor without extending a component scope.
	/// @param delay The time duration to wait before executing the callback.
	/// @param callback The function to execute on the UI thread.
	/// @note Capture state handles by value; use a request token if a component may disappear.
	static void postAfter(std::chrono::milliseconds delay, std::function<void()> callback);

  private:
	/// @template Content The root component type.
	/// @param window The window configuration structure.
	/// @return A prepared `WindowSession` containing the instantiated document.
	template <Component Content>
	[[nodiscard]] static auto prepare(const Window<Content> &window) -> detail::WindowSession {
		detail::installNativeTaskDispatcher();
		auto document = detail::Document::create(window.size);
		Transaction::perform([&] -> void {
			MountContext context{.document = document, .parent = document->root()};
			static_cast<void>(mount(window.content, context));
		});
		return {.title = window.title, .size = window.size, .document = std::move(document)};
	}
};

} // namespace stana
