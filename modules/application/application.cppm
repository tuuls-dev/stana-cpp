module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

export module stana:application;
export import :ui;
import :document;
import :rendering_diagnostics;

export namespace stana::application {

/// @brief Describes one native window and its statically typed root component.
///
/// Construct with designated `.title`, `.size`, and `.content` fields, then pass to
/// `Application::run`. CTAD deduces the content type, so unrelated windows can be
/// supplied as a tuple. This value is configuration, not the native window owner;
/// the application session owns the mounted document and native resources.
///
/// @tparam Content The static component type representing the root UI tree.
template <::stana::ui::Component Content> struct Window {
	std::string title{}; ///< Initial native title; owns its UTF-8 characters.
	Size size{};		 ///< Initial content dimensions in logical points, excluding native chrome.
	Content content;	 ///< Root description mounted once when the application starts.
	::stana::rendering::RenderingColorMode renderingColorMode =
		::stana::rendering::RenderingColorMode::automatic;
};

namespace detail {

/// @brief C++ composition-root state passed to the compiled native application adapter.
///
/// The root component description is already mounted and is not retained here.
struct WindowSession {
	std::string title{}; ///< Owning title configuration for native construction.
	Size size{};		 ///< Initial logical viewport matching document construction.
	::stana::rendering::RenderingColorMode renderingColorMode =
		::stana::rendering::RenderingColorMode::automatic;
	std::shared_ptr<::stana::document::detail::Document>
		document{}; ///< Authoritative retained window state.
};

void installNativeTaskDispatcher();
[[nodiscard]] auto runWindows(const std::vector<WindowSession> &sessions) -> int;

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
		std::vector<::stana::application::detail::WindowSession> sessions(1);
		sessions.at(0) = prepare(window);
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
		std::apply(
			[&](const auto &...window) -> void {
				((sessions.at(index++) = prepare(window)), ...);
			},
			windows
		);
		return session.complete(::stana::application::detail::runWindows(sessions));
	}

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
	/// @return A prepared `WindowSession` containing the instantiated document.
	template <::stana::ui::Component Content>
	[[nodiscard]] static auto prepare(const Window<Content> &window) -> detail::WindowSession {
		::stana::application::detail::installNativeTaskDispatcher();
		auto document = ::stana::document::detail::Document::create(window.size);
		::stana::reactive::Transaction::perform([&] -> void {
			::stana::ui::MountContext context{.document = document, .parent = document->root()};
			using ::stana::ui::mount;
			static_cast<void>(mount(window.content, context));
		});
		return {
			.title = window.title,
			.size = window.size,
			.renderingColorMode = window.renderingColorMode,
			.document = std::move(document)
		};
	}
};

} // namespace stana::application
