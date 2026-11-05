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
	RenderingColorMode renderingColorMode = RenderingColorMode::automatic;
};

namespace detail {

/// @brief C++ composition-root state passed to the compiled native application adapter.
///
/// The root component description is already mounted and is not retained here.
struct WindowSession {
	std::string title{}; ///< Owning title configuration for native construction.
	Size size{};		 ///< Initial logical viewport matching document construction.
	RenderingColorMode renderingColorMode = RenderingColorMode::automatic;
	std::shared_ptr<Document> document{}; ///< Authoritative retained window state.
};

void installNativeTaskDispatcher();
[[nodiscard]] auto runWindows(const std::vector<WindowSession> &sessions) -> int;

} // namespace detail

/// @brief Cumulative renderer work and current gauges; drawable payload is an estimate.
struct RenderingWork {
	std::uint64_t tileCount = 0;		///< Current tile-layer count.
	std::uint64_t rasterizedTiles = 0;	///< Cumulative fully cleared/replayed tiles.
	std::uint64_t presentedTiles = 0;	///< Cumulative tiles whose native presentation was issued.
	std::uint64_t rasterizedPixels = 0; ///< Cumulative visible physical pixels replayed.
	std::uint64_t copiedBytes =
		0; ///< Committed buffer-to-drawable pixel payload, excluding row padding.
	std::uint64_t cpuUploadBytes = 0;	 ///< Bitmap upload payload; zero in the shared-buffer path.
	std::uint64_t bufferAllocations = 0; ///< Cumulative successful shared Metal buffer allocations.
	std::uint64_t rasterBytes =
		0; ///< Current plus retired aligned shared-buffer bytes; excludes drawables.
	std::uint64_t peakRasterBytes =
		0; ///< Peak owned shared-buffer bytes during this window lifetime.
	std::uint64_t estimatedDrawableBytes =
		0; ///< Estimated tile pixel payload times configured drawable count, not measured VRAM.
	std::uint64_t fullDamageFallbacks =
		0; ///< Conservative viewport selections for unknown ink bounds.
	std::uint64_t recoveries =
		0; ///< Consumed recovery cycles; successful confirmation resets the allowance.
	std::uint64_t anchorAttempts = 0; ///< Committed paint-equivalent revision evidence anchors.
	bool hasActiveBatch = false; ///< Whether a composite or its GPU drain currently owns admission.
	bool hasPendingScene =
		false; ///< Whether latest document demand differs from confirmed display.
};

/// @brief Last-known rendering progress for one active native window.
///
/// `Application::renderingDiagnostics()` returns these values for debugging and
/// native verification. A submitted scene is not necessarily displayed: only a
/// confirmed revision joins GPU success for every copied tile, an issued batch
/// transaction and a positive native timestamp from that batch's proof tile.
/// Other tile timestamps remain diagnostic, not independent display proof. This is acknowledged
/// last-known geometry, not a promise of scanout-atomic input or a performance qualification.
struct WindowDiagnostics {
	std::string deviceName{};
	bool unifiedMemory = false;
	bool linearWorking = false;
	bool linearOutput = false;
	std::uint32_t meshSampleCount = 1;
	std::uint64_t offscreenRevision = 0;
	std::uint64_t stateSteps = 0;
	std::uint64_t pipelineBuilds = 0;
	std::uint64_t pipelineCacheHits = 0;
	std::uint64_t resourceUploadBytes = 0;
	std::uint64_t compositedTiles = 0;
	std::uint64_t trackedBytes = 0;
	std::uint64_t peakTrackedBytes = 0;
	std::uint64_t managedWriteBytes = 0;
	std::uint64_t surfaceEpoch = 0; ///< Current native geometry/surface generation.
	std::optional<std::uint64_t> submittedSceneRevision{}; ///< Last GPU submission, if any.
	std::optional<std::uint64_t>
		confirmedSceneRevision{}; ///< Last joined display confirmation, if any.
	std::optional<std::string>
		lastFailure{}; ///< Native failure message, absent when none was recorded.
	RenderingWork
		renderingWork{}; ///< Tile-sized copies remain; cpuUploadBytes excludes command values.
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
		return {
			.title = window.title,
			.size = window.size,
			.renderingColorMode = window.renderingColorMode,
			.document = std::move(document)
		};
	}
};

} // namespace stana
