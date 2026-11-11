#include "window_configuration.hpp"
#include "presentation.hpp"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

@protocol STANAWindowPolicy
@property BOOL stanaAllowsFocus;
@property BOOL stanaEnabled;
@end

@interface STANAConfiguredWindow : NSWindow <STANAWindowPolicy>
@property BOOL stanaAllowsFocus;
@property BOOL stanaEnabled;
@end
@implementation STANAConfiguredWindow

- (BOOL)canBecomeKeyWindow {
	return self.stanaAllowsFocus && self.stanaEnabled;
}

- (BOOL)canBecomeMainWindow {
	return self.stanaAllowsFocus && self.stanaEnabled;
}

@end

@interface STANAConfiguredPanel : NSPanel <STANAWindowPolicy>
@property BOOL stanaAllowsFocus;
@property BOOL stanaEnabled;
@end
@implementation STANAConfiguredPanel

- (BOOL)canBecomeKeyWindow {
	return self.stanaAllowsFocus && self.stanaEnabled;
}

@end

namespace stana::platform::detail {
namespace {
inline constexpr double halfDimension = 0.5;

[[nodiscard]] auto utf8String(const std::string &text) -> NSString * {
	return [[NSString alloc] initWithBytes:text.data()
									length:text.size()
								  encoding:NSUTF8StringEncoding];
}

[[nodiscard]] auto primaryHeight() -> double {
	return NSScreen.screens.firstObject.frame.size.height;
}

[[nodiscard]] auto screenId(NSScreen *screen) -> std::uint64_t {
	return [screen.deviceDescription[@"NSScreenNumber"] unsignedLongLongValue];
}

[[nodiscard]] auto selectedScreen(const WindowConfiguration &configuration, NSWindow *window)
	-> NSScreen * {
	if (configuration.screenSelection == 2) {
		for (NSUInteger index = 0; index < NSScreen.screens.count; ++index) {
			NSScreen *const screen = [NSScreen.screens objectAtIndex:index];
			if (screenId(screen) == configuration.screenId) {
				return screen;
			}
		}
		return nil;
	}
	if (configuration.screenSelection == 1) {
		return NSScreen.screens.firstObject;
	}
	return window.screen != nil ? window.screen : NSScreen.mainScreen;
}

[[nodiscard]] auto nativeLevelValue(MacosWindowLevel level) -> NSWindowLevel {
	switch (level) {
	case MacosWindowLevel::normal:
		return NSNormalWindowLevel;
	case MacosWindowLevel::floating:
		return NSFloatingWindowLevel;
	case MacosWindowLevel::submenu:
		return NSSubmenuWindowLevel;
	case MacosWindowLevel::tornOffMenu:
		return NSTornOffMenuWindowLevel;
	case MacosWindowLevel::mainMenu:
		return NSMainMenuWindowLevel;
	case MacosWindowLevel::statusBar:
		return NSStatusWindowLevel;
	case MacosWindowLevel::modalPanel:
		return NSModalPanelWindowLevel;
	case MacosWindowLevel::popUpMenu:
		return NSPopUpMenuWindowLevel;
	case MacosWindowLevel::screenSaver:
		return NSScreenSaverWindowLevel;
	}
	return NSNormalWindowLevel;
}

void applyCollectionBehavior(NSWindow *window, const WindowConfiguration &configuration) {
	NSWindowCollectionBehavior behavior = NSWindowCollectionBehaviorDefault;
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
	if (!configuration.style.fullscreens || configuration.macos.fullscreenBehavior == 2) {
		behavior |= NSWindowCollectionBehaviorFullScreenNone;
	} else if (configuration.macos.fullscreenBehavior == 1) {
		behavior |= NSWindowCollectionBehaviorFullScreenAuxiliary;
	} else {
		behavior |= NSWindowCollectionBehaviorFullScreenPrimary;
	}
	window.collectionBehavior = behavior;
}

void applyMacosExtensions(
	NSWindow *window,
	const WindowConfiguration &configuration,
	const std::string &defaultTabbingIdentifier
) {
	window.titlebarAppearsTransparent = configuration.macos.transparentTitleBar;
	window.titleVisibility =
		configuration.macos.titleVisible ? NSWindowTitleVisible : NSWindowTitleHidden;
	if (configuration.macos.fullSizeContentView) {
		window.styleMask |= NSWindowStyleMaskFullSizeContentView;
	} else {
		window.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
	}
	for (const auto button : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
		[window standardWindowButton:button].hidden = configuration.macos.hideTitleBarControls;
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
	window.tabbingIdentifier = configuration.macos.tabbingIdentifier
		? utf8String(*configuration.macos.tabbingIdentifier)
		: utf8String(defaultTabbingIdentifier);
	if ([window isKindOfClass:NSPanel.class]) {
		auto *const panel = static_cast<NSPanel *>(window);
		panel.becomesKeyOnlyIfNeeded = configuration.macos.becomesKeyOnlyIfNeeded;
		panel.worksWhenModal = configuration.macos.worksWhenModal;
		panel.floatingPanel = configuration.macos.floatingPanel;
	}
	window.level = configuration.macos.nativeLevel
		? nativeLevelValue(*configuration.macos.nativeLevel)
		: (configuration.level == NativeWindowLevel::floating ? NSFloatingWindowLevel
															  : NSNormalWindowLevel);
}

void applyPlacement(NSWindow *window, const WindowConfiguration &configuration) {
	NSScreen *const screen = selectedScreen(configuration, window);
	const NSRect content = [window contentRectForFrameRect:window.frame];
	NSRect target = NSMakeRect(
		content.origin.x,
		content.origin.y,
		configuration.contentWidth,
		configuration.contentHeight
	);
	switch (configuration.placement) {
	case NativePlacementKind::system: {
		static thread_local std::map<std::uint64_t, NSPoint> cascade;
		const auto id = screenId(screen);
		const auto found = cascade.find(id);
		const NSPoint origin = found == cascade.end()
			? NSMakePoint(NSMinX(screen.visibleFrame), NSMaxY(screen.visibleFrame))
			: found->second;
		[window setContentSize:target.size];
		cascade.insert_or_assign(id, [window cascadeTopLeftFromPoint:origin]);
		return;
	}
	case NativePlacementKind::centered: {
		const NSRect area = screen.visibleFrame;
		target.origin = NSMakePoint(
			NSMidX(area) - target.size.width * halfDimension,
			NSMidY(area) - target.size.height * halfDimension
		);
		break;
	}
	case NativePlacementKind::explicitOrigin:
		target.origin = NSMakePoint(
			configuration.originX,
			primaryHeight() - configuration.originY - configuration.contentHeight
		);
		break;
	}
	[window setFrame:[window frameRectForContentRect:target] display:YES];
}

inline constexpr std::chrono::seconds stateConfirmationDeadline{5};

struct StateCompletion {
	std::shared_ptr<NativeActionCompletion> callback;

	void operator()(NativeActionResult result) const {
		if (*callback) {
			const auto complete = std::move(*callback);
			*callback = {};
			complete(std::move(result));
		}
	}
};

auto applyState(NSWindow *window, NativeWindowState state) -> bool {
	if (state != NativeWindowState::fullscreen &&
		(window.styleMask & NSWindowStyleMaskFullScreen) != 0) {
		[window toggleFullScreen:nil];
		return true;
	}
	if (state != NativeWindowState::minimized && window.miniaturized) {
		[window deminiaturize:nil];
	}
	switch (state) {
	case NativeWindowState::minimized:
		if (!window.miniaturized) {
			[window miniaturize:nil];
		}
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
		if (window.zoomed) {
			[window zoom:nil];
		}
		break;
	}
	return false;
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
	NSWindowStyleMask mask =
		configuration.style.titled ? NSWindowStyleMaskTitled : NSWindowStyleMaskBorderless;
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

auto validateNativeConfiguration(const WindowConfiguration &configuration) -> bool {
	if (configuration.role == NativeWindowRole::dialog && configuration.modal &&
		configuration.owner != 0 && configuration.state != NativeWindowState::normal) {
		return false;
	}
	if (!configuration.style.titled &&
		(configuration.toolbar || configuration.macos.fullSizeContentView ||
			configuration.macos.transparentTitleBar || !configuration.macos.titleVisible ||
			configuration.macos.hideTitleBarControls)) {
		return false;
	}
	if (configuration.macos.tabbingMode > 2 || configuration.macos.spacesBehavior > 2 ||
		configuration.macos.fullscreenBehavior > 2 ||
		(configuration.macos.nativeLevel &&
			*configuration.macos.nativeLevel > MacosWindowLevel::screenSaver)) {
		return false;
	}
	if (configuration.document && configuration.document->representedPath) {
		NSString *const path = utf8String(*configuration.document->representedPath);
		if ([path containsString:@"://"] && [NSURL URLWithString:path] == nil) {
			return false;
		}
	}
	if (utf8String(configuration.title) == nil ||
		(configuration.macos.tabbingIdentifier &&
			utf8String(*configuration.macos.tabbingIdentifier) == nil)) {
		return false;
	}
	if (configuration.state == NativeWindowState::fullscreen &&
		(!configuration.style.fullscreens || configuration.macos.fullscreenBehavior == 2)) {
		return false;
	}
	if (selectedScreen(configuration, nil) == nil) {
		return false;
	}
	if (configuration.role != NativeWindowRole::utility &&
		(configuration.macos.becomesKeyOnlyIfNeeded || configuration.macos.worksWhenModal ||
			configuration.macos.floatingPanel)) {
		return false;
	}
	return true;
}

auto createConfiguredWindow(const WindowConfiguration &configuration) -> NSWindow * {
	if (!validateNativeConfiguration(configuration)) {
		return nil;
	}
	const NSRect rect = NSMakeRect(40, 40, configuration.contentWidth, configuration.contentHeight);
	NSWindow *window = nil;
	if (configuration.role == NativeWindowRole::utility) {
		STANAConfiguredPanel *const panel = [STANAConfiguredPanel alloc];
		window = [panel initWithContentRect:rect
								  styleMask:nativeStyleMask(configuration)
									backing:NSBackingStoreBuffered
									  defer:NO];
	} else {
		STANAConfiguredWindow *const ordinary = [STANAConfiguredWindow alloc];
		window = [ordinary initWithContentRect:rect
									 styleMask:nativeStyleMask(configuration)
									   backing:NSBackingStoreBuffered
										 defer:NO];
	}
	window.releasedWhenClosed = NO;
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
	if (!applied.hasApplied) {
		const char *const identifier = window.tabbingIdentifier.UTF8String;
		applied.defaultTabbingIdentifier = identifier == nullptr ? "" : identifier;
	}
	const NSRect previousContent = [window contentRectForFrameRect:window.frame];
	const NSWindowStyleMask targetStyle = nativeStyleMask(requested) |
		(window.styleMask & NSWindowStyleMaskFullScreen) |
		(requested.macos.fullSizeContentView ? NSWindowStyleMaskFullSizeContentView : 0);
	const bool styleChanged = window.styleMask != targetStyle;
	if (styleChanged) {
		window.styleMask = targetStyle;
	}
	const bool toolbarChanged =
		applied.requested.toolbar.has_value() != requested.toolbar.has_value() ||
		(applied.requested.toolbar && requested.toolbar &&
			applied.requested.toolbar->visible != requested.toolbar->visible);
	window.title = utf8String(requested.title);
	window.alphaValue = requested.opacity;
	window.hasShadow = requested.hasShadow;
	const auto policy = static_cast<id<STANAWindowPolicy>>(window);
	policy.stanaAllowsFocus = requested.acceptsFocus;
	policy.stanaEnabled = requested.isEnabled;
	window.ignoresMouseEvents = requested.ignoresMouseEvents || !requested.isEnabled;
	if ((!requested.acceptsFocus || !requested.isEnabled) && window.keyWindow) {
		[window resignKeyWindow];
	}
	[window standardWindowButton:NSWindowCloseButton].enabled =
		requested.style.closable && requested.isEnabled;
	[window standardWindowButton:NSWindowMiniaturizeButton].enabled =
		requested.style.minimizable && requested.isEnabled;
	[window standardWindowButton:NSWindowZoomButton].enabled =
		requested.style.maximizable && requested.isEnabled;
	window.contentMinSize = requested.minimumContentSize
		? NSMakeSize(requested.minimumContentSize->width, requested.minimumContentSize->height)
		: NSZeroSize;
	window.contentMaxSize = requested.maximumContentSize
		? NSMakeSize(requested.maximumContentSize->width, requested.maximumContentSize->height)
		: NSMakeSize(std::numeric_limits<CGFloat>::max(), std::numeric_limits<CGFloat>::max());
	window.contentAspectRatio =
		requested.aspectRatio ? NSMakeSize(*requested.aspectRatio, 1) : NSZeroSize;
	window.contentResizeIncrements = requested.resizeIncrement
		? NSMakeSize(requested.resizeIncrement->width, requested.resizeIncrement->height)
		: NSMakeSize(1, 1);
	window.appearance = requested.darkTheme
		? [NSAppearance appearanceNamed:*requested.darkTheme ? NSAppearanceNameDarkAqua
															 : NSAppearanceNameAqua]
		: nil;
	window.opaque = requested.backgroundKind != 2 &&
		(requested.backgroundKind != 1 || requested.background.alpha >= 1.0);
	switch (requested.backgroundKind) {
	case 1:
		window.backgroundColor = requested.background.alpha < 1.0
			? NSColor.clearColor
			: [NSColor colorWithSRGBRed:requested.background.red
								  green:requested.background.green
								   blue:requested.background.blue
								  alpha:requested.background.alpha];
		break;
	case 2:
		window.backgroundColor = NSColor.clearColor;
		break;
	default:
		window.backgroundColor = NSColor.windowBackgroundColor;
		break;
	}
	applyMacosExtensions(window, requested, applied.defaultTabbingIdentifier);
	applyCollectionBehavior(window, requested);
	applyWindowFacilities(window, applied, requested);
	const auto &previous = applied.requested;
	if (!applied.hasApplied || previous.placement != requested.placement ||
		!std::is_eq(previous.originX <=> requested.originX) ||
		!std::is_eq(previous.originY <=> requested.originY) ||
		previous.screenSelection != requested.screenSelection ||
		previous.screenId != requested.screenId) {
		applyPlacement(window, requested);
	} else if (styleChanged || toolbarChanged ||
		!std::is_eq(previous.contentWidth <=> requested.contentWidth) ||
		!std::is_eq(previous.contentHeight <=> requested.contentHeight)) {
		const NSRect content = previousContent;
		const NSRect target = NSMakeRect(
			content.origin.x,
			NSMaxY(content) - requested.contentHeight,
			requested.contentWidth,
			requested.contentHeight
		);
		[window setFrame:[window frameRectForContentRect:target] display:YES];
	}
	const bool stateChanged = !applied.hasApplied || previous.state != requested.state;
	const bool visibilityChanged = !applied.hasApplied || previous.isVisible != requested.isVisible;
	applied.requested = requested;
	applied.hasApplied = true;
	if (visibilityChanged && !applyRoleVisibility(window, requested, requested.isVisible)) {
		if (requested.isVisible) {
			if (!requested.presentWithoutActivation && requested.acceptsFocus &&
				requested.isEnabled) {
				[window makeKeyAndOrderFront:nil];
			} else {
				[window orderFront:nil];
			}
		} else {
			[window orderOut:nil];
		}
	}
	if (stateChanged && applyState(window, requested.state)) {
		applied.transitionTarget = requested.state;
	}
}

auto observeNativeWindow(NSWindow *window) -> NativeWindowObservation {
	if (window == nil) {
		return {};
	}
	const NSRect content = [window contentRectForFrameRect:window.frame];
	return {
		.contentX = content.origin.x,
		.contentY = primaryHeight() - NSMaxY(content),
		.contentWidth = content.size.width,
		.contentHeight = content.size.height,
		.screenId = screenId(window.screen),
		.state = currentState(window),
		.isVisible = static_cast<bool>(window.visible),
		.isKey = static_cast<bool>(window.keyWindow),
		.isActive = static_cast<bool>(NSApp.active && (window.mainWindow || window.keyWindow)),
		.backingScale = window.backingScaleFactor
	};
}

void installWindowHostControl(
	NSWindow *window,
	const std::shared_ptr<WindowState> &state,
	const std::shared_ptr<WindowHostControl> &control,
	const std::shared_ptr<AppliedWindowConfiguration> &applied
) {
	if (!control || !applied) {
		return;
	}
	const auto weak = std::weak_ptr{state};
	control->applyConfiguration =
		[window, weak, applied](const WindowConfiguration &configuration) -> NativeWindowOutcome {
		const auto owner = weak.lock();
		if (!owner || owner->isClosed) {
			return NativeWindowOutcome::cancelled;
		}
		if (!validateNativeConfiguration(configuration)) {
			return NativeWindowOutcome::invalid;
		}
		applyWindowConfiguration(window, *applied, configuration);
		if (!configuration.isEnabled) {
			owner->interactionToken = 0;
			owner->interactionEvent = nil;
		}
		owner->layer.opaque = window.opaque;
		for (const auto &tile : owner->tileLayers) {
			tile.opaque = window.opaque;
		}
		return NativeWindowOutcome::applied;
	};
	control->observe = [window, weak] -> NativeWindowObservation {
		const auto owner = weak.lock();
		return owner && !owner->isClosed ? observeNativeWindow(window) : NativeWindowObservation{};
	};
	control->dispatchAction = [window, weak, applied](
								  const NativeWindowAction &action,
								  NativeActionCompletion complete
							  ) -> void {
		const auto owner = weak.lock();
		if (!owner || owner->isClosed) {
			complete({.outcome = NativeWindowOutcome::cancelled});
			return;
		}
		switch (action.kind) {
		case NativeWindowActionKind::focus:
			if (!applied->requested.acceptsFocus || !applied->requested.isEnabled) {
				complete({.outcome = NativeWindowOutcome::denied});
				return;
			}
			[window makeKeyAndOrderFront:nil];
			complete(
				{.outcome = window.keyWindow ? NativeWindowOutcome::applied
											 : NativeWindowOutcome::denied}
			);
			return;
		case NativeWindowActionKind::setState:
			if (applied->requested.role == NativeWindowRole::dialog && applied->requested.modal &&
				applied->requested.owner != 0 && action.state != NativeWindowState::normal) {
				complete({.outcome = NativeWindowOutcome::denied});
				return;
			}
			applyState(window, action.state);
			if (currentState(window) == action.state) {
				complete({.outcome = NativeWindowOutcome::applied});
			} else {
				complete({.outcome = NativeWindowOutcome::pending});
				const StateCompletion terminal{
					std::make_shared<NativeActionCompletion>(std::move(complete))
				};
				applied->stateRequests.emplace_back(action.state, terminal);
				postAfter(stateConfirmationDeadline, [weak, applied, terminal] -> void {
					if (const auto current = weak.lock(); current && !current->isClosed) {
						terminal({.outcome = NativeWindowOutcome::unconfirmed});
						std::erase_if(applied->stateRequests, [](const auto &request) -> bool {
							const auto *completion =
								request.second.template target<StateCompletion>();
							return completion != nullptr && !*completion->callback;
						});
					}
				});
			}
			return;
		case NativeWindowActionKind::close: {
			const BOOL allowed = [window.delegate windowShouldClose:window];
			if (allowed) {
				applied->closeRequests.push_back(std::move(complete));
				[window close];
			} else if (applied->deferredCloseRequest != 0) {
				complete({.outcome = NativeWindowOutcome::pending});
				applied->closeRequests.push_back(std::move(complete));
			} else {
				complete({.outcome = NativeWindowOutcome::denied});
			}
			return;
		}
		case NativeWindowActionKind::respondDeferredClose:
			if (action.token == 0 || action.token != applied->deferredCloseRequest) {
				complete({.outcome = NativeWindowOutcome::invalid});
				return;
			}
			applied->deferredCloseRequest = 0;
			if (action.closeDecision == NativeCloseDecision::deny) {
				auto requests = std::move(applied->closeRequests);
				applied->closeRequests.clear();
				for (const auto &request : requests) {
					request({.outcome = NativeWindowOutcome::denied});
				}
				complete({.outcome = NativeWindowOutcome::applied});
			} else {
				applied->closeRequests.push_back(std::move(complete));
				[window close];
			}
			return;
		case NativeWindowActionKind::beginInteractiveMove:
		case NativeWindowActionKind::beginInteractiveResize:
		case NativeWindowActionKind::showWindowMenu:
			dispatchWindowGesture(window, owner, action, complete);
			return;
		case NativeWindowActionKind::capture:
		case NativeWindowActionKind::print:
			captureWindowContent(
				window,
				owner,
				action.kind == NativeWindowActionKind::print,
				std::move(complete)
			);
			return;
		}
	};
}

void publishWindowObservation(const std::shared_ptr<WindowState> &state) {
	if (!state || state->isClosed || state->nativeWindow == nil) {
		return;
	}
	auto observation = observeNativeWindow(state->nativeWindow);
	if (state->appliedConfiguration->transitionTarget &&
		observation.state != NativeWindowState::fullscreen) {
		const auto target = *state->appliedConfiguration->transitionTarget;
		state->appliedConfiguration->transitionTarget.reset();
		static_cast<void>(applyState(state->nativeWindow, target));
		observation = observeNativeWindow(state->nativeWindow);
	}
	const auto applied = state->appliedConfiguration;
	auto pending = std::move(applied->stateRequests);
	applied->stateRequests.clear();
	for (auto &request : pending) {
		if (request.first == observation.state) {
			request.second({.outcome = NativeWindowOutcome::applied});
		} else {
			applied->stateRequests.push_back(std::move(request));
		}
	}
	if (state->callbacks.stateChanged) {
		state->callbacks.stateChanged(observation);
	}
}

auto enumerateMacScreens() -> std::vector<NativeScreen> {
	std::vector<NativeScreen> result;
	for (NSUInteger index = 0; index < NSScreen.screens.count; ++index) {
		NSScreen *const screen = [NSScreen.screens objectAtIndex:index];
		const auto convert = [](NSRect rect) -> LogicalRect {
			return {
				.origin = {.x = rect.origin.x, .y = primaryHeight() - NSMaxY(rect)},
				.size = {.width = rect.size.width, .height = rect.size.height}
			};
		};
		const char *const name = screen.localizedName.UTF8String;
		result.push_back(
			{.id = screenId(screen),
				.name = name == nullptr ? std::string{} : std::string{name},
				.bounds = convert(screen.frame),
				.workArea = convert(screen.visibleFrame),
				.backingScale = screen.backingScaleFactor,
				.isPrimary = screen == NSScreen.screens.firstObject}
		);
	}
	return result;
}
} // namespace stana::platform::detail
