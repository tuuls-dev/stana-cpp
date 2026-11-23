#include "presentation.hpp"
#include "../../diagnostics/log.hpp"
#import <QuartzCore/CATransaction.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace stana::platform::detail {
namespace {
using stana::diagnostics::detail::Field;
using stana::diagnostics::detail::Level;
using stana::diagnostics::detail::log;

[[nodiscard]] auto now() -> FrameScheduler::Time {
	return std::chrono::duration_cast<FrameScheduler::Time>(
		std::chrono::steady_clock::now().time_since_epoch()
	);
}

void advance(std::uint64_t &identity) {
	if (identity == std::numeric_limits<std::uint64_t>::max()) {
		throw std::overflow_error("Native generation exhausted");
	}
	++identity;
}

[[nodiscard]] auto visible(const WindowState &state) -> bool {
	NSWindow *const window = state.view.window;
	return window != nil && window.isVisible &&
		(window.occlusionState & NSWindowOcclusionStateVisible) != 0;
}

void fail(const std::shared_ptr<WindowState> &state, const std::string &message) {
	state->failureMessage = message;
	state->callbacks.gateSurface();
	state->scheduler.close();
	stop();
}

void replaceGrid(const std::shared_ptr<WindowState> &state, bool preserveResources);
void processTransition(const std::shared_ptr<WindowState> &, FramePhase before, FrameAction);
void noteLiveResizeBody(const std::shared_ptr<WindowState> &state);

void arm(const std::shared_ptr<WindowState> &state, std::chrono::milliseconds delay) {
	advance(state->timerGeneration);
	const auto token = state->timerGeneration;
	const auto weak = std::weak_ptr{state};
	postAfter(delay, [weak, token] -> void {
		if (const auto current = weak.lock();
			current && !current->isClosed && current->timerGeneration == token) {
			const auto before = current->scheduler.phase();
			const auto action = current->scheduler.expire(now());
			processTransition(current, before, action);
		}
	});
}

void armNextWake(const std::shared_ptr<WindowState> &state) {
	if (const auto delay = state->scheduler.wakeDelay(now())) {
		arm(state, *delay);
	}
}

void invalidateEvidence(const std::shared_ptr<WindowState> &state) {
	if (state->active) {
		state->callbacks.abandonPresentation(state->active->attempt);
	}
	advance(state->presentationEpoch);
	advance(state->attachmentGeneration);
	advance(state->timerGeneration);
	state->callbacks.resetPresentation(state->presentationEpoch);
	state->callbacks.gateSurface();
	state->confirmedComposite.reset();
	state->confirmedPacket.reset();
	state->pendingPacket.reset();
	state->preparingScene.reset();
}

void processTransition(
	const std::shared_ptr<WindowState> &state,
	FramePhase before,
	FrameAction action
) {
	const auto phase = state->scheduler.phase();
	if (phase == FramePhase::failed) {
		fail(
			state,
			before == FramePhase::draining ? "GPU drain deadline exceeded"
										   : "Tile presentation recovery exhausted"
		);
		return;
	}
	if ((phase == FramePhase::draining || phase == FramePhase::recovering) &&
		before != FramePhase::draining && before != FramePhase::recovering) {
		invalidateEvidence(state);
		if (state->scheduler.recoveryConsumed()) {
			if (state->offscreenRecoveryUsed) {
				fail(state, "Renderer recovery exhausted before confirmation");
				return;
			}
			state->offscreenRecoveryUsed = true;
			++state->work.recoveries;
		}
		if (state->traceRendering) {
			log(Level::trace, "macos.presentation", "Presentation recovery", [&] -> auto {
				return std::array{
					Field{"epoch", state->presentationEpoch},
					Field{"phase", static_cast<std::uint64_t>(phase)}
				};
			});
		}
	}
	switch (action) {
	case FrameAction::none:
		if (phase == FramePhase::draining && before != FramePhase::draining) {
			armNextWake(state);
		}
		break;
	case FrameAction::retry:
		requestFrame(state);
		armNextWake(state);
		break;
	case FrameAction::replaceGrid:
		try {
			const bool preserve = std::exchange(state->preserveGridResources, false);
			replaceGrid(state, preserve);
			requestFrame(state);
		} catch (const std::exception &error) {
			fail(state, error.what());
		}
		break;
	case FrameAction::failure:
		fail(state, "Tile presentation recovery exhausted");
		break;
	}
}

void applySceneBackground(CALayer *layer, SceneColor background) {
	if (layer == nil) {
		return;
	}
	const CoreOwner color{CGColorCreateGenericRGB(
		background.red,
		background.green,
		background.blue,
		background.alpha >= 1.0 ? 1.0 : 0.0
	)};
	layer.backgroundColor = color.get();
}

[[nodiscard]] auto disabledLayerActions() -> NSDictionary * {
	return @{
		@"sublayers" : NSNull.null,
		@"bounds" : NSNull.null,
		@"position" : NSNull.null,
		@"contents" : NSNull.null,
		@"backgroundColor" : NSNull.null
	};
}

void ensureHost(const std::shared_ptr<WindowState> &state, NSView *view, double scale) {
	const bool opaque = state->nativeWindow == nil || state->nativeWindow.opaque;
	if (state->hostLayer == nil || view.layer != state->hostLayer) {
		CALayer *const host = [CALayer layer];
		host.geometryFlipped = YES;
		host.masksToBounds = YES;
		host.opaque = opaque;
		host.frame = NSRectToCGRect(view.bounds);
		host.contentsScale = scale;
		host.edgeAntialiasingMask = 0;
		host.shouldRasterize = NO;
		host.actions = disabledLayerActions();
		CALayer *const previous = view.layer;
		if (previous != nil && state->contentPresented && state->layer == previous) {
			[host addSublayer:previous];
		} else if (state->fallbackLayer != nil &&
			(state->fallbackLayer.superlayer == previous || previous == state->fallbackLayer)) {
			[host addSublayer:state->fallbackLayer];
		}
		view.wantsLayer = YES;
		view.layer = host;
		state->hostLayer = host;
	}
	state->hostLayer.opaque = opaque;
	const CGRect frame = NSRectToCGRect(view.bounds);
	if (!CGRectEqualToRect(state->hostLayer.frame, frame)) {
		state->hostLayer.frame = frame;
	}
	if (std::is_neq(state->hostLayer.contentsScale <=> scale)) {
		state->hostLayer.contentsScale = scale;
	}
	state->hostLayer.actions = disabledLayerActions();
}

/// @brief Keep one displayed root above the replacement. Unpublished roots are discarded.
void retireDisplayedRoot(const std::shared_ptr<WindowState> &state, LogicalSize presentedSize) {
	CALayer *const current = state->layer;
	if (state->contentPresented && current != nil) {
		if (state->fallbackLayer != nil && state->fallbackLayer != current) {
			[state->fallbackLayer removeFromSuperlayer];
		}
		current.frame = CGRectMake(0, 0, presentedSize.width, presentedSize.height);
		if (current.superlayer != state->hostLayer) {
			[state->hostLayer addSublayer:current];
		}
		state->fallbackLayer = current;
		state->contentPresented = false;
		state->layer = nil;
		state->tileLayers.clear();
		return;
	}
	if (current != nil && current != state->fallbackLayer) {
		[current removeFromSuperlayer];
		state->layer = nil;
		state->tileLayers.clear();
	}
}

void assertDisplayedContinuity(const WindowState &state, bool hadDisplayedRoot) {
	if (!hadDisplayedRoot || state.view == nil || state.view.window == nil ||
		state.hostLayer == nil) {
		return;
	}
	if (state.fallbackLayer == nil && !state.contentPresented) {
		throw std::logic_error("Replacement surface is visible without displayed content");
	}
	if (state.fallbackLayer != nil && state.fallbackLayer.superlayer != state.hostLayer) {
		throw std::logic_error("Displayed fallback is not owned by the host layer");
	}
	if (state.layer != nil && state.layer.superlayer != state.hostLayer) {
		throw std::logic_error("Replacement root is not owned by the host layer");
	}
	if (state.hostLayer.sublayers.count > 2) {
		throw std::logic_error("Resize retained more than one fallback surface");
	}
}

void finishCoalescedInstall(const std::shared_ptr<WindowState> &state) {
	if (state->isClosed || state->scheduler.phase() == FramePhase::draining ||
		state->scheduler.phase() == FramePhase::failed ||
		state->scheduler.phase() == FramePhase::closed) {
		return;
	}
	NSView *const view = state->view;
	if (view == nil || view.window == nil) {
		return;
	}
	const double scale = view.window.backingScaleFactor;
	const LogicalSize size{.width = view.bounds.size.width, .height = view.bounds.size.height};
	if (state->scheduler.phase() == FramePhase::idle && state->layer != nil &&
		state->logicalSize == size && std::is_eq(state->scale <=> scale)) {
		state->preserveGridResources = false;
		requestFrame(state);
		return;
	}
	const bool preserve = std::exchange(state->preserveGridResources, false);
	replaceGrid(state, preserve);
	requestFrame(state);
}

void syncHostGeometry(const std::shared_ptr<WindowState> &state, NSView *view, double scale) {
	if (state->hostLayer == nil || view.layer != state->hostLayer) {
		return;
	}
	const CGRect frame = NSRectToCGRect(view.bounds);
	const bool frameChanged = !CGRectEqualToRect(state->hostLayer.frame, frame);
	const bool scaleChanged = std::is_neq(state->hostLayer.contentsScale <=> scale);
	if (!frameChanged && !scaleChanged) {
		return;
	}
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	if (frameChanged) {
		state->hostLayer.frame = frame;
	}
	if (scaleChanged) {
		state->hostLayer.contentsScale = scale;
	}
	if (const auto scene = state->callbacks.scene()) {
		applySceneBackground(state->hostLayer, (*scene)->background);
	}
	[CATransaction commit];
}

[[nodiscard]] auto gridMatchesView(const WindowState &state, LogicalSize size, double scale)
	-> bool {
	return state.layer != nil && state.logicalSize == size && std::is_eq(state.scale <=> scale);
}

void noteLiveResizeBody(const std::shared_ptr<WindowState> &state) {
	if (state->isClosed || state->scheduler.phase() == FramePhase::failed ||
		state->scheduler.phase() == FramePhase::closed) {
		return;
	}
	NSView *const view = state->view;
	if (view == nil || view.window == nil) {
		return;
	}
	const double scale = view.window.backingScaleFactor;
	const LogicalSize size{.width = view.bounds.size.width, .height = view.bounds.size.height};
	// The synchronous install owns the first grid. Gating before it exists abandons the
	// only present that can clear input.
	if (state->layer == nil && state->hostLayer == nil) {
		return;
	}
	const auto phase = state->scheduler.phase();
	if (gridMatchesView(*state, size, scale) &&
		(phase == FramePhase::idle || phase == FramePhase::occluded ||
		 phase == FramePhase::awaitingEvidence || phase == FramePhase::building)) {
		// A build already admitted for this grid must reach submission. Resizing the host
		// here reenters setFrameSize and would drop that build.
		if (phase != FramePhase::building) {
			syncHostGeometry(state, view, scale);
		}
		return;
	}
	syncHostGeometry(state, view, scale);
	invalidateEvidence(state);
	static_cast<void>(state->scheduler.invalidateSurface(now()));
	state->preserveGridResources = true;
	if (state->scheduler.phase() == FramePhase::draining) {
		armNextWake(state);
	}
	advance(state->surfaceInstallGeneration);
	const auto token = state->surfaceInstallGeneration;
	const auto weak = std::weak_ptr{state};
	dispatch_async(dispatch_get_main_queue(), ^{
	  if (const auto current = weak.lock(); current && !current->isClosed &&
		  current->surfaceInstallGeneration == token) {
		  try {
			  finishCoalescedInstall(current);
		  } catch (const std::exception &error) {
			  fail(current, error.what());
		  }
	  }
	});
}

void replaceGrid(const std::shared_ptr<WindowState> &state, bool preserveResources) {
	if (state->scheduler.phase() == FramePhase::draining) {
		throw std::logic_error("Unsafe grid replacement before GPU completion");
	}
	advance(state->surfaceEpoch);
	NSView *const view = state->view;
	if (view.window == nil) {
		state->tiles.clear();
		state->tileLayers.clear();
		state->active.reset();
		state->rasterBaseline.reset();
		state->confirmedComposite.reset();
		state->latestRequested.reset();
		state->grid = {};
		state->logicalSize = {};
		state->scale = 0;
		state->layer = nil;
		state->hostLayer = nil;
		state->fallbackLayer = nil;
		state->contentPresented = false;
		view.layer = nil;
		state->work.tileCount = 0;
		state->work.estimatedDrawableBytes = 0;
		state->scheduler.gridInstalled();
		return;
	}
	const auto scene = state->callbacks.scene();
	if (!scene) {
		throw std::runtime_error(scene.error());
	}
	const bool nextHybrid = gpu::Compositor::requiresHybrid(*scene, state->callbacks.colorMode);
	const bool nextLinear = gpu::Compositor::requiresLinear(*scene, state->callbacks.colorMode) &&
		state->callbacks.colorMode != gpu::ColorMode::sdr;
	const bool sameAttachment = preserveResources && state->layer != nil &&
		nextHybrid == state->hybridSurface && nextLinear == state->linearOutput;
	state->hybridSurface = nextHybrid;
	state->linearOutput = nextLinear;
	if (sameAttachment) {
		state->compositor->invalidateForResize(state->presentationEpoch);
	} else {
		state->compositor->reset(state->presentationEpoch);
	}
	state->confirmedPacket.reset();
	state->pendingPacket.reset();
	state->preparingScene.reset();
	state->offscreenDeadlineArmed = false;
	state->offscreenTimedOut = false;
	advance(state->offscreenGeneration);
	const double scale = view.window.backingScaleFactor;
	const LogicalSize size{.width = view.bounds.size.width, .height = view.bounds.size.height};
	const LogicalSize presentedSize = state->logicalSize;
	const bool hadDisplayedRoot = state->contentPresented || state->fallbackLayer != nil;
	// Release safe obsolete buffers before budgeting a replacement; GPU owners are independent.
	// The fallback keeps the displayed CAMetalLayer tree, not these CPU buffers.
	state->tiles.clear();
	state->active.reset();
	state->rasterBaseline.reset();
	state->confirmedComposite.reset();
	state->latestRequested.reset();
	state->grid = makeTileGrid(
		size,
		scale,
		{.ownedBytes = state->accounting->bytes.load(),
			.maximumBufferLength = state->device.maxBufferLength}
	);
	state->logicalSize = size;
	state->scale = scale;
	CALayer *const root = [CALayer layer];
	root.geometryFlipped = YES;
	root.masksToBounds = YES;
	root.opaque = state->nativeWindow == nil || state->nativeWindow.opaque;
	root.frame = NSRectToCGRect(view.bounds);
	root.contentsScale = scale;
	root.edgeAntialiasingMask = 0;
	root.shouldRasterize = NO;
	root.actions = disabledLayerActions();
	applySceneBackground(root, (*scene)->background);
	state->tileLayers.clear();
	const CoreOwner colorspace{CGColorSpaceCreateWithName(
		state->linearOutput ? kCGColorSpaceExtendedLinearSRGB : kCGColorSpaceSRGB
	)};
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	ensureHost(state, view, scale);
	applySceneBackground(state->hostLayer, (*scene)->background);
	retireDisplayedRoot(state, presentedSize);
	for (const auto &layout : state->grid.tiles) {
		auto tile = state->hybridSurface
			? std::shared_ptr<RasterTile>{}
			: std::make_shared<RasterTile>(layout, state->device, state->accounting);
		CAMetalLayer *const layer = [CAMetalLayer layer];
		layer.device = state->device;
		layer.pixelFormat =
			state->linearOutput ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm;
		layer.wantsExtendedDynamicRangeContent = state->linearOutput;
		layer.colorspace = colorspace.get();
		layer.opaque = root.opaque;
		layer.framebufferOnly = NO;
		layer.maximumDrawableCount = tileDrawableCount;
		layer.displaySyncEnabled = YES;
		layer.presentsWithTransaction = YES;
		layer.allowsNextDrawableTimeout = NO;
		layer.geometryFlipped = NO;
		layer.anchorPoint = CGPointMake(0, 0);
		layer.bounds = CGRectMake(
			0,
			0,
			static_cast<double>(layout.pixels.width) / scale,
			static_cast<double>(layout.pixels.height) / scale
		);
		layer.position = CGPointMake(
			static_cast<double>(layout.pixels.x) / scale,
			static_cast<double>(layout.pixels.y) / scale
		);
		layer.contentsScale = scale;
		layer.drawableSize = CGSizeMake(
			static_cast<double>(layout.pixels.width),
			static_cast<double>(layout.pixels.height)
		);
		layer.edgeAntialiasingMask = 0;
		layer.shouldRasterize = NO;
		layer.transform = CATransform3DIdentity;
		layer.actions =
			@{@"bounds" : NSNull.null, @"position" : NSNull.null, @"contents" : NSNull.null};
		[root addSublayer:layer];
		state->tileLayers.push_back(layer);
		if (tile) {
			state->tiles.push_back(std::move(tile));
		}
	}
	view.wantsLayer = YES;
	[state->hostLayer insertSublayer:root atIndex:0];
	state->layer = root;
	[CATransaction commit];
	[CATransaction flush];
	assertDisplayedContinuity(*state, hadDisplayedRoot);
	state->work.tileCount = state->grid.tiles.size();
	state->work.estimatedDrawableBytes = state->grid.viewport.width * state->grid.viewport.height *
		(state->linearOutput ? gpu::pixelBytes(gpu::Format::rgba16) : bytesPerPixel) *
		tileDrawableCount;
	state->scheduler.gridInstalled();
}

void confirmed(const std::shared_ptr<WindowState> &state, FrameIdentity attempt) {
	if (!state->active || !state->active->scheduledPresentationIssued ||
		!state->scheduler.confirm(attempt)) {
		return;
	}
	state->confirmedComposite = state->active->target;
	state->confirmedPacket = state->active->composite;
	state->confirmedHeadroom = state->active->headroom;
	state->offscreenRecoveryUsed = false;
	state->pendingPacket.reset();
	state->preparingScene.reset();
	state->confirmedRevision = attempt.sceneRevision;
	state->active.reset();
	advance(state->timerGeneration);
	if (state->traceRendering) {
		log(Level::trace, "macos.presentation", "Presentation confirmed", [&] -> auto {
			return std::array{
				Field{"revision", attempt.sceneRevision},
				Field{"sequence", attempt.sequence}
			};
		});
	}
	requestFrame(state);
}

[[nodiscard]] auto
retryUnknownPresentation(const std::shared_ptr<WindowState> &state, FrameIdentity attempt) -> bool {
	if (!state->active || state->active->attempt.sequence != attempt.sequence ||
		!state->active->scheduledPresentationIssued || !state->active->proofPresentedTime ||
		*state->active->proofPresentedTime > 0) {
		return false;
	}
	const auto before = state->scheduler.phase();
	const auto action = state->scheduler.unknownPresentation(now());
	if (action == FrameAction::none) {
		return false;
	}
	state->callbacks.abandonPresentation(attempt);
	state->active.reset();
	if (state->traceRendering) {
		log(Level::trace,
			"macos.presentation",
			"Retrying after an unknown presentation timestamp",
			[&] -> auto {
				return std::array{
					Field{"epoch", attempt.epoch},
					Field{"sequence", attempt.sequence}
				};
			});
	}
	processTransition(state, before, action);
	return true;
}

void finishEvidence(const std::shared_ptr<WindowState> &state, FrameIdentity attempt) {
	if (!state->active || state->active->attempt.sequence != attempt.sequence ||
		!state->active->scheduledPresentationIssued) {
		return;
	}
	const auto proofTime = state->active->proofPresentedTime;
	if (!proofTime) {
		return;
	}
	if (*proofTime > 0) {
		if (state->callbacks.drawablePresented(
				attempt,
				{.tile = state->active->proofTile, .timestamp = *proofTime}
			)) {
			confirmed(state, attempt);
		}
		return;
	}
	static_cast<void>(retryUnknownPresentation(state, attempt));
}

void issueScheduledPresent(
	const std::shared_ptr<WindowState> &state,
	FrameIdentity attempt,
	NSMutableArray<id<CAMetalDrawable>> *drawables,
	std::size_t presentedCount
) {
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	if (state->fallbackLayer != nil) {
		[state->fallbackLayer removeFromSuperlayer];
		state->fallbackLayer = nil;
	}
	state->contentPresented = true;
	for (NSUInteger index = 0; index < drawables.count; ++index) {
		[[drawables objectAtIndex:index] present];
	}
	[CATransaction commit];
	[CATransaction flush];
	state->work.presentedTiles += presentedCount;
	if (state->active && state->active->attempt.sequence == attempt.sequence) {
		state->active->scheduledPresentationIssued = true;
		finishEvidence(state, attempt);
	}
}

void render(const std::shared_ptr<WindowState> &state) {
	const auto result = state->callbacks.scene();
	if (!result) {
		throw std::runtime_error(result.error());
	}
	const auto &scene = *result;
	state->compositor->desired(scene);
	if (state->offscreenTimedOut && !state->compositor->isInFlight()) {
		state->offscreenTimedOut = false;
		rendererFailure(
			state,
			{.category = gpu::Diagnostic::Category::execution,
				.message = "Offscreen execution exceeded its deadline"}
		);
		return;
	}
	if (!state->compositor->isInFlight() && state->offscreenDeadlineArmed) {
		state->offscreenDeadlineArmed = false;
		advance(state->offscreenGeneration);
	}
	if (state->view != nil && state->view.window != nil &&
		(state->layer != nil || state->hostLayer != nil)) {
		const double scale = state->view.window.backingScaleFactor;
		const LogicalSize size{
			.width = state->view.bounds.size.width,
			.height = state->view.bounds.size.height
		};
		if (state->logicalSize != size || std::is_neq(state->scale <=> scale)) {
			noteLiveResize(state);
			return;
		}
	}
	if (!state->scheduler.canBuild(visible(*state))) {
		if (state->traceRendering) {
			log(Level::trace, "macos.presentation", "Frame deferred", [&] -> auto {
				return std::array{
					Field{"phase", static_cast<std::uint64_t>(state->scheduler.phase())},
					Field{"visible", state->view.window.isVisible != NO},
					Field{
						"occlusionState",
						static_cast<std::uint64_t>(state->view.window.occlusionState)
					}
				};
			});
		}
		return;
	}

	if (state->grid.tiles.empty()) {
		state->callbacks.gateSurface();
		state->scheduler.noDemand();
		return;
	}
	if (!state->hybridSurface && state->confirmedComposite &&
		state->confirmedComposite->source->revision == scene->revision) {
		state->scheduler.noDemand();
		return;
	}
	const bool hybrid = gpu::Compositor::requiresHybrid(
		state->preparingScene ? state->preparingScene->source : scene,
		state->callbacks.colorMode
	);
	const bool linear = gpu::Compositor::requiresLinear(
							state->preparingScene ? state->preparingScene->source : scene,
							state->callbacks.colorMode
						) &&
		state->callbacks.colorMode != gpu::ColorMode::sdr;
	if (hybrid != state->hybridSurface || linear != state->linearOutput) {
		installSurface(state);
		return;
	}
	const auto previous = state->latestRequested ? state->latestRequested : state->rasterBaseline;
	state->preparationTrace = {};
	const auto target = state->preparingScene
		? state->preparingScene
		: prepareScene(scene, state->scale, previous, &state->preparationTrace);
	state->latestRequested = target;

	const auto rasterDirty = sceneDamage(state->rasterBaseline, target, state->grid, state->work);
	auto presentationDirty =
		sceneDamage(state->confirmedComposite, target, state->grid, state->work);
	std::shared_ptr<const gpu::CompositePacket> composite;
	const double headroom = state->linearOutput
		? std::max(1.0, state->view.window.screen.maximumExtendedDynamicRangeColorComponentValue)
		: 1.0;
	if (state->hybridSurface) {
		if (!state->preparingScene) {
			state->preparingScene = target;
			if (state->callbacks.holdPreparedGeometry) {
				state->callbacks.holdPreparedGeometry(target->source->revision);
			}
		}
		const auto prepared = state->pendingPacket ? gpu::Result<bool>{true}
												   : state->compositor->prepare(
														 target,
														 state->grid,
														 state->presentationEpoch,
														 state->callbacks.colorMode
													 );
		if (!prepared) {
			rendererFailure(state, prepared.error());
			return;
		}
		if (!*prepared) {
			state->scheduler.noDemand();
			if (state->compositor->isInFlight() && !state->offscreenDeadlineArmed) {
				state->offscreenDeadlineArmed = true;
				advance(state->offscreenGeneration);
				const auto token = state->offscreenGeneration;
				const auto weak = std::weak_ptr{state};
				postAfter(FrameScheduler::confirmationDeadline, [weak, token] -> void {
					if (const auto owner = weak.lock(); owner && !owner->isClosed &&
						owner->offscreenGeneration == token && owner->compositor->isInFlight()) {
						owner->offscreenTimedOut = true;
						owner->callbacks.gateSurface();
						postAfter(FrameScheduler::recoveryDrainDeadline, [weak, token] -> void {
							if (const auto current = weak.lock(); current && !current->isClosed &&
								current->offscreenGeneration == token &&
								current->compositor->isInFlight()) {
								fail(current, "Offscreen GPU drain deadline exceeded");
							}
						});
					}
				});
			}
			return;
		}
		composite = state->pendingPacket ? state->pendingPacket : state->compositor->readyPacket();
		state->pendingPacket = composite;
		if (state->confirmedPacket != composite) {
			presentationDirty |= composite->dirty;
		}
		if (std::is_neq(headroom <=> state->confirmedHeadroom)) {
			presentationDirty = allTiles(state->grid);
		}
		if (state->confirmedComposite &&
			state->confirmedComposite->source->revision == target->source->revision &&
			state->confirmedPacket == composite && presentationDirty.none()) {
			state->pendingPacket.reset();
			state->preparingScene.reset();
			state->scheduler.noDemand();
			return;
		}
	} else {
		struct SymbolResultKey {
			std::uint64_t document = 0;
			std::uint64_t serial = 0;
			std::uint64_t generation = 0;
			auto operator<=>(const SymbolResultKey &) const = default;
		};

		std::map<SymbolResultKey, SymbolFailure> symbolResults;
		const std::function<void(SymbolFailure)> collectSymbol =
			[&](SymbolFailure failure) -> void {
			const SymbolResultKey key{
				.document = failure.instance.document,
				.serial = failure.instance.serial,
				.generation = failure.generation
			};
			const auto found = symbolResults.find(key);
			if (found == symbolResults.end() || !found->second.code.has_value()) {
				symbolResults.insert_or_assign(key, failure);
			}
		};
		for (const auto &layout : state->grid.tiles) {
			if (!rasterDirty.test(layout.ordinal)) {
				continue;
			}
			rasterScene(
				state->tiles.at(layout.ordinal)->bitmapContext,
				*target,
				layout.pixels,
				true,
				state->presentationEpoch,
				collectSymbol
			);
			state->tiles.at(layout.ordinal)->cpuDirty = true;
			++state->work.rasterizedTiles;
			state->work.rasterizedPixels += layout.pixels.width * layout.pixels.height;
		}
		if (target->source->symbolError) {
			for (const auto &entry : symbolResults) {
				target->source->symbolError(entry.second);
			}
		}
	}
	state->rasterBaseline = target;
	const bool anchor = presentationDirty.none();
	if (anchor) {
		presentationDirty.set(0);
	}
	const auto expected = tileIds(state->grid, presentationDirty);
	const auto proofTile = expected.front();
	state->drawableAcquisitions.clear();
	auto acquired = acquireAll(expected, [&](TileId tile) -> id<CAMetalDrawable> {
		id<CAMetalDrawable> const drawable = [state->tileLayers.at(tile.value) nextDrawable];
		if (drawable == nil) {
			return nil;
		}
		if (drawable.texture.width != state->grid.tiles.at(tile.value).pixels.width ||
			drawable.texture.height != state->grid.tiles.at(tile.value).pixels.height ||
			drawable.texture.pixelFormat !=
				(state->linearOutput ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm) ||
			drawable.texture.device != state->device) {
			throw std::runtime_error("Tile drawable does not match grid");
		}
		state->drawableAcquisitions.push_back(tile);
		return drawable;
	});
	if (!acquired) {
		const auto before = state->scheduler.phase();
		const auto action = state->scheduler.acquisitionFailed(visible(*state), now());
		if (action == FrameAction::retry) {
			armNextWake(state);
		} else {
			processTransition(state, before, action);
		}
		return;
	}
	NSMutableArray<id<CAMetalDrawable>> *const drawables = [NSMutableArray array];
	for (const auto &drawable : *acquired) {
		[drawables addObject:drawable];
	}
	acquired.reset();

	id<MTLCommandBuffer> const buffer = [state->queue commandBuffer];
	if (buffer == nil) {
		throw std::runtime_error("Unable to create complete tile GPU submission");
	}
	auto resources = std::make_shared<BatchResources>();
	resources->target = target;
	resources->composite = composite;
	std::uint64_t copiedBytes = 0;
	if (composite) {
		for (std::size_t index = 0; index < expected.size(); ++index) {
			state->compositor->display(
				buffer,
				*composite,
				expected.at(index).value,
				[drawables objectAtIndex:index].texture,
				headroom
			);
			const auto &layout = state->grid.tiles.at(expected.at(index).value);
			copiedBytes += layout.pixels.width * layout.pixels.height *
				(state->linearOutput ? gpu::pixelBytes(gpu::Format::rgba16) : bytesPerPixel);
		}
	} else {
		id<MTLBlitCommandEncoder> const encoder = [buffer blitCommandEncoder];
		if (encoder == nil) {
			throw std::runtime_error("Unable to create tile GPU encoding");
		}
		for (std::size_t index = 0; index < expected.size(); ++index) {
			const auto &tile = state->tiles.at(expected.at(index).value);
			encodeTile(encoder, *tile, [drawables objectAtIndex:index].texture);
			resources->sources.push_back(tile);
			copiedBytes += tile->layout.pixels.width * tile->layout.pixels.height * bytesPerPixel;
		}
		[encoder endEncoding];
	}
	const auto admitted = state->callbacks.beginPresentation(
		{.epoch = state->presentationEpoch,
			.sceneRevision = target->source->revision,
			.expectedTiles = {proofTile}}
	);
	if (!admitted) {
		throw std::runtime_error(admitted.error());
	}
	const auto attempt = *admitted;
	const auto surface = state->surfaceEpoch;
	const auto attachment = state->attachmentGeneration;
	state->callbacks.gatePendingPresentation(attempt);
	state->active = ActiveBatch{
		.attempt = attempt,
		.surfaceEpoch = surface,
		.attachmentGeneration = attachment,
		.target = target,
		.composite = composite,
		.submittedTiles = expected,
		.headroom = headroom,
		.proofTile = proofTile
	};
	state->scheduler.submitted(attempt, now());
	const auto weak = std::weak_ptr{state};
	const bool tracing = state->traceRendering;
	const auto presentedCount = expected.size();
	const auto scheduledDelay = state->presentDelay;
	for (std::size_t index = 0; index < expected.size(); ++index) {
		const auto tile = expected.at(index);
		[[drawables objectAtIndex:index] addPresentedHandler:^(id<MTLDrawable> presented) {
		  const double timestamp = presented.presentedTime;
		  if (tracing) {
			  log(Level::trace, "macos.presentation", "Tile presentation timestamp", [&] -> auto {
				  return std::array{
					  Field{"epoch", attempt.epoch},
					  Field{"sequence", attempt.sequence},
					  Field{"tile", static_cast<std::uint64_t>(tile.value)},
					  Field{"timestamp", timestamp}
				  };
			  });
		  }
		  dispatch_async(dispatch_get_main_queue(), ^{
			if (const auto current = weak.lock(); current && !current->isClosed &&
				current->presentationEpoch == attempt.epoch && current->surfaceEpoch == surface &&
				current->attachmentGeneration == attachment && current->active &&
				current->active->attempt.sequence == attempt.sequence) {
				if (tile == proofTile) {
					current->active->proofPresentedTime = timestamp;
				}
				finishEvidence(current, attempt);
			}
		  });
		}];
	}
	[buffer addScheduledHandler:^(id<MTLCommandBuffer> scheduled) {
	  static_cast<void>(scheduled);
	  auto present = ^{
		@autoreleasepool {
			if (const auto current = weak.lock(); current && !current->isClosed &&
				current->presentationEpoch == attempt.epoch && current->surfaceEpoch == surface &&
				current->attachmentGeneration == attachment &&
				current->scheduler.matches(attempt)) {
				issueScheduledPresent(current, attempt, drawables, presentedCount);
			}
			[drawables removeAllObjects];
		}
	  };
	  const auto deliver = [present]() -> void {
		  if ([NSThread isMainThread]) {
			  present();
		  } else {
			  dispatch_async(dispatch_get_main_queue(), present);
		  }
	  };
	  if (scheduledDelay.count() > 0) {
		  postAfter(scheduledDelay, deliver);
	  } else {
		  deliver();
	  }
	}];
	[buffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
	  const bool success = completed.status == MTLCommandBufferStatusCompleted;
	  // No owner -> command-buffer -> completion-handler -> owner cycle. Source
	  // storage is independently retained until the GPU read hazard has ended.
	  resources->sources.clear();
	  resources->target.reset();
	  resources->composite.reset();
	  if (tracing) {
		  log(Level::trace, "macos.presentation", "GPU command buffer completed", [&] -> auto {
			  return std::array{
				  Field{"epoch", attempt.epoch},
				  Field{"sequence", attempt.sequence},
				  Field{"status", static_cast<std::int64_t>(completed.status)}
			  };
		  });
	  }
	  dispatch_async(dispatch_get_main_queue(), ^{
		if (const auto current = weak.lock(); current && !current->isClosed) {
			const auto before = current->scheduler.phase();
			const auto action = current->scheduler.gpuFinished(attempt, success, now());
			if (current->presentationEpoch == attempt.epoch && current->surfaceEpoch == surface &&
				current->attachmentGeneration == attachment) {
				if (current->callbacks.gpuCompleted(attempt, success)) {
					confirmed(current, attempt);
				} else if (retryUnknownPresentation(current, attempt)) {
					return;
				}
			}
			processTransition(current, before, action);
		}
	  });
	}];
	applySceneBackground(state->layer, scene->background);
	applySceneBackground(state->hostLayer, scene->background);
	state->submittedRevision = target->source->revision;
	state->work.copiedBytes += copiedBytes;
	if (anchor) {
		++state->work.anchorAttempts;
	}
	if (tracing) {
		log(Level::trace, "macos.presentation", "Frame submitted", [&] -> auto {
			return std::array{
				Field{"revision", scene->revision},
				Field{"surfaceEpoch", surface},
				Field{"epoch", attempt.epoch},
				Field{"sequence", attempt.sequence},
				Field{"active", NSApp.isActive != NO},
				Field{"onActiveSpace", state->view.window.isOnActiveSpace != NO},
				Field{"tiles", static_cast<std::uint64_t>(expected.size())},
				Field{"proofTile", static_cast<std::uint64_t>(proofTile.value)},
				Field{"rasterizedTiles", static_cast<std::uint64_t>(rasterDirty.count())},
				Field{"copiedBytes", copiedBytes}
			};
		});
	}
	[buffer commit];
	armNextWake(state);
}
} // namespace

void rendererFailure(const std::shared_ptr<WindowState> &state, const gpu::Failure &failure) {
	state->failureMessage = failure.message;
	if (state->offscreenRecoveryUsed || state->scheduler.recoveryConsumed()) {
		fail(state, failure.message);
		return;
	}
	state->offscreenRecoveryUsed = true;
	++state->work.recoveries;
	installSurface(state);
}

void requestFrame(const std::shared_ptr<WindowState> &state) {
	if (state->isClosed || state->isFrameQueued || state->scheduler.phase() == FramePhase::failed) {
		return;
	}
	state->isFrameQueued = true;
	const auto weak = std::weak_ptr{state};
	dispatch_async(dispatch_get_main_queue(), ^{
	  if (const auto current = weak.lock(); current && !current->isClosed) {
		  current->isFrameQueued = false;
		  try {
			  render(current);
		  } catch (const std::exception &error) {
			  fail(current, error.what());
		  }
	  }
	});
}

void installSurface(const std::shared_ptr<WindowState> &state) {
	if (state->isClosed) {
		return;
	}
	advance(state->surfaceInstallGeneration);
	state->preserveGridResources = false;
	invalidateEvidence(state);
	const auto action = state->scheduler.invalidateSurface(now());
	if (action == FrameAction::replaceGrid) {
		replaceGrid(state, false);
		requestFrame(state);
	} else if (state->scheduler.phase() == FramePhase::draining) {
		armNextWake(state);
	}
}

void noteLiveResize(const std::shared_ptr<WindowState> &state) {
	noteLiveResizeBody(state);
}

void setPresentDelay(
	const std::shared_ptr<WindowState> &state,
	std::chrono::milliseconds delay
) {
	if (delay.count() < 0) {
		throw std::invalid_argument("Presentation delay cannot be negative");
	}
	state->presentDelay = delay;
}

void closeRenderer(const std::shared_ptr<WindowState> &state) {
	state->compositor->close();
	state->confirmedPacket.reset();
	state->pendingPacket.reset();
	state->preparingScene.reset();
	advance(state->offscreenGeneration);
	state->scheduler.close();
	advance(state->timerGeneration);
	state->active.reset();
	state->tiles.clear();
	state->tileLayers.clear();
	state->rasterBaseline.reset();
	state->confirmedComposite.reset();
	state->latestRequested.reset();
	advance(state->surfaceInstallGeneration);
	state->preserveGridResources = false;
	state->contentPresented = false;
	state->fallbackLayer = nil;
	state->hostLayer = nil;
	state->view.layer = nil;
	state->layer = nil;
}
} // namespace stana::platform::detail
