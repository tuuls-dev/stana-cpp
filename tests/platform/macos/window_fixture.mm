#include "window_fixture.hpp"
#include "../../../src/platform/macos/host_view.hpp"
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include <stdexcept>

namespace stana::testing::fixtures::platform::macos {
namespace {
[[nodiscard]] auto findWindow(const std::string &title) -> NSWindow * {
	for (NSUInteger index = 0; index < NSApp.windows.count; ++index) {
		NSWindow *const window = [NSApp.windows objectAtIndex:index];
		const char *const name = window.title.UTF8String;
		if (name != nullptr && title == name) {
			return window;
		}
	}
	throw std::runtime_error("Fixture window is not attached: " + title);
}

[[nodiscard]] auto requireView(const std::string &title) -> STANAView * {
	NSView *const view = findWindow(title).contentView;
	if (![view isKindOfClass:STANAView.class] ||
		static_cast<STANAView *>(view)->state_ == nullptr) {
		throw std::runtime_error("Fixture view is not a Stana host: " + title);
	}
	return static_cast<STANAView *>(view);
}
} // namespace

auto manualFacilities() -> bool {
	return [NSProcessInfo.processInfo.environment objectForKey:@"STANA_MANUAL_FACILITIES"] != nil;
}

auto surfaceIsOpaque(const std::string &title) -> bool {
	NSWindow *const window = findWindow(title);
	if (window.opaque || window.contentView.layer.opaque) {
		return true;
	}
	NSArray<CALayer *> *const layers = window.contentView.layer.sublayers;
	for (NSUInteger index = 0; index < layers.count; ++index) {
		if ([layers objectAtIndex:index].opaque) {
			return true;
		}
	}
	return false;
}

auto windowOpacity(const std::string &title) -> double {
	return findWindow(title).alphaValue;
}

void resizeWindow(const std::string &title, PhysicalViewport size) {
	NSWindow *const window = findWindow(title);
	[window setContentSize:NSMakeSize(
							   size.width / window.backingScaleFactor,
							   size.height / window.backingScaleFactor
						   )];
}

void setPresentDelay(const std::string &title, std::chrono::milliseconds delay) {
	stana::platform::detail::setPresentDelay(requireView(title)->state_, delay);
}

auto surfaceContinuity(const std::string &title) -> bool {
	const auto &state = *requireView(title)->state_;
	if (state.hostLayer == nil) {
		return state.confirmedRevision == 0;
	}
	if (state.hostLayer.sublayers.count > 2) {
		return false;
	}
	if (state.confirmedRevision == 0 && !state.contentPresented) {
		return state.fallbackLayer == nil;
	}
	if (state.fallbackLayer != nil) {
		return state.fallbackLayer.superlayer == state.hostLayer;
	}
	return state.contentPresented && state.layer != nil &&
		state.layer.superlayer == state.hostLayer;
}

auto surfaceSettled(const std::string &title) -> bool {
	const auto &state = *requireView(title)->state_;
	return surfaceContinuity(title) && state.contentPresented && state.fallbackLayer == nil &&
		state.layer != nil;
}

auto fallbackCoversContent(const std::string &title) -> bool {
	const auto &state = *requireView(title)->state_;
	return state.fallbackLayer != nil && state.fallbackLayer.superlayer == state.hostLayer &&
		!state.contentPresented;
}

auto replacementBackgroundReady(const std::string &title) -> bool {
	const auto &state = *requireView(title)->state_;
	return state.hostLayer != nil && state.hostLayer.backgroundColor != nil &&
		state.layer != nil && state.layer.backgroundColor != nil;
}

auto retainedHostLayers(const std::string &title) -> std::size_t {
	CALayer *const host = requireView(title)->state_->hostLayer;
	return host == nil ? 0 : host.sublayers.count;
}

auto compositorFullResets(const std::string &title) -> std::uint64_t {
	return requireView(title)->state_->compositor->fullResets();
}

auto compositorResizeInvalidations(const std::string &title) -> std::uint64_t {
	return requireView(title)->state_->compositor->resizeInvalidations();
}

void staleBackingScale(const std::string &title) {
	STANAView *const view = requireView(title);
	if (view.window == nil) {
		throw std::runtime_error("Fixture window has no screen scale");
	}
	view->state_->scale = view.window.backingScaleFactor + 1.0;
	[view viewDidChangeBackingProperties];
}

void showWindow(const std::string &title, bool visible) {
	NSWindow *const window = findWindow(title);
	if (visible) {
		[window makeKeyAndOrderFront:nil];
	} else {
		[window orderOut:nil];
	}
}

void replaceAttachment(const std::string &title) {
	NSWindow *const window = findWindow(title);
	NSView *const original = window.contentView;
	if (original == nil) {
		throw std::runtime_error("Missing original view");
	}
	window.contentView = [[NSView alloc] initWithFrame:original.frame];
	window.contentView = original;
}

auto activateToolbar(const std::string &title) -> bool {
	NSToolbarItem *const item = findWindow(title).toolbar.items.firstObject;
	if (item == nil || item.action == nullptr || item.target == nil) {
		throw std::runtime_error("Missing toolbar activation target");
	}
	static __weak NSToolbarItem *previous = nil;
	const bool retained = previous == nil || previous == item;
	previous = item;
	[NSApp sendAction:item.action to:item.target from:item];
	return retained;
}

void pressWindow(const std::string &title) {
	NSWindow *const window = findWindow(title);
	const NSPoint location = [window.contentView convertPoint:NSMakePoint(1, 1) toView:nil];
	NSEvent *const event = [NSEvent mouseEventWithType:NSEventTypeRightMouseDown
											  location:location
										 modifierFlags:0
											 timestamp:NSProcessInfo.processInfo.systemUptime
										  windowNumber:window.windowNumber
											   context:nil
										   eventNumber:0
											clickCount:1
											  pressure:1];
	if (event == nil) {
		throw std::runtime_error("Unable to create fixture pointer event");
	}
	[NSApp postEvent:event atStart:NO];
}

void dismissWindowMenuAfter(std::chrono::milliseconds delay) {
	// A main-queue callback cannot reenter that queue while AppKit tracks a menu.
	// A common-mode run-loop timer keeps this fixture active during native tracking.
	NSTimer *const timer =
		[NSTimer timerWithTimeInterval:std::chrono::duration<double>(delay).count()
							   repeats:NO
								 block:^(NSTimer *) {
								   [NSApp.windowsMenu cancelTracking];
								 }];
	[NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
}

void cancelPrintPanelAfter(std::chrono::milliseconds delay) {
	NSTimer *const timer =
		[NSTimer timerWithTimeInterval:std::chrono::duration<double>(delay).count()
							   repeats:NO
								 block:^(NSTimer *) {
								   if (NSApp.modalWindow != nil) {
									   [NSApp stopModalWithCode:NSModalResponseCancel];
								   }
								 }];
	[NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
}

void closeWindow(const std::string &title) {
	[findWindow(title) close];
}

auto resultPath() -> std::string {
	NSString *const path =
		[NSProcessInfo.processInfo.environment objectForKey:@"STANA_NATIVE_RESULT_FILE"];
	const char *const value = path.UTF8String;
	return value == nullptr ? std::string{} : std::string{value};
}

auto processIdentity() -> std::int32_t {
	return NSProcessInfo.processInfo.processIdentifier;
}
} // namespace stana::testing::fixtures::platform::macos
