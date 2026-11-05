#include "window_fixture.hpp"
#import <AppKit/AppKit.h>
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
