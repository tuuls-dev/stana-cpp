#include "window_configuration.hpp"
#include "presentation.hpp"

#import <AppKit/AppKit.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace stana::platform::detail {
namespace {

[[nodiscard]] auto utf8String(const std::string &text) -> NSString * {
	NSString *const value = [[NSString alloc] initWithBytes:text.data()
													 length:text.size()
												   encoding:NSUTF8StringEncoding];
	return value == nil ? @"" : value;
}

[[nodiscard]] auto screenHeight(NSScreen *screen) -> double {
	return screen == nil ? 0.0 : screen.frame.size.height;
}

[[nodiscard]] auto stanaPointFromAppKit(NSPoint point, NSScreen *screen) -> LogicalPoint {
	const double height = screenHeight(screen);
	return {.x = point.x, .y = height - point.y};
}

[[nodiscard]] auto appKitPointFromStana(LogicalPoint point, NSScreen *screen) -> NSPoint {
	const double height = screenHeight(screen);
	return NSMakePoint(point.x, height - point.y);
}

[[nodiscard]] auto configurationChanged(
	const WindowConfiguration &previous,
	const WindowConfiguration &next
) -> bool {
	return previous.title != next.title || previous.contentWidth != next.contentWidth ||
		previous.contentHeight != next.contentHeight || previous.opacity != next.opacity ||
		previous.hasShadow != next.hasShadow || previous.isVisible != next.isVisible ||
		previous.acceptsFocus != next.acceptsFocus ||
		previous.ignoresMouseEvents != next.ignoresMouseEvents ||
		previous.presentWithoutActivation != next.presentWithoutActivation ||
		previous.placement != next.placement || previous.originX != next.originX ||
		previous.originY != next.originY || previous.level != next.level ||
		previous.state != next.state || previous.style.closable != next.style.closable ||
		previous.style.minimizable != next.style.minimizable ||
		previous.style.maximizable != next.style.maximizable ||
		previous.style.resizable != next.style.resizable ||
		previous.style.titled != next.style.titled ||
		previous.minimumContentSize != next.minimumContentSize ||
		previous.maximumContentSize != next.maximumContentSize ||
		previous.aspectRatio != next.aspectRatio ||
		previous.resizeIncrement != next.resizeIncrement || previous.macos.present != next.macos.present ||
		previous.macos.fullSizeContentView != next.macos.fullSizeContentView ||
		previous.macos.transparentTitleBar != next.macos.transparentTitleBar ||
		previous.macos.titleVisible != next.macos.titleVisible ||
		previous.macos.hideTitleBarControls != next.macos.hideTitleBarControls ||
		previous.macos.tabbingMode != next.macos.tabbingMode ||
		previous.macos.tabbingIdentifier != next.macos.tabbingIdentifier ||
		previous.macos.spacesBehavior != next.macos.spacesBehavior ||
		previous.macos.fullscreenBehavior != next.macos.fullscreenBehavior ||
		previous.macos.becomesKeyOnlyIfNeeded != next.macos.becomesKeyOnlyIfNeeded ||
		previous.macos.worksWhenModal != next.macos.worksWhenModal ||
		previous.macos.floatingPanel != next.macos.floatingPanel ||
		previous.macos.nativeLevel != next.macos.nativeLevel;
}

[[nodiscard]] auto nativeLevelValue(std::uint8_t level) -> NSWindowLevel {
	switch (level) {
	case 1:
		return NSFloatingWindowLevel;
	case 2:
		return NSSubmenuWindowLevel;
	case 3:
		return NSTornOffMenuWindowLevel;
	case 4:
		return NSMainMenuWindowLevel;
	case 5:
		return NSStatusWindowLevel;
	case 6:
		return NSModalPanelWindowLevel;
	case 7:
		return NSPopUpMenuWindowLevel;
	case 8:
		return NSScreenSaverWindowLevel;
	default:
		return NSNormalWindowLevel;
	}
}

void applyCollectionBehavior(NSWindow *window, const WindowConfiguration &configuration) {
	NSWindowCollectionBehavior behavior = NSWindowCollectionBehaviorDefault;
	if (configuration.level == NativeWindowLevel::floating || configuration.macos.floatingPanel) {
		behavior |= NSWindowCollectionBehaviorCanJoinAllSpaces;
		behavior |= NSWindowCollectionBehaviorFullScreenAuxiliary;
	}
	if (configuration.macos.present) {
		switch (configuration.macos.spacesBehavior) {
		case 1:
			behavior |= NSWindowCollectionBehaviorCanJoinAllSpaces;
			break;
		case 2:
			behavior |= NSWindowCollectionBehaviorMoveToActiveSpace;
			break;
		default:
			break;
		}
		switch (configuration.macos.fullscreenBehavior) {
		case 1:
			behavior |= NSWindowCollectionBehaviorFullScreenAuxiliary;
			break;
		case 2:
			behavior |= NSWindowCollectionBehaviorFullScreenNone;
			break;
		default:
			behavior |= NSWindowCollectionBehaviorFullScreenPrimary;
			break;
		}
	}
	window.collectionBehavior = behavior;
}

void applyMacosExtensions(NSWindow *window, const WindowConfiguration &configuration) {
	if (!configuration.macos.present) {
		return;
	}
	window.titlebarAppearsTransparent = configuration.macos.transparentTitleBar;
	window.titleVisibility =
		configuration.macos.titleVisible ? NSWindowTitleVisible : NSWindowTitleHidden;
	if (configuration.macos.fullSizeContentView) {
		window.styleMask |= NSWindowStyleMaskFullSizeContentView;
	}
	if (configuration.macos.hideTitleBarControls) {
		[[window standardWindowButton:NSWindowCloseButton] setHidden:YES];
		[[window standardWindowButton:NSWindowMiniaturizeButton] setHidden:YES];
		[[window standardWindowButton:NSWindowZoomButton] setHidden:YES];
	}
	switch (configuration.macos.tabbingMode) {
	case 1:
		window.tabbingMode = NSWindowTabbingModePreferred;
		break;
	case 2:
		window.tabbingMode = NSWindowTabbingModeDisallowed;
		break;
	default:
		window.tabbingMode = NSWindowTabbingModeAutomatic;
		break;
	}
	if (configuration.macos.tabbingIdentifier) {
		window.tabbingIdentifier = utf8String(*configuration.macos.tabbingIdentifier);
	}
	if ([window isKindOfClass:[NSPanel class]]) {
		NSPanel *const panel = static_cast<NSPanel *>(window);
		panel.becomesKeyOnlyIfNeeded = configuration.macos.becomesKeyOnlyIfNeeded;
		panel.worksWhenModal = configuration.macos.worksWhenModal;
		panel.floatingPanel = configuration.macos.floatingPanel;
	}
	window.level = nativeLevelValue(configuration.macos.nativeLevel);
}

void applyPlacement(NSWindow *window, const WindowConfiguration &configuration) {
	NSScreen *const screen = window.screen != nil ? window.screen : NSScreen.mainScreen;
	const NSSize contentSize =
		NSMakeSize(configuration.contentWidth, configuration.contentHeight);
	[window setContentSize:contentSize];
	switch (configuration.placement) {
	case NativePlacementKind::system:
		break;
	case NativePlacementKind::centered:
		[window center];
		break;
	case NativePlacementKind::explicitOrigin: {
		const NSPoint appKitOrigin = appKitPointFromStana(
			{.x = configuration.originX, .y = configuration.originY + configuration.contentHeight},
			screen
		);
		const NSRect frame = [window frameRectForContentRect:NSMakeRect(
														  appKitOrigin.x,
														  appKitOrigin.y,
														  configuration.contentWidth,
														  configuration.contentHeight
													  )];
		[window setFrame:frame display:YES];
		break;
	}
	}
}

void applyState(NSWindow *window, NativeWindowState state) {
	switch (state) {
	case NativeWindowState::minimized:
		[window miniaturize:nil];
		break;
	case NativeWindowState::maximized:
		if (!window.zoomed) {
			[window zoom:nil];
		}
		break;
	case NativeWindowState::fullscreen:
		if ((window.styleMask & NSWindowStyleMaskFullScreen) == 0) {
			[window toggleFullScreen:nil];
		}
		break;
	case NativeWindowState::normal:
		if (window.miniaturized) {
			[window deminiaturize:nil];
		}
		if ((window.styleMask & NSWindowStyleMaskFullScreen) != 0) {
			[window toggleFullScreen:nil];
		}
		if (window.zoomed) {
			[window zoom:nil];
		}
		break;
	}
}

[[nodiscard]] auto currentState(NSWindow *window) -> NativeWindowState {
	if (window.miniaturized) {
		return NativeWindowState::minimized;
	}
	if ((window.styleMask & NSWindowStyleMaskFullScreen) != 0) {
		return NativeWindowState::fullscreen;
	}
	if (window.zoomed) {
		return NativeWindowState::maximized;
	}
	return NativeWindowState::normal;
}

} // namespace

auto nativeStyleMask(const WindowConfiguration &configuration) -> NSWindowStyleMask {
	NSWindowStyleMask mask = 0;
	if (configuration.style.titled) {
		mask |= NSWindowStyleMaskTitled;
	} else {
		mask |= NSWindowStyleMaskBorderless;
	}
	if (configuration.style.closable) {
		mask |= NSWindowStyleMaskClosable;
	}
	if (configuration.style.minimizable) {
		mask |= NSWindowStyleMaskMiniaturizable;
	}
	if (configuration.style.resizable) {
		mask |= NSWindowStyleMaskResizable;
	}
	if (configuration.role == NativeWindowRole::utility) {
		mask |= NSWindowStyleMaskUtilityWindow;
	}
	return mask;
}

auto createConfiguredWindow(const WindowConfiguration &configuration) -> NSWindow * {
	const NSRect contentRect =
		NSMakeRect(40, 40, configuration.contentWidth, configuration.contentHeight);
	const NSWindowStyleMask mask = nativeStyleMask(configuration);
	NSWindow *window = nil;
	if (configuration.role == NativeWindowRole::utility ||
		(configuration.macos.present && configuration.macos.floatingPanel)) {
		window = [[NSPanel alloc] initWithContentRect:contentRect
											styleMask:mask
											  backing:NSBackingStoreBuffered
												defer:NO];
	} else {
		window = [[NSWindow alloc] initWithContentRect:contentRect
											 styleMask:mask
											   backing:NSBackingStoreBuffered
												 defer:NO];
	}
	window.releasedWhenClosed = NO;
	window.title = utf8String(configuration.title);
	window.alphaValue = static_cast<CGFloat>(configuration.opacity);
	window.hasShadow = configuration.hasShadow;
	window.ignoresMouseEvents = configuration.ignoresMouseEvents;
	if (configuration.minimumContentSize) {
		window.contentMinSize = NSMakeSize(
			configuration.minimumContentSize->width,
			configuration.minimumContentSize->height
		);
	}
	if (configuration.maximumContentSize) {
		window.contentMaxSize = NSMakeSize(
			configuration.maximumContentSize->width,
			configuration.maximumContentSize->height
		);
	}
	if (configuration.aspectRatio) {
		window.contentAspectRatio = NSMakeSize(*configuration.aspectRatio, 1.0);
	}
	if (configuration.resizeIncrement) {
		window.contentResizeIncrements = NSMakeSize(
			configuration.resizeIncrement->width,
			configuration.resizeIncrement->height
		);
	}
	applyMacosExtensions(window, configuration);
	applyCollectionBehavior(window, configuration);
	applyPlacement(window, configuration);
	if (configuration.level == NativeWindowLevel::floating && !configuration.macos.present) {
		window.level = NSFloatingWindowLevel;
	}
	return window;
}

void applyWindowConfiguration(
	NSWindow *window,
	AppliedWindowConfiguration &applied,
	const WindowConfiguration &requested
) {
	if (window == nil) {
		return;
	}
	if (applied.hasApplied && !configurationChanged(applied.requested, requested)) {
		return;
	}
	window.title = utf8String(requested.title);
	window.alphaValue = static_cast<CGFloat>(requested.opacity);
	window.hasShadow = requested.hasShadow;
	window.ignoresMouseEvents = requested.ignoresMouseEvents;
	if (requested.minimumContentSize) {
		window.contentMinSize =
			NSMakeSize(requested.minimumContentSize->width, requested.minimumContentSize->height);
	}
	if (requested.maximumContentSize) {
		window.contentMaxSize =
			NSMakeSize(requested.maximumContentSize->width, requested.maximumContentSize->height);
	}
	if (requested.aspectRatio) {
		window.contentAspectRatio = NSMakeSize(*requested.aspectRatio, 1.0);
	}
	if (requested.resizeIncrement) {
		window.contentResizeIncrements = NSMakeSize(
			requested.resizeIncrement->width,
			requested.resizeIncrement->height
		);
	}
	applyMacosExtensions(window, requested);
	applyCollectionBehavior(window, requested);
	if (!applied.hasApplied || applied.requested.placement != requested.placement ||
		applied.requested.originX != requested.originX ||
		applied.requested.originY != requested.originY ||
		applied.requested.contentWidth != requested.contentWidth ||
		applied.requested.contentHeight != requested.contentHeight) {
		applyPlacement(window, requested);
	} else if (applied.requested.contentWidth != requested.contentWidth ||
		applied.requested.contentHeight != requested.contentHeight) {
		[window setContentSize:NSMakeSize(requested.contentWidth, requested.contentHeight)];
	}
	if (!applied.hasApplied || applied.requested.state != requested.state) {
		applyState(window, requested.state);
	}
	if (requested.isVisible) {
		if (requested.presentWithoutActivation || !requested.acceptsFocus) {
			[window orderFront:nil];
		} else if (!applied.hasApplied) {
			[window makeKeyAndOrderFront:nil];
		} else {
			[window orderFront:nil];
		}
	} else {
		[window orderOut:nil];
	}
	if (requested.level == NativeWindowLevel::floating && !requested.macos.present) {
		window.level = NSFloatingWindowLevel;
	}
	applied.requested = requested;
	applied.hasApplied = true;
}

auto observeNativeWindow(NSWindow *window) -> NativeWindowObservation {
	if (window == nil) {
		return {};
	}
	NSScreen *const screen = window.screen != nil ? window.screen : NSScreen.mainScreen;
	const NSRect content = [window contentRectForFrameRect:window.frame];
	const auto topLeft = stanaPointFromAppKit(
		NSMakePoint(content.origin.x, content.origin.y + content.size.height),
		screen
	);
	return {
		.contentX = topLeft.x,
		.contentY = topLeft.y,
		.contentWidth = content.size.width,
		.contentHeight = content.size.height,
		.screenId = screen == nil ? 0 : static_cast<std::uint64_t>(screen.hash),
		.state = currentState(window),
		.isVisible = window.isVisible,
		.isKey = window.isKeyWindow,
		.isActive = window.isMainWindow,
		.backingScale = screen == nil ? 1.0 : screen.backingScaleFactor
	};
}

void installWindowHostControl(
	NSWindow *window,
	const std::shared_ptr<WindowState> &state,
	const std::shared_ptr<WindowHostControl> &control,
	const std::shared_ptr<AppliedWindowConfiguration> &applied
) {
	if (control == nullptr || applied == nullptr) {
		return;
	}
	const auto weakState = std::weak_ptr{state};
	control->applyConfiguration =
		[window, applied, weakState](const WindowConfiguration &configuration) -> void {
		if (const auto owner = weakState.lock(); !owner || owner->isClosed) {
			return;
		}
		applyWindowConfiguration(window, *applied, configuration);
	};
	control->observe = [window] -> NativeWindowObservation {
		return observeNativeWindow(window);
	};
	control->dispatchAction =
		[window, applied, weakState](
			NativeWindowActionKind kind,
			NativeWindowState stateValue,
			std::uint64_t token
		) -> NativeWindowOutcome {
		if (const auto owner = weakState.lock(); !owner || owner->isClosed) {
			return NativeWindowOutcome::cancelled;
		}
		switch (kind) {
		case NativeWindowActionKind::focus:
			[window makeKeyAndOrderFront:nil];
			return NativeWindowOutcome::applied;
		case NativeWindowActionKind::setState:
			applyState(window, stateValue);
			return NativeWindowOutcome::applied;
		case NativeWindowActionKind::close:
			[window performClose:nil];
			return NativeWindowOutcome::pending;
		case NativeWindowActionKind::respondDeferredClose:
			if (token == 0 || token != applied->deferredCloseRequest) {
				return NativeWindowOutcome::invalid;
			}
			applied->deferredCloseRequest = 0;
			[window close];
			return NativeWindowOutcome::applied;
		case NativeWindowActionKind::beginInteractiveMove:
		case NativeWindowActionKind::beginInteractiveResize:
		case NativeWindowActionKind::showWindowMenu:
		case NativeWindowActionKind::capture:
		case NativeWindowActionKind::print:
			return NativeWindowOutcome::unsupported;
		}
		return NativeWindowOutcome::unsupported;
	};
}

auto enumerateMacScreens() -> std::vector<NativeScreen> {
	std::vector<NativeScreen> screens;
	NSArray<NSScreen *> *const native = NSScreen.screens;
	NSScreen *const primary = NSScreen.mainScreen;
	for (NSUInteger index = 0; index < native.count; ++index) {
		NSScreen *const screen = [native objectAtIndex:index];
		const NSRect frame = screen.frame;
		const NSRect visible = screen.visibleFrame;
		const auto topLeft = stanaPointFromAppKit(
			NSMakePoint(frame.origin.x, frame.origin.y + frame.size.height),
			screen
		);
		const auto workTopLeft = stanaPointFromAppKit(
			NSMakePoint(visible.origin.x, visible.origin.y + visible.size.height),
			screen
		);
		const char *const name = screen.localizedName.UTF8String;
		screens.push_back(
			{.id = static_cast<std::uint64_t>(screen.hash),
				.name = name != nullptr ? std::string{name} : std::string{},
				.bounds =
					{.origin = topLeft,
						.size = {.width = frame.size.width, .height = frame.size.height}},
				.workArea =
					{.origin = workTopLeft,
						.size = {.width = visible.size.width, .height = visible.size.height}},
				.backingScale = screen.backingScaleFactor,
				.isPrimary = screen == primary}
		);
	}
	return screens;
}

} // namespace stana::platform::detail
