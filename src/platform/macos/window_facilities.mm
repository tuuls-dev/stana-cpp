#include "../../gpu/color.hpp"
#include "presentation.hpp"
#include "window_configuration.hpp"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace stana::platform::detail {
namespace {
inline constexpr std::uint8_t bottomAnchor = 6;
inline constexpr std::uint8_t bottomTrailingAnchor = 7;
inline constexpr std::uint8_t resizeBottomLeading = 6;
inline constexpr std::uint8_t resizeBottomTrailing = 7;
inline constexpr std::size_t halfPixelBytes = 8;
inline constexpr double maximumByte = 255;
inline constexpr unsigned captureAttempts = 500;
inline constexpr std::chrono::milliseconds captureInterval{10};

[[nodiscard]] auto ownedText(NSString *value) -> std::string {
	const char *const text = value.UTF8String;
	return text == nullptr ? std::string{} : std::string{text};
}

[[nodiscard]] auto nativeText(const std::string &text) -> NSString * {
	return [[NSString alloc] initWithBytes:text.data()
									length:text.size()
								  encoding:NSUTF8StringEncoding];
}

[[nodiscard]] auto windowsById() -> std::map<std::uint64_t, std::weak_ptr<WindowState>> & {
	static thread_local std::map<std::uint64_t, std::weak_ptr<WindowState>> windows;
	return windows;
}

[[nodiscard]] auto windowsByKey() -> std::map<std::string, std::weak_ptr<WindowState>> & {
	static thread_local std::map<std::string, std::weak_ptr<WindowState>> windows;
	return windows;
}

[[nodiscard]] auto nativeFormat(const std::string &format) -> NSString * {
	if (format == "text" || format == "text/plain") {
		return NSPasteboardTypeString;
	}
	if (format == "url" || format == "text/uri-list") {
		return NSPasteboardTypeURL;
	}
	if (format == "file" || format == "file-url") {
		return NSPasteboardTypeFileURL;
	}
	NSString *const value = nativeText(format);
	if ([value containsString:@"/"]) {
		return [UTType typeWithMIMEType:value].identifier;
	}
	return value;
}

[[nodiscard]] auto offeredFormat(NSPasteboardItem *item, NSString *accepted) -> NSString * {
	if ([item.types containsObject:accepted]) {
		return accepted;
	}
	UTType *const target = [UTType typeWithIdentifier:accepted];
	if (target == nil) {
		return nil;
	}
	for (NSUInteger index = 0; index < item.types.count; ++index) {
		NSString *const offered = [item.types objectAtIndex:index];
		UTType *const source = [UTType typeWithIdentifier:offered];
		if (source != nil && [source conformsToType:target]) {
			return offered;
		}
	}
	return nil;
}

[[nodiscard]] auto nativeOperation(std::uint8_t operation) -> NSDragOperation {
	switch (operation) {
	case 1:
		return NSDragOperationCopy;
	case 2:
		return NSDragOperationMove;
	case 3:
		return NSDragOperationLink;
	default:
		return NSDragOperationNone;
	}
}
} // namespace
} // namespace stana::platform::detail

@interface STANAToolbarDelegate : NSObject <NSToolbarDelegate> {
  @public
	stana::platform::detail::NativeToolbar descriptor_;
}
- (void)activate:(NSToolbarItem *)sender;
@end
@implementation STANAToolbarDelegate

- (NSArray<NSToolbarItemIdentifier> *)toolbarAllowedItemIdentifiers:(NSToolbar *)toolbar {
	static_cast<void>(toolbar);
	NSMutableArray<NSString *> *const items = [NSMutableArray array];
	for (const auto &item : descriptor_.items) {
		[items addObject:stana::platform::detail::nativeText(item.identifier)];
	}
	return items;
}

- (NSArray<NSToolbarItemIdentifier> *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar {
	return [self toolbarAllowedItemIdentifiers:toolbar];
}

- (NSToolbarItem *)toolbar:(NSToolbar *)toolbar
		itemForItemIdentifier:(NSToolbarItemIdentifier)identifier
	willBeInsertedIntoToolbar:(BOOL)inserted {
	static_cast<void>(toolbar);
	static_cast<void>(inserted);
	for (const auto &item : descriptor_.items) {
		if ([identifier isEqualToString:stana::platform::detail::nativeText(item.identifier)]) {
			NSToolbarItem *const native = [[NSToolbarItem alloc] initWithItemIdentifier:identifier];
			native.label = stana::platform::detail::nativeText(item.label);
			native.enabled = item.enabled;
			native.autovalidates = NO;
			native.target = self;
			native.action = @selector(activate:);
			return native;
		}
	}
	return nil;
}

- (void)activate:(NSToolbarItem *)sender {
	if (sender.enabled && descriptor_.activate) {
		const auto activate = descriptor_.activate;
		activate(stana::platform::detail::ownedText(sender.itemIdentifier));
	}
}

@end

@interface STANAWindowRestoration : NSObject <NSWindowRestoration>
@end
@implementation STANAWindowRestoration

+ (void)restoreWindowWithIdentifier:(NSUserInterfaceItemIdentifier)identifier
							  state:(NSCoder *)coder
				  completionHandler:(void (^)(NSWindow *, NSError *))completion {
	static_cast<void>(coder);
	const auto &registry = stana::platform::detail::windowsByKey();
	const auto found = registry.find(stana::platform::detail::ownedText(identifier));
	const auto state = found == registry.end() ? nullptr : found->second.lock();
	completion(state && !state->isClosed ? state->nativeWindow : nil, nil);
}

@end

namespace stana::platform::detail {
void registerConfiguredWindow(const std::shared_ptr<WindowState> &state) {
	const auto &configuration = state->callbacks.configuration;
	windowsById().insert_or_assign(configuration.id, state);
	if (configuration.restoration.enabled) {
		windowsByKey().insert_or_assign(configuration.restoration.key, state);
	}
}

void closeOwnedWindows(std::uint64_t owner) {
	if (owner == 0) {
		return;
	}
	std::vector<std::shared_ptr<WindowState>> children;
	children.reserve(windowsById().size());
	for (const auto &[id, weak] : windowsById()) {
		static_cast<void>(id);
		if (const auto child = weak.lock();
			child && !child->isClosed && child->callbacks.configuration.owner == owner) {
			children.push_back(child);
		}
	}
	for (const auto &child : children) {
		[child->nativeWindow close];
	}
}

void unregisterConfiguredWindows() {
	windowsById().clear();
	windowsByKey().clear();
}

auto attachWindowRole(const std::shared_ptr<WindowState> &state) -> bool {
	const auto &configuration = state->callbacks.configuration;
	NSWindow *const window = state->nativeWindow;
	NSWindow *parent = nil;
	if (configuration.owner != 0) {
		const auto found = windowsById().find(configuration.owner);
		const auto owner = found == windowsById().end() ? nullptr : found->second.lock();
		if (!owner || owner->isClosed) {
			return false;
		}
		parent = owner->nativeWindow;
		if (!(configuration.role == NativeWindowRole::dialog && configuration.modal)) {
			[parent addChildWindow:window ordered:NSWindowAbove];
		}
	}
	if (configuration.role == NativeWindowRole::popup) {
		if (parent == nil) {
			return false;
		}
		const auto &anchor = configuration.anchor;
		const NSRect rect = [parent.contentView convertRect:NSMakeRect(
																anchor.origin.x,
																anchor.origin.y,
																anchor.size.width,
																anchor.size.height
															)
													 toView:nil];
		const NSRect global = [parent convertRectToScreen:rect];
		NSRect content = [window contentRectForFrameRect:window.frame];
		double x = NSMinX(global);
		double y = NSMinY(global) - content.size.height;
		switch (configuration.anchorEdge) {
		case 0:
			y = NSMaxY(global);
			break;
		case 1:
			x = NSMidX(global) - content.size.width / 2;
			y = NSMaxY(global);
			break;
		case 2:
			x = NSMaxX(global) - content.size.width;
			y = NSMaxY(global);
			break;
		case 3:
			x = NSMinX(global) - content.size.width;
			y = NSMidY(global) - content.size.height / 2;
			break;
		case 4:
			x = NSMaxX(global);
			y = NSMidY(global) - content.size.height / 2;
			break;
		case bottomAnchor:
			x = NSMidX(global) - content.size.width / 2;
			break;
		case bottomTrailingAnchor:
			x = NSMaxX(global) - content.size.width;
			break;
		default:
			break;
		}
		content.origin = NSMakePoint(x, y);
		[window setFrame:[window frameRectForContentRect:content] display:YES];
	}

	return true;
}

auto applyRoleVisibility(NSWindow *window, const WindowConfiguration &configuration, bool visible)
	-> bool {
	if (configuration.role != NativeWindowRole::dialog || !configuration.modal) {
		return false;
	}
	if (configuration.owner != 0) {
		const auto found = windowsById().find(configuration.owner);
		const auto owner = found == windowsById().end() ? nullptr : found->second.lock();
		if (!owner || owner->isClosed) {
			return true;
		}
		if (visible && window.sheetParent == nil) {
			[owner->nativeWindow beginSheet:window completionHandler:nil];
		} else if (!visible) {
			if (window.sheetParent != nil) {
				[window.sheetParent endSheet:window];
			}
			[window orderOut:nil];
		}
		return true;
	}
	if (visible) {
		[window orderFront:nil];
		const auto found = windowsById().find(configuration.id);
		if (found != windowsById().end()) {
			const auto weak = found->second;
			postAfter(std::chrono::milliseconds{0}, [weak] -> void {
				if (const auto owner = weak.lock();
					owner && !owner->isClosed && owner->appliedConfiguration->requested.isVisible) {
					[NSApp runModalForWindow:owner->nativeWindow];
				}
			});
		}
	} else {
		if (NSApp.modalWindow == window) {
			[NSApp stopModal];
		}
		[window orderOut:nil];
	}
	return visible;
}

void applyWindowFacilities(
	NSWindow *window,
	AppliedWindowConfiguration &applied,
	const WindowConfiguration &configuration
) {
	if (!configuration.toolbar) {
		window.toolbar = nil;
		applied.toolbarDelegate = nil;
	} else {
		const auto &descriptor = *configuration.toolbar;
		const auto &previous = applied.requested.toolbar;
		const bool replace = !applied.hasApplied || !previous ||
			previous->identifier != descriptor.identifier || previous->items != descriptor.items;
		if (replace) {
			STANAToolbarDelegate *const delegate = [[STANAToolbarDelegate alloc] init];
			delegate->descriptor_ = descriptor;
			NSToolbar *const toolbar =
				[[NSToolbar alloc] initWithIdentifier:nativeText(descriptor.identifier)];
			toolbar.delegate = delegate;
			toolbar.allowsUserCustomization = NO;
			toolbar.autosavesConfiguration = NO;
			applied.toolbarDelegate = delegate;
			window.toolbar = toolbar;
		}
		window.toolbar.visible = descriptor.visible;
		for (NSUInteger index = 0;
			index < window.toolbar.items.count && index < descriptor.items.size();
			++index) {
			[window.toolbar.items objectAtIndex:index].enabled =
				descriptor.items.at(index).enabled && configuration.isEnabled;
		}
	}
	if (configuration.document && configuration.document->representedPath) {
		NSString *const path = nativeText(*configuration.document->representedPath);
		NSURL *const url = [path containsString:@"://"] ? [NSURL URLWithString:path]
														: [NSURL fileURLWithPath:path];
		window.representedURL = url;
	} else {
		window.representedURL = nil;
	}
	window.documentEdited = configuration.document && configuration.document->edited;
	NSMutableArray<NSPasteboardType> *const types = [NSMutableArray array];
	for (const auto &format : configuration.dropTarget.formats) {
		NSString *const type = nativeFormat(format);
		if (type != nil) {
			[types addObject:type];
		}
	}
	[window.contentView unregisterDraggedTypes];
	if (types.count != 0) {
		[window.contentView registerForDraggedTypes:types];
	}
	window.restorable = configuration.restoration.enabled;
	window.identifier =
		configuration.restoration.enabled ? nativeText(configuration.restoration.key) : nil;
	window.restorationClass =
		configuration.restoration.enabled ? STANAWindowRestoration.class : Nil;
}

void encodeWindowRestoration(const std::shared_ptr<WindowState> &state, NSCoder *coder) {
	const auto restoration = state->appliedConfiguration->requested.restoration;
	if (!restoration.enabled) {
		return;
	}
	[coder encodeRect:state->nativeWindow.frame forKey:@"stana.frame"];
	if (restoration.save) {
		const auto bytes = restoration.save();
		[coder encodeObject:[NSData dataWithBytes:bytes.data() length:bytes.size()]
					 forKey:@"stana.payload"];
	}
}

void decodeWindowRestoration(const std::shared_ptr<WindowState> &state, NSCoder *coder) {
	const auto restoration = state->appliedConfiguration->requested.restoration;
	if (!restoration.enabled) {
		return;
	}
	if ([coder containsValueForKey:@"stana.frame"]) {
		[state->nativeWindow setFrame:[coder decodeRectForKey:@"stana.frame"] display:YES];
	}
	NSData *const data = [coder decodeObjectOfClass:NSData.class forKey:@"stana.payload"];
	if (data != nil && restoration.restore) {
		std::vector<std::uint8_t> bytes(data.length);
		[data getBytes:bytes.data() length:bytes.size()];
		restoration.restore(std::move(bytes));
	}
}

auto negotiateWindowDrag(
	const std::shared_ptr<WindowState> &state,
	id<NSDraggingInfo> sender,
	bool entered,
	bool dropping
) -> NSDragOperation {
	if (!state || state->isClosed || !state->appliedConfiguration->requested.isEnabled) {
		return NSDragOperationNone;
	}
	const auto target = state->appliedConfiguration->requested.dropTarget;
	const NSPoint local = [state->view convertPoint:sender.draggingLocation fromView:nil];
	NativeDragInfo info{.location = {.x = local.x, .y = local.y}, .operation = target.operation};
	for (const auto &format : target.formats) {
		NSString *const type = nativeFormat(format);
		if (type == nil) {
			continue;
		}
		for (NSUInteger index = 0; index < sender.draggingPasteboard.pasteboardItems.count;
			++index) {
			NSPasteboardItem *const item =
				[sender.draggingPasteboard.pasteboardItems objectAtIndex:index];
			NSString *const offered = offeredFormat(item, type);
			if (offered != nil) {
				info.formats.push_back(format);
				if (dropping) {
					NSData *const data = [item dataForType:offered];
					if (data != nil) {
						NativeDropItem payload{
							.format = [offered isEqualToString:type] ? format : ownedText(offered),
							.bytes = std::vector<std::uint8_t>(data.length)
						};
						[data getBytes:payload.bytes.data() length:payload.bytes.size()];
						info.items.push_back(std::move(payload));
					}
				}
			}
		}
	}
	if (info.formats.empty() || (dropping && info.items.empty())) {
		return NSDragOperationNone;
	}
	const auto proposed = target.negotiate ? target.negotiate(info, entered) : target.operation;
	if (state->isClosed) {
		return NSDragOperationNone;
	}
	const auto operation = nativeOperation(proposed);
	if (proposed != target.operation || (sender.draggingSourceOperationMask & operation) == 0) {
		return NSDragOperationNone;
	}
	info.operation = proposed;
	if (dropping && (!target.drop || !target.drop(info))) {
		return NSDragOperationNone;
	}
	return operation;
}

void publishWindowInteraction(
	const std::shared_ptr<WindowState> &state,
	NSEvent *event,
	std::uint8_t phase
) {
	if (!state || state->isClosed || !state->appliedConfiguration->requested.isEnabled) {
		return;
	}
	std::uint64_t token = 0;
	if (phase == 0) {
		static std::atomic<std::uint64_t> identity{1};
		token = identity.fetch_add(1, std::memory_order_relaxed);
		state->interactionToken = token;
		state->interactionEvent = event;
	} else if (phase == 2) {
		state->interactionToken = 0;
		state->interactionEvent = nil;
	}
	const NSPoint point = [state->view convertPoint:event.locationInWindow fromView:nil];
	if (state->callbacks.interaction) {
		state->callbacks.interaction(phase, {.x = point.x, .y = point.y}, token);
	}
}

void dispatchWindowGesture(
	NSWindow *window,
	const std::shared_ptr<WindowState> &state,
	const NativeWindowAction &action,
	const NativeActionCompletion &complete
) {
	if (action.token == 0 || action.token != state->interactionToken ||
		state->interactionEvent == nil) {
		complete({.outcome = NativeWindowOutcome::invalid});
		return;
	}
	NSEvent *const event = state->interactionEvent;
	state->interactionToken = 0;
	state->interactionEvent = nil;
	if (action.kind == NativeWindowActionKind::beginInteractiveMove) {
		[window performWindowDragWithEvent:event];
		complete({.outcome = NativeWindowOutcome::applied});
		return;
	}
	if (action.kind == NativeWindowActionKind::showWindowMenu) {
		NSMenu *const menu =
			[[NSMenu alloc] initWithTitle:NSLocalizedString(@"Window", @"Window menu title")];
		for (const auto &[title, selector] : std::array<std::pair<NSString *, SEL>, 4>{
				 {{NSLocalizedString(@"Minimize", @"Window menu action"),
					  @selector(performMiniaturize:)},
					 {NSLocalizedString(@"Zoom", @"Window menu action"), @selector(performZoom:)},
					 {NSLocalizedString(@"Enter Full Screen", @"Window menu action"),
						 @selector(toggleFullScreen:)},
					 {NSLocalizedString(@"Close", @"Window menu action"), @selector(performClose:)}}
			 }) {
			NSMenuItem *const item = [[NSMenuItem alloc] initWithTitle:title
																action:selector
														 keyEquivalent:@""];
			item.target = window;
			[menu addItem:item];
		}
		[menu addItem:NSMenuItem.separatorItem];
		NSMenuItem *const arrange = [[NSMenuItem alloc]
			initWithTitle:NSLocalizedString(@"Bring All to Front", @"Window menu action")
				   action:@selector(arrangeInFront:)
			keyEquivalent:@""];
		arrange.target = NSApp;
		[menu addItem:arrange];
		NSApp.windowsMenu = menu;
		[menu popUpMenuPositioningItem:nil
							atLocation:NSMakePoint(action.location.x, action.location.y)
								inView:state->view];
		complete({.outcome = NativeWindowOutcome::applied});
		return;
	}
	if (!state->appliedConfiguration->requested.style.resizable) {
		complete({.outcome = NativeWindowOutcome::denied});
		return;
	}
	const NSRect start = [window contentRectForFrameRect:window.frame];
	const NSPoint origin = [window convertPointToScreen:event.locationInWindow];
	const bool left = action.edge == 2 || action.edge == 4 || action.edge == resizeBottomLeading;
	const bool right = action.edge == 3 || action.edge == 5 || action.edge == resizeBottomTrailing;
	const bool top = action.edge == 0 || action.edge == 4 || action.edge == 5;
	const bool bottom = action.edge == 1 || action.edge == resizeBottomLeading ||
		action.edge == resizeBottomTrailing;
	while (!state->isClosed) {
		NSEvent *const next =
			[window nextEventMatchingMask:NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp];
		if (next == nil || next.type == NSEventTypeLeftMouseUp) {
			break;
		}
		const NSPoint point = [window convertPointToScreen:next.locationInWindow];
		NSSize size = NSMakeSize(
			start.size.width +
				(right ? point.x - origin.x
						: left ? origin.x - point.x
							   : 0),
			start.size.height +
				(top ? point.y - origin.y
						: bottom ? origin.y - point.y
								 : 0)
		);
		const NSSize minimum = window.contentMinSize;
		const NSSize maximum = window.contentMaxSize;
		size.width = std::clamp(size.width, minimum.width, maximum.width);
		size.height = std::clamp(size.height, minimum.height, maximum.height);
		const auto &configuration = state->appliedConfiguration->requested;
		if (configuration.resizeIncrement) {
			size.width = std::floor(size.width / configuration.resizeIncrement->width) *
				configuration.resizeIncrement->width;
			size.height = std::floor(size.height / configuration.resizeIncrement->height) *
				configuration.resizeIncrement->height;
		}
		if (configuration.aspectRatio) {
			size.height = size.width / *configuration.aspectRatio;
		}
		const NSRect rect = NSMakeRect(
			left ? NSMaxX(start) - size.width : start.origin.x,
			bottom ? NSMaxY(start) - size.height : start.origin.y,
			size.width,
			size.height
		);
		[window setFrame:[window frameRectForContentRect:rect] display:YES];
	}
	complete(
		{.outcome = state->isClosed ? NativeWindowOutcome::cancelled : NativeWindowOutcome::applied}
	);
}

namespace {
/// @brief Owns a capture-only compositor for CPU windows; shares existing packets for GPU windows.
struct CaptureJob : std::enable_shared_from_this<CaptureJob> {
	std::weak_ptr<WindowState> state;
	std::shared_ptr<gpu::Compositor> compositor;
	std::shared_ptr<const PreparedScene> scene;
	TileGrid grid;
	NativeActionCompletion complete;
	bool printing = false;
	unsigned attempts = 0;

	void deliver(gpu::Result<gpu::Capture> captured) {
		const auto owner = state.lock();
		if (!owner || owner->isClosed) {
			complete({.outcome = NativeWindowOutcome::cancelled});
			return;
		}
		if (!captured) {
			complete({.outcome = NativeWindowOutcome::failed});
			return;
		}
		const auto &source = *captured;
		NativeActionResult result{
			.outcome = NativeWindowOutcome::applied,
			.width = source.width,
			.height = source.height
		};
		const auto bytes = std::span<const std::byte>{source.bytes};
		constexpr std::size_t channels = 4;
		const auto count = static_cast<std::size_t>(source.width) * source.height;
		result.pixels.resize(count * channels);
		for (std::size_t pixel = 0; pixel < count; ++pixel) {
			std::array<double, channels> color{};
			if (source.format == gpu::Format::rgba16) {
				for (std::size_t channel = 0; channel < channels; ++channel) {
					color.at(channel) =
						::stana::gpu::detail::halfAt(bytes, pixel * halfPixelBytes + channel * 2);
				}
				color = ::stana::gpu::detail::displayMap(color, 1);
				for (std::size_t channel = 0; channel < 3; ++channel) {
					color.at(channel) = color.at(3) > 0.0
						? ::stana::gpu::detail::encodeSRGB(color.at(channel) / color.at(3))
						: 0.0;
				}
			} else {
				constexpr double maximum = 255;
				color = {
					static_cast<double>(std::to_integer<unsigned>(
						::stana::gpu::detail::byteAt(bytes, pixel * channels + 2)
					)) / maximum,
					static_cast<double>(std::to_integer<unsigned>(
						::stana::gpu::detail::byteAt(bytes, pixel * channels + 1)
					)) / maximum,
					static_cast<double>(std::to_integer<unsigned>(
						::stana::gpu::detail::byteAt(bytes, pixel * channels)
					)) / maximum,
					static_cast<double>(std::to_integer<unsigned>(
						::stana::gpu::detail::byteAt(bytes, pixel * channels + 3)
					)) / maximum
				};
			}
			if (source.format != gpu::Format::rgba16) {
				for (std::size_t channel = 0; channel < 3; ++channel) {
					color.at(channel) = color.at(3) > 0.0 ? color.at(channel) / color.at(3) : 0.0;
				}
			}
			for (std::size_t channel = 0; channel < channels; ++channel) {
				const double normalized =
					std::isnan(color.at(channel)) ? 0.0 : std::clamp(color.at(channel), 0.0, 1.0);
				result.pixels.at(pixel * channels + channel) =
					static_cast<std::uint8_t>(std::lround(normalized * maximumByte));
			}
		}
		if (printing) {
			NSBitmapImageRep *const bitmap = [[NSBitmapImageRep alloc]
				initWithBitmapDataPlanes:nullptr
							  pixelsWide:source.width
							  pixelsHigh:source.height
						   bitsPerSample:8
						 samplesPerPixel:4
								hasAlpha:YES
								isPlanar:NO
						  colorSpaceName:NSCalibratedRGBColorSpace
							bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
							 bytesPerRow:static_cast<NSInteger>(source.width) * 4
							bitsPerPixel:32];
			if (bitmap == nil) {
				complete({.outcome = NativeWindowOutcome::failed});
				return;
			}
			if (bitmap.bitmapData == nullptr) {
				complete({.outcome = NativeWindowOutcome::failed});
				return;
			}
			[[NSData dataWithBytes:result.pixels.data()
							length:result.pixels.size()] getBytes:bitmap.bitmapData
														   length:result.pixels.size()];
			NSImage *const image = [[NSImage alloc]
				initWithSize:NSMakeSize(source.width / source.scale, source.height / source.scale)];
			[image addRepresentation:bitmap];
			NSImageView *const view = [[NSImageView alloc]
				initWithFrame:NSMakeRect(0, 0, image.size.width, image.size.height)];
			view.image = image;
			NSPrintOperation *const operation = [NSPrintOperation printOperationWithView:view];
			if ([operation.printPanel runModalWithPrintInfo:operation.printInfo] !=
				NSModalResponseOK) {
				complete({.outcome = NativeWindowOutcome::cancelled});
				return;
			}
			operation.showsPrintPanel = NO;
			const BOOL success = [operation runOperation];
			const auto session = static_cast<PMPrintSession>(operation.printInfo.PMPrintSession);
			const auto status = PMSessionError(session);
			complete(
				{.outcome = success			  ? NativeWindowOutcome::applied
						: status == kPMCancel ? NativeWindowOutcome::cancelled
											  : NativeWindowOutcome::failed}
			);

		} else {
			complete(std::move(result));
		}
	}

	void step() {
		const auto owner = state.lock();
		if (!owner || owner->isClosed) {
			complete({.outcome = NativeWindowOutcome::cancelled});
			return;
		}
		if (owner->grid.viewport.width != grid.viewport.width ||
			owner->grid.viewport.height != grid.viewport.height) {
			complete({.outcome = NativeWindowOutcome::cancelled});
			return;
		}
		if (++attempts > captureAttempts) {
			complete({.outcome = NativeWindowOutcome::failed});
			return;
		}
		auto packet =
			owner->hybridSurface ? owner->compositor->readyPacket() : compositor->readyPacket();
		if (packet && packet->scene->source->revision != scene->source->revision) {
			packet.reset();
		}
		if (!packet) {
			const auto activeCompositor = owner->hybridSurface ? owner->compositor : compositor;
			activeCompositor->desired(scene->source);
			const auto prepared = activeCompositor->prepare(
				scene,
				grid,
				owner->presentationEpoch,
				owner->hybridSurface ? owner->callbacks.colorMode
									 : ::stana::gpu::detail::ColorMode::sdr
			);
			if (!prepared) {
				complete({.outcome = NativeWindowOutcome::failed});
				return;
			}
			packet = *prepared ? activeCompositor->readyPacket() : nullptr;
		}
		if (!packet) {
			const auto job = shared_from_this();
			postAfter(captureInterval, [job] -> void {
				job->step();
			});
			return;
		}
		const auto job = shared_from_this();
		gpu::capture(
			owner->compositor->resources().device,
			packet,
			grid,
			[job](gpu::Result<gpu::Capture> result) -> void {
				postAfter(
					std::chrono::milliseconds{0},
					[job, captured = std::move(result)] -> void {
						job->deliver(captured);
					}
				);
			}
		);
	}
};
} // namespace

void captureWindowContent(
	NSWindow *window,
	const std::shared_ptr<WindowState> &state,
	bool printing,
	NativeActionCompletion complete
) {
	static_cast<void>(window);
	const auto source = state->callbacks.scene();
	if (!source || state->grid.tiles.empty()) {
		complete({.outcome = NativeWindowOutcome::failed});
		return;
	}
	const auto compositor =
		gpu::Compositor::create(state->compositor->resources().device, state->accounting);
	if (!compositor) {
		complete({.outcome = NativeWindowOutcome::failed});
		return;
	}
	auto job = std::make_shared<CaptureJob>();
	job->state = state;
	job->compositor = *compositor;
	job->grid = state->grid;
	job->scene = prepareScene(*source, state->scale);
	job->printing = printing;
	job->complete = std::move(complete);
	job->compositor->desired(*source);
	job->complete({.outcome = NativeWindowOutcome::pending});
	job->step();
}
} // namespace stana::platform::detail
