#include "presentation.hpp"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <dispatch/dispatch.h>
#include <exception>
#include <expected>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace stana::native {
void publishAppearance(const std::shared_ptr<WindowState> &state);

namespace {
constexpr double initialWindowOffset = 40;

[[nodiscard]] auto stopRequested() -> std::atomic<bool> & {
	static std::atomic<bool> requested{false};
	return requested;
}

[[nodiscard]] auto activeWindows() -> std::vector<std::weak_ptr<WindowState>> & {
	static thread_local std::vector<std::weak_ptr<WindowState>> windows;
	return windows;
}

[[nodiscard]] auto nativeString(const std::string &text) -> NSString * {
	NSString *const value = [[NSString alloc] initWithBytes:text.data()
													 length:text.size()
												   encoding:NSUTF8StringEncoding];
	if (value == nil) {
		throw std::invalid_argument("Stana text must be valid UTF-8");
	}
	return value;
}
} // namespace

/// @brief Copies the view's effective light or dark appearance into the document.
/// Call after the view is installed in a window, and again when AppKit changes it.
/// An unset callback leaves the document on its existing theme.
void publishAppearance(const std::shared_ptr<WindowState> &state) {
	if (state->isClosed || !state->callbacks.appearance) {
		return;
	}
	NSView *const view = state->view;
	if (view == nil) {
		return;
	}
	NSAppearanceName const matched = [view.effectiveAppearance
		bestMatchFromAppearancesWithNames:@[ NSAppearanceNameDarkAqua, NSAppearanceNameAqua ]];
	state->callbacks.appearance([matched isEqualToString:NSAppearanceNameDarkAqua]);
}

} // namespace stana::native

/// @brief AppKit view that translates native events into the retained document callbacks.
/// The native view hierarchy owns this object; state_ owns no strong view reference.
@interface STANAView : NSView {
  @public
	std::shared_ptr<stana::native::WindowState>
		state_; ///< Engine callbacks and renderer resources.
}
@end

@implementation STANAView

- (BOOL)isFlipped {
	return YES;
}

- (BOOL)acceptsFirstResponder {
	return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent *)event {
	static_cast<void>(event);
	return YES;
}

- (BOOL)wantsUpdateLayer {
	return YES;
}

- (void)updateLayer {
	if (state_) {
		stana::native::requestFrame(state_);
	}
}

- (void)setFrameSize:(NSSize)size {
	const bool sizeChanged = !NSEqualSizes(self.frame.size, size);
	[super setFrameSize:size];
	if (state_ && !state_->isClosed && sizeChanged) {
		stana::native::installSurface(state_);
		state_->callbacks.resize(size.width, size.height);
		stana::native::requestFrame(state_);
	}
}

- (void)viewDidMoveToWindow {
	[super viewDidMoveToWindow];
	if (state_ && !state_->isClosed) {
		stana::native::installSurface(state_);
		stana::native::requestFrame(state_);
	}
}

- (void)viewDidChangeBackingProperties {
	[super viewDidChangeBackingProperties];
	if (state_ && !state_->isClosed && self.window != nil &&
		std::is_neq(self.window.backingScaleFactor <=> state_->layer.contentsScale)) {
		stana::native::installSurface(state_);
		stana::native::requestFrame(state_);
	}
}

- (void)viewDidChangeEffectiveAppearance {
	[super viewDidChangeEffectiveAppearance];
	if (state_ && !state_->isClosed) {
		stana::native::publishAppearance(state_);
	}
}

- (void)mouseDown:(NSEvent *)event {
	if (state_ && state_->traceRendering) {
		std::cerr << "Pointer gate=" << state_->allowsPointer()
				  << " confirmed=" << state_->confirmedRevision << " epoch=" << state_->surfaceEpoch
				  << '\n';
	}
	if (state_ && state_->allowsPointer()) {
		const auto point = [self convertPoint:event.locationInWindow fromView:nil];
		if (state_->traceRendering) {
			std::cerr << "Down x=" << point.x << " y=" << point.y << '\n';
		}
		state_->callbacks.pointerDown(point.x, point.y);
	}
}

- (void)mouseUp:(NSEvent *)event {
	if (state_ && state_->traceRendering) {
		std::cerr << "Pointer gate=" << state_->allowsPointer()
				  << " confirmed=" << state_->confirmedRevision << " epoch=" << state_->surfaceEpoch
				  << '\n';
	}
	if (state_ && state_->allowsPointer()) {
		const auto point = [self convertPoint:event.locationInWindow fromView:nil];
		if (state_->traceRendering) {
			std::cerr << "Up x=" << point.x << " y=" << point.y << '\n';
		}
		state_->callbacks.pointerUp(point.x, point.y);
	}
}

- (void)scrollWheel:(NSEvent *)event {
	if (state_ && state_->traceRendering) {
		std::cerr << "Pointer gate=" << state_->allowsPointer()
				  << " confirmed=" << state_->confirmedRevision << " epoch=" << state_->surfaceEpoch
				  << '\n';
	}
	if (state_ && state_->allowsPointer()) {
		const auto point = [self convertPoint:event.locationInWindow fromView:nil];
		state_->callbacks.scroll(point.x, point.y, -event.scrollingDeltaY);
	}
}

- (void)keyDown:(NSEvent *)event {
	if (!state_ || state_->isClosed) {
		return;
	}
	using stana::native::KeyAction;
	if ([event.charactersIgnoringModifiers isEqualToString:@"\t"]) {
		state_->callbacks.key(
			(event.modifierFlags & NSEventModifierFlagShift) != 0 ? KeyAction::tabBackward
																  : KeyAction::tabForward
		);
	} else if ([event.charactersIgnoringModifiers isEqualToString:@" "] ||
		[event.charactersIgnoringModifiers isEqualToString:@"\r"]) {
		state_->callbacks.key(KeyAction::activate);
	} else {
		[super keyDown:event];
	}
}

@end

/// @brief Delegate connecting native closure to document disposal and final loop shutdown.
@interface STANAWindowDelegate : NSObject <NSWindowDelegate> {
  @public
	std::shared_ptr<stana::native::WindowState> state_; ///< Closed atomically on the UI executor.
}
@end
@implementation STANAWindowDelegate

- (void)windowDidBecomeKey:(NSNotification *)notification {
	static_cast<void>(notification);
	if (state_ && !state_->isClosed) {
		stana::native::requestFrame(state_);
	}
}

- (void)windowDidChangeScreen:(NSNotification *)notification {
	static_cast<void>(notification);
	if (state_ && !state_->isClosed) {
		// Backing-scale changes replace the surface separately. A display/headroom
		// change only remaps the cached linear packet; it must not step state.
		stana::native::requestFrame(state_);
	}
}

- (void)screenParametersChanged:(NSNotification *)notification {
	static_cast<void>(notification);
	if (state_ && !state_->isClosed) {
		stana::native::requestFrame(state_);
	}
}

- (void)windowDidChangeOcclusionState:(NSNotification *)notification {
	static_cast<void>(notification);
	if (state_ && !state_->isClosed) {
		stana::native::requestFrame(state_);
	}
}

- (void)windowWillClose:(NSNotification *)notification {
	[NSNotificationCenter.defaultCenter removeObserver:self];
	NSWindow *const closingWindow = notification.object;
	if (state_ && !std::exchange(state_->isClosed, true)) {
		stana::native::closeRenderer(state_);
		state_->callbacks.close();
	}
	bool hasOpenWindow = false;
	for (NSUInteger index = 0; index < NSApp.windows.count; ++index) {
		NSWindow *const window = [NSApp.windows objectAtIndex:index];
		if (window != closingWindow && window.isVisible) {
			hasOpenWindow = true;
		}
	}
	if (!hasOpenWindow) {
		stana::native::stop();
	}
}

@end

namespace stana::native {
auto diagnostics() -> std::vector<WindowStatistics> {
	if (![NSThread isMainThread]) {
		throw std::logic_error("Native diagnostics require the UI executor");
	}
	std::vector<WindowStatistics> result;
	std::erase_if(activeWindows(), [](const auto &weak) -> bool {
		return weak.expired();
	});
	for (const auto &weak : activeWindows()) {
		if (const auto state = weak.lock(); state && !state->isClosed) {
			state->work.rasterBytes = state->accounting->bytes.load();
			state->work.peakRasterBytes = state->accounting->peak.load();
			state->work.bufferAllocations = state->accounting->allocations.load();
			state->work.hasActiveBatch = state->scheduler.hasActive();
			const auto latest = state->callbacks.scene();
			state->work.hasPendingScene = latest &&
				(!state->confirmedComposite ||
					state->confirmedComposite->source->revision != (*latest)->revision ||
					(state->hybridSurface &&
						(state->compositor->hasPendingWork() ||
							state->compositor->readyPacket() != state->confirmedPacket)));
			const auto &gpuWork = state->compositor->statistics();
			auto work = state->work;
			work.rasterizedPixels += gpuWork.cpuRasterPixels;
			const auto &budget = state->compositor->resources().budget;
			const std::scoped_lock lock{budget->mutex};
			const char *const deviceName = state->device.name.UTF8String;
			result.push_back(
				{.deviceName = deviceName != nullptr ? std::string{deviceName} : std::string{},
					.unifiedMemory = state->device.hasUnifiedMemory,
					.linearWorking = state->compositor->readyPacket() &&
						state->compositor->readyPacket()->linear,
					.linearOutput = state->linearOutput,
					.meshSampleCount = state->compositor->readyPacket()
						? state->compositor->readyPacket()->samples
						: 1,
					.offscreenRevision = gpuWork.completedRevision,
					.stateSteps = gpuWork.stateSteps,
					.pipelineBuilds = gpuWork.pipelineBuilds,
					.pipelineCacheHits = gpuWork.pipelineCacheHits,
					.resourceUploadBytes = state->compositor->resources().uploadBytes,
					.compositedTiles = gpuWork.compositedTiles,
					.gpuLeafDraws = gpuWork.gpuLeafDraws,
					.trackedBytes = budget->bytes,
					.peakTrackedBytes = budget->peak,
					.managedWriteBytes = state->accounting->managedWriteBytes.load(),
					.logicalViewport = state->logicalSize,
					.backingScale = state->scale,
					.surfaceEpoch = state->surfaceEpoch,
					.submittedRevision = state->submittedRevision,
					.confirmedRevision = state->confirmedRevision,
					.failure = state->failureMessage,
					.renderingWork = work,
					.preparedBranches = state->preparationTrace.branches,
					.preparedPaints = state->preparationTrace.paints,
					.drawableAcquisitions = state->drawableAcquisitions}
			);
		}
	}
	return result;
}

auto createMailboxWakeup(std::function<void()> onWake)
	-> std::expected<MailboxWakeup, std::string> {
	if (![NSThread isMainThread]) {
		return std::unexpected("Native mailbox wakeup requires the main thread");
	}

	struct Source {
		dispatch_source_t source = nullptr;
		std::atomic<bool> closed{false};
		std::function<void()> handler;

		Source() = default;
		Source(const Source &) = delete;
		Source(Source &&) = delete;
		auto operator=(const Source &) -> Source & = delete;
		auto operator=(Source &&) -> Source & = delete;

		~Source() {
			if (source != nullptr) {
				dispatch_source_cancel(source);
			}
		}
	};

	const auto state = std::make_shared<Source>();
	state->handler = std::move(onWake);
	constexpr uintptr_t unusedHandle = 0;
	constexpr unsigned long unusedMask = 0;
	state->source = dispatch_source_create(
		DISPATCH_SOURCE_TYPE_DATA_ADD,
		unusedHandle,
		unusedMask,
		dispatch_get_main_queue()
	);
	if (state->source == nullptr) {
		return std::unexpected("Failed to create the native mailbox wakeup source");
	}
	const std::weak_ptr<Source> weak = state;
	dispatch_source_set_event_handler(state->source, ^{
	  const auto current = weak.lock();
	  if (!current || current->closed.load(std::memory_order_acquire)) {
		  return;
	  }
	  try {
		  current->handler();
	  } catch (...) {
		  std::cerr << "Stana: uncaught mailbox wakeup exception\n";
		  stop();
	  }
	});
	dispatch_activate(state->source);
	return MailboxWakeup{
		.notify = [state] -> void {
			if (!state->closed.load(std::memory_order_acquire) && state->source != nullptr) {
				constexpr unsigned long wakeupIncrement = 1;
				dispatch_source_merge_data(state->source, wakeupIncrement);
			}
		},
		.close = [state] -> void {
			state->closed.store(true, std::memory_order_release);
			if (state->source != nullptr) {
				dispatch_source_cancel(state->source);
			}
		}
	};
}

void postAfter(std::chrono::milliseconds delay, std::function<void()> callback) {
	if (delay.count() < 0) {
		throw std::invalid_argument("Stana delay cannot be negative");
	}
	constexpr std::int64_t nanosPerMillisecond = 1000000;
	if (delay.count() > std::numeric_limits<std::int64_t>::max() / nanosPerMillisecond) {
		throw std::invalid_argument("Stana delay exceeds native range");
	}
	const auto action = std::move(callback);
	dispatch_after(
		dispatch_time(DISPATCH_TIME_NOW, delay.count() * nanosPerMillisecond),
		dispatch_get_main_queue(),
		^{
		  action();
		}
	);
}

void stop() {
	stopRequested().store(true, std::memory_order_release);
	if (NSApp == nil) {
		return;
	}
	[NSApp stop:nil];
	// AppKit's run loop may be waiting for an event; waking it lets stop take effect.
	NSEvent *const wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
											 location:NSZeroPoint
										modifierFlags:0
											timestamp:0
										 windowNumber:0
											  context:nil
											  subtype:0
												data1:0
												data2:0];
	if (wake != nil) {
		[NSApp postEvent:wake atStart:YES];
	}
}

auto run(std::vector<Window> windows) -> std::expected<int, std::string> {
	if (![NSThread isMainThread]) {
		return std::unexpected("Application::run requires the main thread");
	}
	@autoreleasepool {
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
		id<MTLDevice> const device = MTLCreateSystemDefaultDevice();
		if (device == nil) {
			return std::unexpected("Native rendering requires an available Metal device");
		}
		const auto resources = gpu::DeviceResources::get(device);
		if (!resources) {
			return std::unexpected(resources.error().message);
		}

		// Explicit C++ startup has no NSApplicationMain/nib lifecycle. Complete
		// registration before exposing layer attachments or admitting drawables.
		[NSApp finishLaunching];
		[NSApp activate];
		std::vector<std::shared_ptr<WindowState>> states;
		NSMutableArray<NSWindow *> *const nativeWindows = [NSMutableArray array];
		NSMutableArray<STANAWindowDelegate *> *const delegates = [NSMutableArray array];
		for (auto &configuration : windows) {
			auto state = std::make_shared<WindowState>();
			state->callbacks = std::move(configuration);
			states.push_back(state);
			activeWindows().emplace_back(state);
			state->traceRendering =
				[NSProcessInfo.processInfo.environment objectForKey:@"STANA_TRACE_NATIVE"] != nil;
			state->device = device;
			state->queue = (*resources)->queue;
			const auto compositor = gpu::Compositor::create(*resources, state->accounting);
			if (!compositor) {
				return std::unexpected(compositor.error().message);
			}
			state->compositor = *compositor;
			const auto weak = std::weak_ptr{state};
			state->compositor->setWake([weak] -> void {
				if (const auto owner = weak.lock(); owner && !owner->isClosed) {
					requestFrame(owner);
				}
			});
			state->compositor->setDiagnostic(state->callbacks.gpuError);
			state->compositor->setExecutionFailure([weak](const gpu::Failure &failure) -> void {
				if (const auto owner = weak.lock(); owner && !owner->isClosed) {
					rendererFailure(owner, failure);
				}
			});

			const NSRect frame = NSMakeRect(
				initialWindowOffset,
				initialWindowOffset,
				state->callbacks.width,
				state->callbacks.height
			);
			NSWindow *const window = [[NSWindow alloc]
				initWithContentRect:frame
						  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
				NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
							backing:NSBackingStoreBuffered
							  defer:NO];
			window.releasedWhenClosed = NO;
			window.title = nativeString(state->callbacks.title);
			STANAView *const view = [[STANAView alloc]
				initWithFrame:NSMakeRect(0, 0, frame.size.width, frame.size.height)];
			view->state_ = state;
			state->view = view;
			view.wantsLayer = YES;

			window.contentView = view;
			STANAWindowDelegate *const delegate = [[STANAWindowDelegate alloc] init];
			delegate->state_ = state;
			window.delegate = delegate;
			[NSNotificationCenter.defaultCenter
				addObserver:delegate
				   selector:@selector(screenParametersChanged:)
					   name:NSApplicationDidChangeScreenParametersNotification
					 object:nil];
			[delegates addObject:delegate];
			[nativeWindows addObject:window];
			publishAppearance(state);
			const auto weakState = std::weak_ptr{state};
			state->callbacks.ready([weakState] -> void {
				if (const auto current = weakState.lock(); current && !current->isClosed) {
					requestFrame(current);
				}
			});
			[window center];
			[window makeKeyAndOrderFront:nil];
			[window makeFirstResponder:view];
			installSurface(state);
			requestFrame(state);
		}
		if (windows.empty()) {
			stopRequested().store(false, std::memory_order_release);
			return 0;
		}
		[NSApp activate];
		// Honor Application::stop() issued during prepare/onMount, before this loop starts.
		if (!stopRequested().exchange(false, std::memory_order_acq_rel)) {
			[NSApp run];
		}
		for (NSUInteger index = 0; index < nativeWindows.count; ++index) {
			[[nativeWindows objectAtIndex:index] close];
		}
		// windowWillClose of the last window also requests stop; drop that for the next run().
		stopRequested().store(false, std::memory_order_release);
		for (const auto &state : states) {
			if (!state->failureMessage.empty()) {
				return std::unexpected(state->failureMessage);
			}
		}
		return 0;
	}
}
} // namespace stana::native
