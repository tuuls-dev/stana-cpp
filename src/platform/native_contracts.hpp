#ifndef STANA_NATIVE_CONTRACTS_HPP
#define STANA_NATIVE_CONTRACTS_HPP

#include "gpu_contracts.hpp"
#include <chrono>
#include <compare>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/// @brief Private module-free transport shared by C++ engine adapters and native code.
/// These types are not installed as public headers and carry no borrowed Node data.
namespace stana::native {

/// @brief Native adapter's paint opcode; converted explicitly from portable PaintKind.
enum class PaintOperation : std::uint8_t {
	fill,	  ///< Filled rounded rectangle.
	stroke,	  ///< Outlined rounded rectangle.
	text,	  ///< Core Text label.
	line,	  ///< Line from rectangle origin to its opposite corner.
	pushClip, ///< Save state and intersect rounded clipping bounds.
	popClip	  ///< Restore the previous saved state.
};

/// @brief Owning typed command crossing the module/Objective-C++ boundary.
/// The C++ application adapter produces this value; the native raster pass reads it.
struct Command {
	PaintOperation operation = PaintOperation::fill; ///< Paint opcode.
	double x = 0;		   ///< Document-relative left coordinate in logical points.
	double y = 0;		   ///< Document-relative top coordinate in logical points.
	double width = 0;	   ///< Logical horizontal extent.
	double height = 0;	   ///< Logical vertical extent.
	double red = 0;		   ///< Red component in encoded sRGB.
	double green = 0;	   ///< Green component in encoded sRGB.
	double blue = 0;	   ///< Blue component in encoded sRGB.
	double alpha = 1;	   ///< Opacity applied by scene projection.
	double radius = 0;	   ///< Rounding radius for rectangles/clips.
	double lineWidth = 1;  ///< Outline thickness in logical points.
	std::string text{};	   ///< Owned UTF-8 label, including embedded NUL bytes.
	double fontSize = 0;   ///< Resolved, scaled font size in logical points.
	double fontWeight = 0; ///< Native system-font weight request.
	auto operator<=>(const Command &) const = default;

	[[nodiscard]] auto operator==(const Command &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Owning standard-library-only geometry at the native boundary.
struct LogicalPoint {
	double x = 0;
	double y = 0;
	auto operator<=>(const LogicalPoint &) const = default;
};

struct LogicalSize {
	double width = 0;
	double height = 0;
	auto operator<=>(const LogicalSize &) const = default;
};

struct LogicalRect {
	LogicalPoint origin{};
	LogicalSize size{};
	auto operator<=>(const LogicalRect &) const = default;
};

struct SceneColor {
	double red = 0;
	double green = 0;
	double blue = 0;
	double alpha = 1;
	auto operator<=>(const SceneColor &) const = default;
};

struct NodeIdentity {
	std::uint64_t document = 0;
	std::uint64_t serial = 0;
	auto operator<=>(const NodeIdentity &) const = default;
};

struct GPUCanvasLeaf {
	NodeIdentity instance;
	LogicalRect bounds;
	std::shared_ptr<const gpu::Drawing> drawing;
	double opacity = 1;
};

/// @brief Local immutable transport, reused independently of ancestor branch copies.
struct PaintSegments {
	std::uint64_t identity = 0;
	std::vector<Command> prefix;
	std::vector<Command> suffix;
	std::optional<GPUCanvasLeaf> gpu;
	std::optional<LogicalRect> childClipBounds;
};

struct SceneBranch {
	NodeIdentity id{};
	std::uint64_t generation = 0;
	bool visible = true;
	std::shared_ptr<const PaintSegments> paint;
	std::vector<std::shared_ptr<const SceneBranch>> children;
};

/// @brief Complete owning production tree; never forces public flat inspection.
struct Scene {
	std::uint64_t revision = 0;
	LogicalSize size{};
	SceneColor background{};
	std::uint64_t textEnvironment = 0;
	std::shared_ptr<const SceneBranch> root;
};

using SceneResult = std::expected<std::shared_ptr<const Scene>, std::string>;

struct TileId {
	std::uint32_t value = 0;
	auto operator<=>(const TileId &) const = default;
};

struct TilePresentation {
	TileId tile{};
	double timestamp = 0;
};

struct PresentationRequest {
	std::uint64_t epoch = 0;
	std::uint64_t sceneRevision = 0;
	std::vector<TileId>
		expectedTiles; ///< Required proof set, distinct from the complete native copy set.
	// Native transaction-plus-anchor mode declares the first submitted tile only.
	// Portable callers retain strict all-declared-tiles evidence semantics.
};

/// @brief Cumulative native work and current allocation/queue gauges; excludes compositor traffic.
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
/// @brief Native keyboard actions understood by the retained document adapter.
enum class KeyAction : std::uint8_t {
	tabForward,	 ///< Move focus to the next eligible occurrence.
	tabBackward, ///< Move focus to the previous eligible occurrence.
	activate	 ///< Invoke the currently focused control.
};

/// @brief Module-free copy of an exact portable presentation identity, never a scene ID alone.
struct FrameIdentity {
	std::uint64_t epoch = 0;		 ///< Changes on surface geometry or drawable-queue replacement.
	std::uint64_t sequence = 0;		 ///< Never-reused submission identity, including retries.
	std::uint64_t sceneRevision = 0; ///< Owning document's submitted scene revision.
};

/// @brief Callbacks installed by one C++ window session and invoked on the main thread.
/// Native hosts own this value until closure. Callback captures must not borrow
/// component descriptions, because descriptions are already destroyed after mounting.
struct Window {
	std::string title{}; ///< Initial UTF-8 native title.
	double width = 0;	 ///< Initial content width in logical points.
	double height = 0;	 ///< Initial content height in logical points.
	gpu::ColorMode colorMode = gpu::ColorMode::automatic;
	std::function<void(gpu::Diagnostic)> gpuError{};
	std::function<SceneResult()> scene{};		  ///< Produces an owning current scene.
	std::function<void(double, double)> resize{}; ///< Reflows the retained viewport.
	std::function<void(bool isDark)>
		appearance{}; ///< Reports whether the current system appearance is dark.
	std::function<void(double, double)> pointerDown{}; ///< Dispatches a logical-coordinate press.
	std::function<void(double, double)> pointerUp{};   ///< Dispatches logical-coordinate release.
	std::function<void(double, double, double)> scroll{}; ///< Dispatches vertical scrolling.
	std::function<void(KeyAction)> key{}; ///< Invokes portable focus/activation behavior.
	std::function<std::expected<FrameIdentity, std::string>(const PresentationRequest &)>
		beginPresentation{}; ///< Registers exact evidence identity before GPU submission.
	std::function<bool(FrameIdentity, bool)>
		gpuCompleted{}; ///< Returns true only when this observation newly confirms geometry.
	std::function<bool(FrameIdentity, TilePresentation)>
		drawablePresented{}; ///< Joins timestamp evidence through the same tracker as tests.
	std::function<void(FrameIdentity)>
		abandonPresentation{}; ///< Retires expired work without inventing presentation evidence.
	std::function<void(std::uint64_t)>
		resetPresentation{}; ///< Invalidates callbacks from previous native attachments/surfaces.
	std::function<void(std::uint64_t)> holdPreparedGeometry{};
	std::function<void(FrameIdentity)>
		gatePendingPresentation{};		 ///< Gates changed geometry before commit.
	std::function<void()> gateSurface{}; ///< Immediately gates replaced native attachments.
	std::function<void()> close{};		 ///< Disposes the document before native state is released.
	std::function<void(std::function<void()>)>
		ready{}; ///< Connects document publication to repaint demand.
};

/// @brief Last-known native progress for one active host; no mutable native objects escape.
struct WindowStatistics {
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
	LogicalSize logicalViewport{};	///< Actual attached view size, not a requested window size.
	double backingScale = 0;		///< Actual surface scale used by grid construction.
	std::uint64_t surfaceEpoch = 0; ///< Generation of the current surface geometry.
	std::uint64_t submittedRevision = 0; ///< Last scene sent to a GPU command buffer.
	std::uint64_t confirmedRevision =
		0;						   ///< Last scene with joined GPU and positive timestamp evidence.
	std::string failure{};		   ///< Terminal native failure, or empty when none was recorded.
	RenderingWork renderingWork{}; ///< Actual shared raster and tile presentation work.
	std::vector<NodeIdentity>
		preparedBranches{}; ///< Bounded last-build fixture record of actual preparation.
	std::vector<std::uint64_t>
		preparedPaints{}; ///< Actual local resources prepared in the last build.
	std::vector<TileId>
		drawableAcquisitions{}; ///< Actual successful acquisitions for the last draft.
};

/// @brief Returns UI-thread snapshots of active native hosts without creating an application.
[[nodiscard]] auto diagnostics() -> std::vector<WindowStatistics>;

/// @brief Runs native windows until the final window closes or stop() is called.
/// @return Exit status, or a diagnostic if required native rendering is unavailable.
[[nodiscard]] auto run(std::vector<Window> windows) -> std::expected<int, std::string>;
/// @brief Requests event-loop shutdown without creating NSApplication on its own.
void stop();
/// @brief Schedules an owning callback on the UI queue after a nonnegative delay.
void postAfter(std::chrono::milliseconds delay, std::function<void()> callback);

/// @brief Native system-font parameters, grouped to prevent swapping size and weight.
struct FontRequest {
	double size = 0;   ///< Logical point size after environment scaling.
	double weight = 0; ///< Native system font weight, zero for regular.
};

/// @brief Measures UTF-8 text with the same Core Text font contract used for drawing.
/// @return Logical width and line height, constrained by the requested maximum width.
[[nodiscard]] auto measureText(const std::string &text, FontRequest font, double maximumWidth)
	-> std::pair<double, double>;
} // namespace stana::native

#endif // STANA_NATIVE_CONTRACTS_HPP
