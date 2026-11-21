#include "../../../src/platform/macos/host_view.hpp"
#include "fixtures.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>

namespace {
namespace native = stana::platform::detail;
using stana::testing::fixtures::platform::macos::require;
constexpr double previewWidth = 160;
constexpr double previewHeight = 40;
constexpr double textOrigin = 5;
constexpr double lineHeight = 30;
constexpr double fontSize = 20;
constexpr NSInteger menuItemCount = 8;
constexpr NSInteger copyItemIndex = 4;
constexpr unsigned char darkPixelThreshold = 240;

void selectorsAndRanges() {
	STANAView *const view = [[STANAView alloc] initWithFrame:NSMakeRect(0, 0, 200, 40)];
	view->state_ = std::make_shared<native::WindowState>();
	view->state_->confirmedRevision = 1;
	const auto current = std::make_shared<native::NativeTextState>(native::NativeTextState{
		.target = {.document = 1, .node = 2, .session = 3, .store = 4},
		.displayed = "a😀b",
		.selected = {.start = 1, .end = 3},
		.marked = native::NativeTextRange{.start = 1, .end = 3}
	});
	const auto edits = std::make_shared<std::vector<native::NativeTextEdit>>();
	view->state_->callbacks.textInput.state =
		[current] -> std::optional<stana::platform::detail::NativeTextState> {
		return std::optional{*current};
	};
	view->state_->callbacks.textInput.begin = [] -> std::uint64_t {
		return 1;
	};
	view->state_->callbacks.textInput.end = [](std::uint64_t) -> void {};
	view->state_->callbacks.textInput.edit = [edits](
												 native::NativeTextTarget,
												 std::uint64_t,
												 const native::NativeTextEdit &edit
											 ) -> bool {
		edits->push_back(edit);
		return true;
	};
	require(
		[view hasMarkedText] && NSEqualRanges([view selectedRange], NSMakeRange(1, 2)),
		"native displayed ranges"
	);
	NSRange actual{};
	require(
		[view attributedSubstringForProposedRange:NSMakeRange(1, 2) actualRange:&actual] != nil &&
			NSEqualRanges(actual, NSMakeRange(1, 2)),
		"supplementary scalar query"
	);
	require(
		[view attributedSubstringForProposedRange:NSMakeRange(2, 1) actualRange:&actual] == nil &&
			actual.location == NSNotFound,
		"reject split surrogate query"
	);
	[view setMarkedText:@"候補"
		   selectedRange:NSMakeRange(2, 0)
		replacementRange:NSMakeRange(NSNotFound, 0)];
	require(
		edits->back().kind == native::NativeTextEdit::Kind::mark && !edits->back().replacement &&
			edits->back().selection.start == 2,
		"marked callback transport"
	);
	[view insertText:[[NSAttributedString alloc] initWithString:@"確定"]
		replacementRange:NSMakeRange(1, 2)];
	require(
		edits->back().kind == native::NativeTextEdit::Kind::insert &&
			edits->back().replacement->end == 3,
		"attributed insertion transport"
	);
	[view unmarkText];
	require(
		edits->back().kind == native::NativeTextEdit::Kind::commit,
		"unmark commits through shared editor path"
	);
	require(
		NSEqualRects(
			[view firstRectForCharacterRange:NSMakeRange(0, 0) actualRange:&actual],
			NSZeroRect
		) && actual.location == NSNotFound,
		"unavailable geometry stays unavailable"
	);
}

void contextMenuCommands() {
	STANAView *const view =
		[[STANAView alloc] initWithFrame:NSMakeRect(0, 0, previewWidth, previewHeight)];
	view->state_ = std::make_shared<native::WindowState>();
	view->state_->confirmedRevision = 1;
	const auto target = std::make_shared<native::NativeTextTarget>(
		native::NativeTextTarget{.document = 1, .node = 2, .session = 3, .store = 4}
	);
	const auto actions = std::make_shared<std::size_t>(0);
	view->state_->callbacks.textInput.contextMenu =
		[target](double, double) -> std::optional<native::NativeTextTarget> {
		return *target;
	};
	view->state_->callbacks.textInput.canEdit =
		[target](native::NativeTextTarget captured, native::TextCommand) -> bool {
		return captured.store == target->store;
	};
	view->state_->callbacks.textInput.command = [target, actions](
													native::NativeTextTarget captured,
													std::uint64_t,
													native::TextCommand command
												) -> bool {
		if (captured.store != target->store || command != native::TextCommand::copy) {
			return false;
		}
		++*actions;
		return true;
	};
	view->state_->callbacks.textInput.begin = [] -> std::uint64_t {
		return 1;
	};
	view->state_->callbacks.textInput.end = [](std::uint64_t) -> void {};
	NSEvent *const event = [NSEvent mouseEventWithType:NSEventTypeRightMouseDown
											  location:NSMakePoint(textOrigin, textOrigin)
										 modifierFlags:0
											 timestamp:0
										  windowNumber:0
											   context:nil
										   eventNumber:1
											clickCount:1
											  pressure:1];
	NSMenu *const menu = [view menuForEvent:event];
	require(menu != nil && menu.numberOfItems == menuItemCount, "standard native editing menu");
	NSMenuItem *const copy = [menu itemAtIndex:copyItemIndex];
	require([view validateMenuItem:copy], "menu availability uses portable command query");
	[view performContextTextCommand:copy];
	require(*actions == 1, "native menu enters shared command callback");
	++target->store;
	require(![view validateMenuItem:copy], "menu validation rejects changed store");
	[view performContextTextCommand:copy];
	require(*actions == 1, "stale menu does not act on a new selection");
}

void reentrantInputTarget() {
	STANAView *const view =
		[[STANAView alloc] initWithFrame:NSMakeRect(0, 0, previewWidth, previewHeight)];
	view->state_ = std::make_shared<native::WindowState>();
	const auto current = std::make_shared<native::NativeTextState>(
		native::NativeTextState{.target = {.document = 1, .node = 2, .session = 3, .store = 4}}
	);
	const auto calls = std::make_shared<std::size_t>(0);
	view->state_->callbacks.textInput.state = [current] -> std::optional<native::NativeTextState> {
		return *current;
	};
	view->state_->callbacks.textInput.begin = [] -> std::uint64_t {
		return 1;
	};
	view->state_->callbacks.textInput.end = [](std::uint64_t) -> void {};
	__weak STANAView *const weak = view;
	view->state_->callbacks.textInput.edit =
		[current,
			calls,
			weak](native::NativeTextTarget, std::uint64_t, const native::NativeTextEdit &) -> bool {
		++*calls;
		if (*calls == 1) {
			++current->target.node;
			STANAView *const live = weak;
			[live insertText:@"b" replacementRange:NSMakeRange(NSNotFound, 0)];
		}
		return true;
	};
	[view insertText:@"a" replacementRange:NSMakeRange(NSNotFound, 0)];
	require(
		*calls == 1 && view->textSequence_ == 0 && !view->textSequenceTarget_,
		"reentrant insertion cannot retarget another editor"
	);
	*calls = 0;
	view->state_->callbacks.textInput.key = [current](native::NativeTextKey, bool, bool) -> bool {
		++current->target.node;
		return true;
	};
	NSEvent *const event = [NSEvent keyEventWithType:NSEventTypeKeyDown
											location:NSZeroPoint
									   modifierFlags:0
										   timestamp:0
										windowNumber:0
											 context:nil
										  characters:@"a"
						 charactersIgnoringModifiers:@"a"
										   isARepeat:NO
											 keyCode:0];
	require(
		[view handleTextKey:event] && *calls == 0,
		"focus redirection during key routing cancels native defaults"
	);
}

void shapedResourcesAndPixels(double scale) {
	const auto shaped = native::shapeTextLine(
		"e\xCC\x81😀",
		{.size = fontSize, .weight = 0, .family = "Helvetica", .italic = true},
		false
	);
	require(
		shaped.has_value() && shaped->resource && shaped->width > 0,
		"native shaping owns glyph resources"
	);
	require(
		std::ranges::none_of(
			shaped->carets,
			[](const auto &stop) -> bool {
				return stop.utf16Offset == 3;
			}
		),
		"native stops exclude split surrogate"
	);
	const auto line =
		native::shapeTextLine("Italic ffi", {.size = fontSize, .italic = true}, false);
	require(line.has_value(), "shape pixel fixture");
	auto source = std::make_shared<native::Scene>();
	source->revision = 1;
	source->size = {.width = previewWidth, .height = previewHeight};
	source->background = {.red = 1, .green = 1, .blue = 1};
	auto paint = std::make_shared<native::PaintSegments>();
	paint->identity = 1;
	paint->prefix.push_back(
		{.operation = native::PaintOperation::text,
			.x = textOrigin,
			.y = textOrigin,
			.width = line->width,
			.height = lineHeight,
			.text = "Italic ffi",
			.fontSize = fontSize,
			.fontItalic = true,
			.textResource = line->resource,
			.textBaseline = line->ascent}
	);
	auto branch = std::make_shared<native::SceneBranch>();
	branch->id = {.document = 1, .serial = 1};
	branch->generation = 1;
	branch->paint = paint;
	source->root = branch;
	const auto prepared = native::prepareScene(source, scale, {});
	require(
		prepared->root->paint->prefix.front().text.get() == line->resource.get(),
		"painting reuses shaped resource"
	);
	const native::PixelRect rectangle{
		.width = static_cast<std::size_t>(previewWidth * scale),
		.height = static_cast<std::size_t>(previewHeight * scale)
	};
	stana::testing::fixtures::platform::macos::Bitmap first{rectangle}, second{rectangle};
	native::rasterScene(first.context.get(), *prepared, rectangle);
	native::rasterScene(
		second.context.get(),
		*native::prepareScene(source, scale, prepared),
		rectangle
	);
	require(first.bytes == second.bytes, "incremental native text pixels match fresh replay");
	bool hasGlyphInk = false;
	for (std::size_t y = 0; y < rectangle.height; ++y) {
		for (std::size_t x = 0; x < rectangle.width; ++x) {
			const auto offset = y * first.bytesPerRow + x * native::bytesPerPixel;
			if (std::to_integer<unsigned char>(first.bytes.at(offset + 3)) != 0 &&
				std::to_integer<unsigned char>(first.bytes.at(offset)) < darkPixelThreshold) {
				hasGlyphInk = true;
			}
		}
	}
	require(hasGlyphInk, "shared glyph resources produce actual native pixel ink");
	const auto zero = native::shapeTextLine("zero", {.size = 0}, false);
	require(zero && !zero->resource, "zero font never allocates native glyphs");
}
} // namespace

auto main() -> int {
	@autoreleasepool {
		try {
			selectorsAndRanges();
			contextMenuCommands();
			reentrantInputTarget();
			shapedResourcesAndPixels(1);
			shapedResourcesAndPixels(2);
			return 0;
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	}
}
