#include <iostream>
#include <stdexcept>
#include <string>
import stana;

namespace {
constexpr std::size_t trailingLineStart = 5;
constexpr stana::Points naturalLineHeight{20};
constexpr stana::Points centeredLineX{24};
constexpr stana::Points explicitLineHeight{30};
constexpr stana::Points middleCaretX{40};
constexpr stana::Points selectionWidth{16};
constexpr stana::Points bidiAdvance{8};

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void wrappingAndHardBreaks() {
	const auto layout = stana::text::layoutText(
		{.text = "ab cd", .wrap = stana::text::Wrap::word, .width = centeredLineX}
	);
	require(
		layout->lines.size() == 2 && layout->lines.front().source.end == 3,
		"word wrap source ranges"
	);
	const auto hard = stana::text::layoutText({.text = "a\r\nb\n"});
	require(
		hard->lines.size() == 3 && hard->lines.front().source.end == 3 &&
			hard->lines.back().source.start == trailingLineStart,
		"hard/trailing breaks"
	);
	const auto small = stana::text::layoutText(
		{.text = "👩‍👩‍👧‍👦x",
			.wrap = stana::text::Wrap::grapheme,
			.width = stana::Points{1}}
	);
	require(
		small->lines.size() == 2 &&
			small->lines.front().inkEnd == std::string{"👩‍👩‍👧‍👦"}.size(),
		"never split oversized grapheme"
	);
	const auto zero = stana::text::layoutText(
		{.text = "abcdef", .wrap = stana::text::Wrap::word, .width = stana::Points{0}}
	);
	require(
		zero->lines.size() == 1 && zero->size.height == naturalLineHeight,
		"zero width makes progress"
	);
}

void geometryAndTypography() {
	const auto layout = stana::text::layoutText(
		{.text = "abcd",
			.width = stana::Points{80},
			.lineHeight = explicitLineHeight,
			.alignment = stana::Alignment::center}
	);
	require(
		layout->lines.front().origin.x == centeredLineX &&
			layout->size.height == explicitLineHeight,
		"alignment and explicit line height"
	);
	require(
		layout->hit({.x = middleCaretX}) == 2 && layout->caret(2).origin.x == middleCaretX,
		"hit and caret share geometry"
	);
	require(
		layout->selection({.start = 1, .end = 3}).front().size.width == selectionWidth,
		"selection fragments"
	);
	const auto limited = stana::text::layoutText({.text = "a\nb\nc", .maxLines = 2});
	require(
		limited->lines.size() == 2 && limited->request.text == "a\nb\nc",
		"line limit clips without changing source"
	);
	const auto empty =
		stana::text::layoutText({.text = "", .font = stana::Font::system(stana::Points{})});
	require(
		empty->lines.empty() && empty->size == stana::Size{},
		"zero font has no resources or geometry"
	);
	const auto scaledZero = stana::text::layoutText(
		{.text = "zero",
			.font = stana::Font::system(stana::Points{}),
			.lineHeight = stana::Points{}}
	);
	require(
		scaledZero->size == stana::Size{} && scaledZero->lines.empty(),
		"zero scale permits a resolved zero line height"
	);
}

void bidiSelectionFragments() {
	constexpr std::size_t rtlMiddle = 4;
	constexpr std::size_t rtlEnd = 6;
	const auto layout = stana::text::layoutText(
		{.text = "abאב"},
		[](const std::string &, const stana::Font &, stana::Direction) -> stana::text::ShapedLine {
			return {
				.width = selectionWidth * 2,
				.ascent = naturalLineHeight,
				.carets =
					{{.offset = 0},
						{.offset = 1, .primary = bidiAdvance, .secondary = bidiAdvance},
						{.offset = 2, .primary = selectionWidth, .secondary = selectionWidth * 2},
						{.offset = rtlMiddle,
							.primary = selectionWidth + bidiAdvance,
							.secondary = selectionWidth + bidiAdvance},
						{.offset = rtlEnd, .primary = selectionWidth, .secondary = selectionWidth}},
				.runs = {
					{.source = {.start = 0, .end = 2}, .width = selectionWidth},
					{.source = {.start = 2, .end = rtlEnd},
						.x = selectionWidth,
						.width = selectionWidth,
						.rightToLeft = true}
				}
			};
		}
	);
	const auto rtl = layout->selection({.start = 2, .end = rtlEnd});
	require(
		rtl.size() == 1 && rtl.front().origin.x == selectionWidth &&
			rtl.front().size.width == selectionWidth,
		"RTL run boundaries use their own visual affinity"
	);
	const auto split = layout->selection({.start = 1, .end = rtlMiddle});
	require(
		split.size() == 2 && split.front().size.width == bidiAdvance &&
			split.at(1).size.width == bidiAdvance &&
			split.at(1).origin.x == selectionWidth + bidiAdvance,
		"mixed-direction selections retain disjoint fragments"
	);
}

} // namespace

auto main() -> int {
	try {
		wrappingAndHardBreaks();
		geometryAndTypography();
		bidiSelectionFragments();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
