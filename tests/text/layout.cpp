#include <iostream>
#include <stdexcept>
#include <string>
import stana;

namespace {
void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void wrappingAndHardBreaks() {
	const auto layout = stana::text::layoutText(
		{.text = "ab cd", .wrap = stana::text::Wrap::word, .width = stana::Points{24}}
	);
	require(
		layout->lines.size() == 2 && layout->lines.front().source.end == 3,
		"word wrap source ranges"
	);
	const auto hard = stana::text::layoutText({.text = "a\r\nb\n"});
	require(
		hard->lines.size() == 3 && hard->lines.front().source.end == 3 &&
			hard->lines.back().source.start == 5,
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
		zero->lines.size() == 1 && zero->size.height == stana::Points{20},
		"zero width makes progress"
	);
}

void geometryAndTypography() {
	const auto layout = stana::text::layoutText(
		{.text = "abcd",
			.width = stana::Points{80},
			.lineHeight = stana::Points{30},
			.alignment = stana::Alignment::center}
	);
	require(
		layout->lines.front().origin.x == stana::Points{24} &&
			layout->size.height == stana::Points{30},
		"alignment and explicit line height"
	);
	require(
		layout->hit({.x = stana::Points{40}}) == 2 &&
			layout->caret(2).origin.x == stana::Points{40},
		"hit and caret share geometry"
	);
	require(
		layout->selection({1, 3}).front().size.width == stana::Points{16},
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
}
} // namespace

auto main() -> int {
	try {
		wrappingAndHardBreaks();
		geometryAndTypography();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
