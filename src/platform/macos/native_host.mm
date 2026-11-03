#include "../native_contracts.hpp"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace stana::native {
/// @brief Balances a Create/Copy-rule Core Foundation resource within a native operation.
/// Used for bitmap contexts, paths, colorspaces and shaped Core Text lines; callers
/// never transfer these native handles into exported C++ modules.
template <typename Reference> class CoreOwner {
  public:
	explicit CoreOwner(Reference reference) : reference_(reference) {}

	CoreOwner(const CoreOwner &) = delete;
	CoreOwner(CoreOwner &&) = delete;
	auto operator=(const CoreOwner &) -> CoreOwner & = delete;
	auto operator=(CoreOwner &&) -> CoreOwner & = delete;

	~CoreOwner() {
		if (reference_ != nullptr) {
			CFRelease(reference_);
		}
	}

	[[nodiscard]] auto get() const noexcept -> Reference { return reference_; }

  private:
	Reference reference_{}; ///< Owning Create/Copy-rule reference, released at scope exit.
};

/// @brief Owns one native host's callbacks and Metal resources on the main thread.
/// The application owns this state until close; in-flight frame observers hold only
/// weak references, so late native callbacks cannot resurrect a closed window.
struct WindowState {
	std::uint64_t presentationEpoch = 0; ///< Invalidates evidence on geometry or attachment change.
	std::uint64_t attachmentGeneration = 1; ///< Current native drawable-queue generation.
	std::uint64_t retryRevision = 0;	 ///< Scene which consumed its bounded presentation retry.
	std::uint64_t submittedRevision = 0; ///< Last scene encoded for GPU submission.

	Window callbacks{};			///< Owning portable-engine callbacks supplied by the C++ session.
	id<MTLDevice> device = nil; ///< Native GPU used for full-scene presentation.
	id<MTLCommandQueue> queue = nil; ///< Serial GPU command submission queue.
	CAMetalLayer *layer = nil;		 ///< Drawable queue attached to this view's backing layer.
	__weak NSView *view = nil; ///< Non-owning invalidation target; native hierarchy owns the view.
	std::uint64_t surfaceEpoch = 1; ///< Changes when viewport/scale invalidates prepared geometry.
	std::uint64_t confirmedRevision =
		0; ///< Last scene with matching GPU and positive display evidence.
	std::uint64_t confirmedSequence = 0; ///< Last exact attempt, including same-scene retries.
	bool traceRendering =
		false; ///< Opt-in native diagnostics selected at explicit application startup.
	bool isFrameQueued = false;	  ///< Coalesces publication demand into one main-queue repaint.
	std::string failureMessage{}; ///< Terminal native render failure returned by run().
	bool isClosed = false; ///< Makes callbacks and further rendering ineligible after closure.

	[[nodiscard]] auto allowsPointer() const -> bool { return !isClosed && confirmedRevision != 0; }

	/// @brief Invalidates evidence immediately, before any replacement frame is submitted.
	void resetPresentation() {
		if (presentationEpoch == std::numeric_limits<std::uint64_t>::max()) {
			throw std::overflow_error("Native presentation epoch exhausted");
		}
		callbacks.resetPresentation(++presentationEpoch);
	}
};

void requestFrame(const std::shared_ptr<WindowState> &state);
void installSurface(const std::shared_ptr<WindowState> &state);
void publishAppearance(const std::shared_ptr<WindowState> &state);

namespace {
constexpr std::size_t bytesPerPixel = 4;
constexpr std::size_t bitsPerComponent = 8;
constexpr std::size_t maximumDimension = 16384;
constexpr std::size_t maximumBitmapBytes = std::size_t{256} * 1024 * 1024;
constexpr double fallbackLineHeight = 1.4;
constexpr double initialWindowOffset = 40;

/// @brief Weak native-host registry for diagnostics; explicit run() owns the actual hosts.
[[nodiscard]] auto activeWindows() -> std::vector<std::weak_ptr<WindowState>> & {
	static thread_local std::vector<std::weak_ptr<WindowState>> windows;
	return windows;
}

/// @brief Constructs an NSString without treating embedded NUL bytes as terminators.
[[nodiscard]] auto nativeString(const std::string &text) -> NSString * {
	NSString *const value = [[NSString alloc] initWithBytes:text.data()
													 length:text.size()
												   encoding:NSUTF8StringEncoding];
	if (value == nil) {
		throw std::invalid_argument("Stana text must be valid UTF-8");
	}
	return value;
}

/// @brief Shapes text with native system typography shared by measurement and drawing.
[[nodiscard]] auto attributedText(const Command &command) -> NSAttributedString * {
	NSFont *const font = [NSFont systemFontOfSize:command.fontSize weight:command.fontWeight];
	NSColor *const color = [NSColor colorWithSRGBRed:command.red
											   green:command.green
												blue:command.blue
											   alpha:command.alpha];
	return [[NSAttributedString alloc]
		initWithString:nativeString(command.text)
			attributes:@{NSFontAttributeName : font, NSForegroundColorAttributeName : color}];
}

/// @brief Applies one owning display-list command to a worker-local bitmap context.
void paintCommand(CGContextRef context, const Command &command) {
	const CGRect bounds = CGRectMake(command.x, command.y, command.width, command.height);
	const CoreOwner path{
		CGPathCreateWithRoundedRect(bounds, command.radius, command.radius, nullptr)
	};
	switch (command.operation) {
	case PaintOperation::fill:
		CGContextSetRGBFillColor(context, command.red, command.green, command.blue, command.alpha);
		CGContextAddPath(context, path.get());
		CGContextFillPath(context);
		break;
	case PaintOperation::stroke:
		CGContextSetRGBStrokeColor(
			context,
			command.red,
			command.green,
			command.blue,
			command.alpha
		);
		CGContextSetLineWidth(context, command.lineWidth);
		CGContextAddPath(context, path.get());
		CGContextStrokePath(context);
		break;
	case PaintOperation::line:
		CGContextSetRGBStrokeColor(
			context,
			command.red,
			command.green,
			command.blue,
			command.alpha
		);
		CGContextSetLineWidth(context, command.lineWidth);
		CGContextMoveToPoint(context, command.x, command.y);
		CGContextAddLineToPoint(context, command.x + command.width, command.y + command.height);
		CGContextStrokePath(context);
		break;
	case PaintOperation::pushClip:
		CGContextSaveGState(context);
		CGContextAddPath(context, path.get());
		CGContextClip(context);
		break;
	case PaintOperation::popClip:
		CGContextRestoreGState(context);
		break;
	case PaintOperation::text: {
		const auto text = attributedText(command);
		const CoreOwner line{
			CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)text)
		};
		CGFloat ascent = 0;
		CTLineGetTypographicBounds(line.get(), &ascent, nullptr, nullptr);
		CGContextSaveGState(context);
		CGContextTranslateCTM(context, command.x, command.y + ascent);
		CGContextScaleCTM(context, 1, -1);
		CGContextSetTextPosition(context, 0, 0);
		CTLineDraw(line.get(), context);
		CGContextRestoreGState(context);
		break;
	}
	}
}

/// @brief Rasterizes a complete current scene and submits it to a fresh Metal drawable.
/// The complete bitmap is intentionally the initial correctness path, not a claim
/// of tiled/incremental renderer parity. Resource dimensions are checked before allocation.
void render(const std::shared_ptr<WindowState> &state) {
	if (state->isClosed) {
		return;
	}
	const auto scene = state->callbacks.scene();
	if (!scene) {
		return;
	}
	NSView *const view = state->view;
	const double scale = view.window.backingScaleFactor;
	const double width = std::ceil(view.bounds.size.width * scale);
	const double height = std::ceil(view.bounds.size.height * scale);
	if (width < 1 || height < 1) {
		return;
	}
	if (!std::isfinite(width) || !std::isfinite(height) ||
		width > static_cast<double>(maximumDimension) ||
		height > static_cast<double>(maximumDimension)) {
		throw std::runtime_error("Stana viewport exceeds native raster limits");
	}
	const auto pixelWidth = static_cast<std::size_t>(width);
	const auto pixelHeight = static_cast<std::size_t>(height);
	const auto bytesPerRow = pixelWidth * bytesPerPixel;
	if (pixelHeight > maximumBitmapBytes / bytesPerRow) {
		throw std::runtime_error("Stana bitmap budget exceeded");
	}
	std::vector<std::byte> pixels(bytesPerRow * pixelHeight);
	const CoreOwner colorspace{CGColorSpaceCreateWithName(kCGColorSpaceSRGB)};
	const auto bitmapInfo = static_cast<CGBitmapInfo>(
		static_cast<std::uint32_t>(kCGImageAlphaPremultipliedFirst) |
		static_cast<std::uint32_t>(kCGBitmapByteOrder32Little)
	);
	const CoreOwner context{CGBitmapContextCreate(
		pixels.data(),
		pixelWidth,
		pixelHeight,
		bitsPerComponent,
		bytesPerRow,
		colorspace.get(),
		bitmapInfo
	)};
	if (context.get() == nullptr) {
		throw std::runtime_error("Unable to allocate native bitmap context");
	}
	CGContextSetRGBFillColor(context.get(), scene->red, scene->green, scene->blue, 1);
	CGContextFillRect(context.get(), CGRectMake(0, 0, width, height));
	CGContextTranslateCTM(context.get(), 0, height);
	CGContextScaleCTM(context.get(), scale, -scale);
	for (const auto &command : scene->commands) {
		paintCommand(context.get(), command);
	}
	state->layer.frame = NSRectToCGRect(view.bounds);
	state->layer.drawableSize = CGSizeMake(width, height);
	state->layer.contentsScale = scale;
	id<CAMetalDrawable> const drawable = [state->layer nextDrawable];
	if (drawable == nil) {
		return;
	}
	MTLTextureDescriptor *const descriptor =
		[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
														   width:pixelWidth
														  height:pixelHeight
													   mipmapped:NO];
	descriptor.storageMode = MTLStorageModeShared;
	id<MTLTexture> const texture = [state->device newTextureWithDescriptor:descriptor];
	if (texture == nil) {
		throw std::runtime_error("Unable to allocate native scene texture");
	}
	[texture replaceRegion:MTLRegionMake2D(0, 0, pixelWidth, pixelHeight)
			   mipmapLevel:0
				 withBytes:pixels.data()
			   bytesPerRow:bytesPerRow];
	id<MTLCommandBuffer> const buffer = [state->queue commandBuffer];
	id<MTLBlitCommandEncoder> const encoder = [buffer blitCommandEncoder];
	if (buffer == nil || encoder == nil) {
		throw std::runtime_error("Unable to allocate native GPU command encoding");
	}
	[encoder copyFromTexture:texture
				 sourceSlice:0
				 sourceLevel:0
				sourceOrigin:MTLOriginMake(0, 0, 0)
				  sourceSize:MTLSizeMake(pixelWidth, pixelHeight, 1)
				   toTexture:drawable.texture
			destinationSlice:0
			destinationLevel:0
		   destinationOrigin:MTLOriginMake(0, 0, 0)];
	[encoder endEncoding];
	const auto admitted =
		state->callbacks.beginPresentation(state->presentationEpoch, scene->revision);
	if (!admitted) {
		throw std::runtime_error(admitted.error());
	}
	const auto attempt = *admitted;
	const auto weakState = std::weak_ptr{state};
	const auto epoch = state->surfaceEpoch;
	const auto revision = scene->revision;
	const bool tracing = state->traceRendering;
	const auto attachment = state->attachmentGeneration;
	[buffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
	  const bool success = completed.status == MTLCommandBufferStatusCompleted;
	  if (tracing) {
		  std::cerr << "GPU epoch=" << epoch << " revision=" << revision
					<< " status=" << static_cast<long>(completed.status) << '\n';
	  }
	  // Native callbacks only transport owning values. All evidence mutation and
	  // document publication happen on the UI executor, just as in TestHost.
	  dispatch_async(dispatch_get_main_queue(), ^{
		if (const auto current = weakState.lock();
			current && !current->isClosed && current->presentationEpoch == attempt.epoch) {
			if (current->callbacks.gpuCompleted(attempt, success)) {
				current->confirmedRevision = std::max(current->confirmedRevision, revision);
				current->confirmedSequence = attempt.sequence;
			}
		}
	  });
	}];
	[drawable addPresentedHandler:^(id<MTLDrawable> presented) {
	  const double timestamp = presented.presentedTime;
	  if (tracing) {
		  std::cerr << "Presentation epoch=" << epoch << " revision=" << revision
					<< " time=" << timestamp << '\n';
	  }
	  dispatch_async(dispatch_get_main_queue(), ^{
		if (const auto current = weakState.lock();
			current && !current->isClosed && current->presentationEpoch == attempt.epoch) {
			if (current->callbacks.drawablePresented(attempt, timestamp)) {
				current->confirmedRevision = std::max(current->confirmedRevision, revision);
				current->confirmedSequence = attempt.sequence;
			}
		}
	  });
	}];
	[buffer presentDrawable:drawable];
	state->submittedRevision = revision;
	[buffer commit];
	constexpr auto confirmationDeadline = std::chrono::milliseconds{1000};
	postAfter(confirmationDeadline, [weakState, epoch, revision, attachment, attempt] -> void {
		if (const auto current = weakState.lock(); current && !current->isClosed &&
			current->surfaceEpoch == epoch && current->attachmentGeneration == attachment) {
			current->callbacks.abandonPresentation(attempt);
			if (current->confirmedSequence < attempt.sequence &&
				current->retryRevision != revision) {
				current->retryRevision = revision;
				installSurface(current);
				requestFrame(current);
			}
		}
	});
}
} // namespace

/// @brief Replaces a native drawable queue after attachment or an unconfirmed attempt.
/// Queue generation is separate from logical geometry/surface epoch so stale
/// callbacks cannot confirm a replacement while prepared scene content stays valid.
void installSurface(const std::shared_ptr<WindowState> &state) {
	if (state->attachmentGeneration == std::numeric_limits<std::uint64_t>::max()) {
		throw std::overflow_error("Native attachment generation exhausted");
	}
	++state->attachmentGeneration;
	state->resetPresentation();
	CAMetalLayer *const layer = [CAMetalLayer layer];
	layer.device = state->device;
	layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
	layer.framebufferOnly = NO;
	layer.opaque = YES;
	const CoreOwner colorspace{CGColorSpaceCreateWithName(kCGColorSpaceSRGB)};
	layer.colorspace = colorspace.get();
	NSView *const view = state->view;
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	layer.frame = NSRectToCGRect(view.bounds);
	view.wantsLayer = YES;
	view.layer = layer;
	state->layer = layer;
	[CATransaction commit];
	[CATransaction flush];
}

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

/// @brief Coalesces scene publication and resize demand into a hosted-layer repaint.
/// AppKit does not drive drawRect for a layer-hosting view. Dispatching explicitly
/// also ensures the initial drawable is requested after attachment/show completes.
void requestFrame(const std::shared_ptr<WindowState> &state) {
	if (state->isClosed || state->isFrameQueued) {
		return;
	}
	state->isFrameQueued = true;
	const auto weakState = std::weak_ptr{state};
	dispatch_async(dispatch_get_main_queue(), ^{
	  if (const auto current = weakState.lock(); current && !current->isClosed) {
		  current->isFrameQueued = false;
		  try {
			  render(current);
		  } catch (const std::exception &error) {
			  current->failureMessage = error.what();
			  stop();
		  }
	  }
	});
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
		++state_->surfaceEpoch;
		state_->resetPresentation();
		state_->callbacks.resize(size.width, size.height);
		stana::native::requestFrame(state_);
	}
}

- (void)viewDidChangeBackingProperties {
	[super viewDidChangeBackingProperties];
	if (state_ && !state_->isClosed &&
		std::is_neq(self.window.backingScaleFactor <=> state_->layer.contentsScale)) {
		++state_->surfaceEpoch;
		state_->resetPresentation();
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

- (void)windowDidChangeOcclusionState:(NSNotification *)notification {
	static_cast<void>(notification);
	if (state_ && !state_->isClosed) {
		stana::native::requestFrame(state_);
	}
}

- (void)windowWillClose:(NSNotification *)notification {
	NSWindow *const closingWindow = notification.object;
	if (state_ && !std::exchange(state_->isClosed, true)) {
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
			result.push_back(
				{.surfaceEpoch = state->surfaceEpoch,
					.submittedRevision = state->submittedRevision,
					.confirmedRevision = state->confirmedRevision,
					.failure = state->failureMessage}
			);
		}
	}
	return result;
}

auto measureText(const std::string &text, FontRequest font, double maximumWidth)
	-> std::pair<double, double> {
	@autoreleasepool {
		const auto attributed =
			attributedText({.text = text, .fontSize = font.size, .fontWeight = font.weight});
		const CoreOwner line{
			CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)attributed)
		};
		CGFloat ascent = 0;
		CGFloat descent = 0;
		CGFloat leading = 0;
		const double width = CTLineGetTypographicBounds(line.get(), &ascent, &descent, &leading);
		return {
			std::min(maximumWidth, std::ceil(width)),
			std::max(std::ceil(ascent + descent + leading), font.size * fallbackLineHeight)
		};
	}
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
			return std::unexpected("No Metal device is available; no silent CPU fallback");
		}
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
			state->queue = [device newCommandQueue];
			state->layer = [CAMetalLayer layer];
			state->layer.device = device;
			state->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
			state->layer.framebufferOnly = NO;
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
			view.layer = state->layer;
			window.contentView = view;
			STANAWindowDelegate *const delegate = [[STANAWindowDelegate alloc] init];
			delegate->state_ = state;
			window.delegate = delegate;
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
			return 0;
		}
		[NSApp activate];
		[NSApp run];
		for (NSUInteger index = 0; index < nativeWindows.count; ++index) {
			[[nativeWindows objectAtIndex:index] close];
		}
		for (const auto &state : states) {
			if (!state->failureMessage.empty()) {
				return std::unexpected(state->failureMessage);
			}
		}
		return 0;
	}
}
} // namespace stana::native
