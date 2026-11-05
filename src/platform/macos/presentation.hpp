#ifndef STANA_MACOS_PRESENTATION_HPP
#define STANA_MACOS_PRESENTATION_HPP
#include "../scheduler.hpp"
#include "compositor.hpp"
#include "renderer.hpp"
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include <functional>
#include <optional>
#include <string>

namespace stana::platform::detail {
/// @brief Independent GPU-read owner. Deliberately does not own its command buffer.
struct BatchResources {
	std::vector<std::shared_ptr<RasterTile>> sources;
	std::shared_ptr<const PreparedScene> target;
	std::shared_ptr<const gpu::CompositePacket> composite;
};

struct ActiveBatch {
	FrameIdentity attempt;
	std::uint64_t surfaceEpoch = 0;
	std::uint64_t attachmentGeneration = 0;
	std::shared_ptr<const PreparedScene> target;
	std::shared_ptr<const gpu::CompositePacket> composite;
	std::vector<TileId>
		submittedTiles; ///< Complete copy/present set, not a claim of per-tile display proof.
	double headroom = 1;
	TileId proofTile{}; ///< User-approved transaction anchor: first actual submitted tile.
	bool scheduledPresentationIssued = false;
	std::optional<double>
		proofPresentedTime{}; ///< First proof-tile callback; 0 is unknown, not display.
};

/// @brief UI-confined native state. Asynchronous observers hold weak references only.
struct WindowState {
	Window callbacks;
	id<MTLDevice> device = nil;
	id<MTLCommandQueue> queue = nil;
	std::shared_ptr<gpu::Compositor> compositor;
	std::shared_ptr<const gpu::CompositePacket> confirmedPacket;
	std::shared_ptr<const gpu::CompositePacket> pendingPacket;
	std::shared_ptr<const PreparedScene> preparingScene;
	bool hybridSurface = false;
	bool linearOutput = false;
	double confirmedHeadroom = 1;
	bool offscreenRecoveryUsed = false;
	bool offscreenDeadlineArmed = false;
	bool offscreenTimedOut = false;
	std::uint64_t offscreenGeneration = 0;
	CALayer *layer = nil;
	__weak NSView *view = nil;
	std::vector<CAMetalLayer *> tileLayers;
	TileGrid grid;
	double scale = 0;
	LogicalSize logicalSize{};
	std::vector<std::shared_ptr<RasterTile>> tiles;
	std::shared_ptr<RasterAccounting> accounting = std::make_shared<RasterAccounting>();
	std::shared_ptr<const PreparedScene> rasterBaseline;
	std::shared_ptr<const PreparedScene> confirmedComposite;
	std::shared_ptr<const PreparedScene> latestRequested;
	std::optional<ActiveBatch> active;
	FrameScheduler scheduler;
	RenderingWork work;
	PreparationTrace preparationTrace;
	std::vector<TileId> drawableAcquisitions;
	std::uint64_t timerGeneration = 0;
	std::uint64_t presentationEpoch = 0;
	std::uint64_t surfaceEpoch = 1;
	std::uint64_t attachmentGeneration = 0;
	std::uint64_t submittedRevision = 0;
	std::uint64_t confirmedRevision = 0;
	bool traceRendering = false;
	bool isFrameQueued = false;
	bool isClosed = false;
	std::string failureMessage;

	[[nodiscard]] auto allowsPointer() const -> bool { return !isClosed && confirmedRevision != 0; }
};

void rendererFailure(const std::shared_ptr<WindowState> &, const gpu::Failure &);
void requestFrame(const std::shared_ptr<WindowState> &);
/// @brief Invalidates input/evidence before draining and replacing an entire attachment.
void installSurface(const std::shared_ptr<WindowState> &);
void closeRenderer(const std::shared_ptr<WindowState> &);
} // namespace stana::platform::detail
#endif
