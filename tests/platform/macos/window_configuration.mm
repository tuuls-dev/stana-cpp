#include "../../../src/platform/macos/window_configuration.hpp"
#include "../../../src/platform/macos/presentation.hpp"
#include "window_fixture.hpp"
#import <AppKit/AppKit.h>
#include <cmath>
#include <compare>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

@interface STANATestDragInfo : NSObject <NSDraggingInfo>
@property(strong) NSWindow *draggingDestinationWindow;
@property NSDragOperation draggingSourceOperationMask;
@property NSPoint draggingLocation;
@property NSPoint draggedImageLocation;
@property(strong) NSImage *draggedImage;
@property(strong) NSPasteboard *draggingPasteboard;
@property(strong) id draggingSource;
@property NSInteger draggingSequenceNumber;
@property NSDraggingFormation draggingFormation;
@property BOOL animatesToDestination;
@property NSInteger numberOfValidItemsForDrop;
@property NSSpringLoadingHighlight springLoadingHighlight;
@end
@implementation STANATestDragInfo

- (void)slideDraggedImageTo:(NSPoint)point {
	static_cast<void>(point);
}

- (NSArray<NSString *> *)namesOfPromisedFilesDroppedAtDestination:(NSURL *)destination {
	static_cast<void>(destination);
	return nil;
}

- (void)resetSpringLoading {
}

- (void)enumerateDraggingItemsWithOptions:(NSDraggingItemEnumerationOptions)options
								  forView:(NSView *)view
								  classes:(NSArray<Class> *)classes
							searchOptions:(NSDictionary<NSPasteboardReadingOptionKey, id> *)search
							   usingBlock:(void (^)(NSDraggingItem *, NSInteger, BOOL *))block {
	static_cast<void>(options);
	static_cast<void>(view);
	static_cast<void>(classes);
	static_cast<void>(search);
	static_cast<void>(block);
}

@end
@interface STANATestFlippedView : NSView
@end
@implementation STANATestFlippedView

- (BOOL)isFlipped {
	return YES;
}

@end

namespace stana::testing::fixtures::platform::macos {
namespace {
using namespace ::stana::platform::detail;
inline constexpr double initialWidth = 320;
inline constexpr double initialHeight = 240;
inline constexpr double originX = 120;
inline constexpr double originY = 140;
inline constexpr double resizedWidth = 400;
inline constexpr double resizedHeight = 300;
inline constexpr double minimumDimension = 100;
inline constexpr double maximumDimension = 600;
inline constexpr double resizeStep = 10;

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void configurationContracts() {
	WindowConfiguration configuration;
	configuration.title = "Stana configuration contracts";
	configuration.isVisible = false;
	configuration.contentWidth = initialWidth;
	configuration.contentHeight = initialHeight;
	configuration.placement = NativePlacementKind::explicitOrigin;
	configuration.originX = originX;
	configuration.originY = originY;
	NSWindow *const window = createConfiguredWindow(configuration);
	require(window != nil, "native window created");
	AppliedWindowConfiguration applied;
	applyWindowConfiguration(window, applied, configuration);
	const auto initial = observeNativeWindow(window);
	require(
		std::is_eq(initial.contentX <=> configuration.originX) &&
			std::is_eq(initial.contentY <=> configuration.originY),
		"global top-left transform"
	);
	configuration.contentWidth = resizedWidth;
	configuration.contentHeight = resizedHeight;
	applyWindowConfiguration(window, applied, configuration);
	const auto resized = observeNativeWindow(window);
	require(
		std::is_eq(initial.contentX <=> resized.contentX) &&
			std::is_eq(initial.contentY <=> resized.contentY),
		"size-only update preserves content origin"
	);
	configuration.minimumContentSize =
		LogicalSize{.width = minimumDimension, .height = minimumDimension};
	configuration.maximumContentSize =
		LogicalSize{.width = maximumDimension, .height = maximumDimension};
	configuration.aspectRatio = 2;
	configuration.resizeIncrement = LogicalSize{.width = resizeStep, .height = resizeStep};
	configuration.macos.present = true;
	configuration.macos.fullSizeContentView = true;
	configuration.macos.hideTitleBarControls = true;
	configuration.macos.tabbingIdentifier = "group";
	configuration.macos.nativeLevel = MacosWindowLevel::statusBar;
	configuration.macos.spacesBehavior = 2;
	configuration.macos.fullscreenBehavior = 2;
	configuration.darkTheme = true;
	configuration.document =
		NativeDocument{.representedPath = "/tmp/stana-document", .edited = true};
	applyWindowConfiguration(window, applied, configuration);
	require(window.level == NSStatusWindowLevel, "explicit native level");
	require(window.documentEdited && window.representedURL != nil, "document applied");
	require(
		(window.collectionBehavior & NSWindowCollectionBehaviorCanJoinAllSpaces) == 0,
		"spaces policies are exclusive"
	);
	configuration.minimumContentSize.reset();
	configuration.maximumContentSize.reset();
	configuration.aspectRatio.reset();
	configuration.resizeIncrement.reset();
	configuration.macos.fullSizeContentView = false;
	configuration.macos.hideTitleBarControls = false;
	configuration.macos.tabbingIdentifier.reset();
	configuration.macos.nativeLevel.reset();
	configuration.level = NativeWindowLevel::floating;
	configuration.darkTheme.reset();
	configuration.document.reset();
	applyWindowConfiguration(window, applied, configuration);
	require(NSEqualSizes(window.contentMinSize, NSZeroSize), "minimum cleared");
	require(window.contentMaxSize.width > maximumDimension, "maximum cleared");
	require(NSEqualSizes(window.contentAspectRatio, NSZeroSize), "aspect cleared");
	require(NSEqualSizes(window.contentResizeIncrements, NSMakeSize(1, 1)), "increment cleared");
	require(
		(window.styleMask & NSWindowStyleMaskFullSizeContentView) == 0,
		"full-size style reversed"
	);
	require(![window standardWindowButton:NSWindowCloseButton].hidden, "controls restored");
	require(
		window.level == NSFloatingWindowLevel && window.tabbingIdentifier.length == 0,
		"portable level and tabbing restored"
	);
	require(
		window.appearance == nil && !window.documentEdited && window.representedURL == nil,
		"theme and document reset"
	);
	configuration.level = NativeWindowLevel::normal;
	configuration.style.resizable = false;
	configuration.style.titled = false;
	configuration.acceptsFocus = false;
	configuration.isEnabled = false;
	applyWindowConfiguration(window, applied, configuration);
	require(window.level == NSNormalWindowLevel, "floating level reversed");
	require(
		(window.styleMask & (NSWindowStyleMaskResizable | NSWindowStyleMaskTitled)) == 0,
		"chrome updated live"
	);
	require(
		!window.canBecomeKeyWindow && window.ignoresMouseEvents,
		"focus and enabled policy applied"
	);
	configuration.isEnabled = true;
	configuration.acceptsFocus = true;
	applyWindowConfiguration(window, applied, configuration);
	require(
		window.canBecomeKeyWindow && !window.ignoresMouseEvents,
		"interaction policies reversed"
	);
	int activated = 0;
	configuration.toolbar = NativeToolbar{
		.identifier = "tools",
		.items = {{.identifier = "save", .label = "Save"}},
		.activate = [&](const std::string &identifier) -> void {
			require(identifier == "save", "toolbar identity");
			++activated;
		}
	};
	applyWindowConfiguration(window, applied, configuration);
	require(window.toolbar.items.count == 1, "ordered toolbar installed");
	NSToolbarItem *const item = window.toolbar.items.firstObject;
	if (item == nil || item.target == nil || item.action == nullptr) {
		throw std::runtime_error("Missing toolbar action target");
	}
	[NSApp sendAction:item.action to:item.target from:item];
	require(activated == 1, "toolbar activation delivered");
	configuration.toolbar.reset();
	applyWindowConfiguration(window, applied, configuration);
	require(window.toolbar == nil, "toolbar removed");
	configuration.screenSelection = 2;
	configuration.screenId = std::numeric_limits<std::uint64_t>::max();
	require(
		!validateNativeConfiguration(configuration) && createConfiguredWindow(configuration) == nil,
		"stale display rejected"
	);
	[window close];
}

void restorationContracts() {
	auto state = std::make_shared<WindowState>();
	WindowConfiguration configuration;
	configuration.isVisible = false;
	configuration.restoration = {
		.key = "test.restore",
		.enabled = true,
		.save = [] -> std::vector<std::uint8_t> {
			return {1, 2, 3};
		}
	};
	std::vector<std::uint8_t> restored;
	configuration.restoration.restore = [&](std::vector<std::uint8_t> bytes) -> void {
		restored = std::move(bytes);
	};
	NSWindow *const window = createConfiguredWindow(configuration);
	state->nativeWindow = window;
	applyWindowConfiguration(window, *state->appliedConfiguration, configuration);
	NSKeyedArchiver *const encoder = [[NSKeyedArchiver alloc] initRequiringSecureCoding:YES];
	encodeWindowRestoration(state, encoder);
	[encoder finishEncoding];
	NSError *error = nil;
	NSKeyedUnarchiver *const decoder =
		[[NSKeyedUnarchiver alloc] initForReadingFromData:encoder.encodedData error:&error];
	require(decoder != nil && error == nil, "restoration archive decoded");
	decodeWindowRestoration(state, decoder);
	require(restored == std::vector<std::uint8_t>{1, 2, 3}, "owned restoration payload round trip");
	[decoder finishDecoding];
	[window close];
}

void ownedDropAndGestureContracts() {
	auto state = std::make_shared<WindowState>();
	WindowConfiguration configuration;
	configuration.isVisible = false;
	configuration.dropTarget.formats = {"text/plain"};
	NativeDragInfo delivered;
	configuration.dropTarget.drop = [&](const NativeDragInfo &info) -> bool {
		delivered = info;
		return true;
	};
	NSWindow *const window = createConfiguredWindow(configuration);
	state->nativeWindow = window;
	STANATestFlippedView *const view =
		[[STANATestFlippedView alloc] initWithFrame:NSMakeRect(0, 0, initialWidth, initialHeight)];
	window.contentView = view;
	state->view = view;
	applyWindowConfiguration(window, *state->appliedConfiguration, configuration);
	NSPasteboard *const pasteboard = [NSPasteboard pasteboardWithUniqueName];
	[pasteboard declareTypes:@[ NSPasteboardTypeString ] owner:nil];
	[pasteboard setString:@"owned" forType:NSPasteboardTypeString];
	STANATestDragInfo *const drag = [[STANATestDragInfo alloc] init];
	drag.draggingPasteboard = pasteboard;
	drag.draggingSourceOperationMask = NSDragOperationCopy;
	drag.draggingLocation = [view convertPoint:NSMakePoint(3, 4) toView:nil];
	require(
		negotiateWindowDrag(state, drag, true, false) == NSDragOperationCopy,
		"accepted MIME format negotiates copy"
	);
	require(negotiateWindowDrag(state, drag, false, true) == NSDragOperationCopy, "drop accepted");
	[pasteboard clearContents];
	require(
		delivered.items.size() == 1 && delivered.items.front().format == "text/plain",
		"owned drop representation identity"
	);
	const auto &bytes = delivered.items.front().bytes;
	require(
		std::string{bytes.begin(), bytes.end()} == "owned",
		"drop bytes survive pasteboard clearing"
	);
	require(
		std::is_eq(delivered.location.x <=> 3.0) && std::is_eq(delivered.location.y <=> 4.0),
		"drop uses content-local top-left coordinates"
	);
	std::uint64_t token = 0;
	state->callbacks.interaction =
		[&](std::uint8_t phase, LogicalPoint point, std::uint64_t minted) -> void {
		static_cast<void>(point);
		if (phase == 0) {
			token = minted;
		}
	};
	NSEvent *const press = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
											  location:drag.draggingLocation
										 modifierFlags:0
											 timestamp:0
										  windowNumber:window.windowNumber
											   context:nil
										   eventNumber:0
											clickCount:1
											  pressure:1];
	publishWindowInteraction(state, press, 0);
	require(token != 0, "press produces gesture token");
	const NativeWindowAction action{
		.kind = NativeWindowActionKind::beginInteractiveMove,
		.token = token
	};
	NativeWindowOutcome outcome = NativeWindowOutcome::applied;
	const auto other = std::make_shared<WindowState>();
	dispatchWindowGesture(window, other, action, [&](NativeActionResult result) -> void {
		outcome = result.outcome;
	});
	require(outcome == NativeWindowOutcome::invalid, "cross-window gesture token rejected");
	publishWindowInteraction(state, press, 2);
	dispatchWindowGesture(window, state, action, [&](NativeActionResult result) -> void {
		outcome = result.outcome;
	});
	require(outcome == NativeWindowOutcome::invalid, "released gesture token rejected");
	[window close];
}

void ownerContracts() {
	WindowConfiguration parentConfiguration;
	parentConfiguration.id = 1;
	parentConfiguration.isVisible = false;
	WindowConfiguration childConfiguration;
	childConfiguration.id = 2;
	childConfiguration.owner = 1;
	childConfiguration.role = NativeWindowRole::utility;
	childConfiguration.isVisible = false;
	NSWindow *const parentWindow = createConfiguredWindow(parentConfiguration);
	NSWindow *const childWindow = createConfiguredWindow(childConfiguration);
	const auto parent = std::make_shared<WindowState>();
	const auto child = std::make_shared<WindowState>();
	parent->nativeWindow = parentWindow;
	child->nativeWindow = childWindow;
	parent->callbacks.configuration = parentConfiguration;
	child->callbacks.configuration = childConfiguration;
	registerConfiguredWindow(parent);
	registerConfiguredWindow(child);
	applyWindowConfiguration(parentWindow, *parent->appliedConfiguration, parentConfiguration);
	applyWindowConfiguration(childWindow, *child->appliedConfiguration, childConfiguration);
	require(
		attachWindowRole(child) && childWindow.parentWindow == parentWindow,
		"utility owner resolved before presentation"
	);
	require([childWindow isKindOfClass:NSPanel.class], "utility keeps panel role");
	[childWindow close];
	[parentWindow close];
	unregisterConfiguredWindows();
}

} // namespace
} // namespace stana::testing::fixtures::platform::macos

auto main() -> int {
	int result = 0;
	@autoreleasepool {
		try {
			[NSApplication sharedApplication];
			[NSApp finishLaunching];
			stana::testing::fixtures::platform::macos::configurationContracts();
			stana::testing::fixtures::platform::macos::restorationContracts();
			stana::testing::fixtures::platform::macos::ownedDropAndGestureContracts();
			stana::testing::fixtures::platform::macos::ownerContracts();
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			result = 1;
		}
		const auto path = stana::testing::fixtures::platform::macos::resultPath();
		if (!path.empty()) {
			std::ofstream{path} << result;
		}
	}
	return result;
}
