#include "window_fixture.hpp"
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
