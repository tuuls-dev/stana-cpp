#include "../../../src/platform/macos/host_view.hpp"
#include "fixtures.hpp"
#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>

namespace {
namespace native = stana::platform::detail;
using stana::testing::fixtures::platform::macos::require;

void selectorsAndRanges() {
	STANAView *const view = [[STANAView alloc] initWithFrame:NSMakeRect(0, 0, 200, 40)];
	view->state_ = std::make_shared<native::WindowState>();
	view->state_->confirmedRevision = 1;
	const auto current = std::make_shared<native::NativeTextState>(native::NativeTextState{
		.target = {.document = 1, .node = 2, .session = 3, .store = 4},
		.displayed = "a😀b",
		.selected = {1, 3},
		.marked = native::NativeTextRange{1, 3}
	});
	const auto edits = std::make_shared<std::vector<native::NativeTextEdit>>();
	view->state_->callbacks.textInput.state = [current] {
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

void shapedResourcesAndPixels(double scale) {
	const auto shaped = native::shapeTextLine(
		"e\xCC\x81😀",
		{.size = 20, .weight = 0, .family = "Helvetica", .italic = true},
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
	const auto line = native::shapeTextLine("Italic ffi", {.size = 20, .italic = true}, false);
	require(line.has_value(), "shape pixel fixture");
	auto source = std::make_shared<native::Scene>();
	source->revision = 1;
	source->size = {.width = 160, .height = 40};
	source->background = {.red = 1, .green = 1, .blue = 1};
	auto paint = std::make_shared<native::PaintSegments>();
	paint->identity = 1;
	paint->prefix.push_back(
		{.operation = native::PaintOperation::text,
			.x = 5,
			.y = 5,
			.width = line->width,
			.height = 30,
			.text = "Italic ffi",
			.fontSize = 20,
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
		.width = static_cast<std::size_t>(160 * scale),
		.height = static_cast<std::size_t>(40 * scale)
	};
	stana::testing::fixtures::platform::macos::Bitmap first{rectangle}, second{rectangle};
	native::rasterScene(first.context.get(), *prepared, rectangle);
	native::rasterScene(
		second.context.get(),
		*native::prepareScene(source, scale, prepared),
		rectangle
	);
	require(first.bytes == second.bytes, "incremental native text pixels match fresh replay");
	const auto zero = native::shapeTextLine("zero", {.size = 0}, false);
	require(zero && !zero->resource, "zero font never allocates native glyphs");
}
} // namespace

auto main() -> int {
	@autoreleasepool {
		try {
			selectorsAndRanges();
			shapedResourcesAndPixels(1);
			shapedResourcesAndPixels(2);
			return 0;
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	}
}
