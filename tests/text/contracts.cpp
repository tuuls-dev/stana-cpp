#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

import stana;

namespace {
void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

/// @brief Covers grapheme-based editing and exact byte/code-unit conversion boundaries.
void unicodeBoundaries() {

	const std::string combining = "e\xCC\x81";
	const std::string emoji = "\xF0\x9F\x98\x80";
	const std::string family = "👩‍👩‍👧‍👦";
	require(
		stana::text::isValidUtf8(combining) && stana::text::isValidUtf8(emoji),
		"valid scalars"
	);
	require(
		!stana::text::isValidUtf8("\xC0\x80") && !stana::text::isValidUtf8("\xED\xA0\x80") &&
			!stana::text::isValidUtf8("\xF0\x9F"),
		"reject noncanonical or truncated UTF-8"
	);
	require(
		stana::text::graphemeBoundaries(combining) ==
			std::vector<std::size_t>({0, combining.size()}),
		"combining cluster"
	);
	require(
		stana::text::graphemeBoundaries(family) == std::vector<std::size_t>({0, family.size()}),
		"ZWJ family cluster"
	);
	require(stana::text::utf16Offset(emoji, emoji.size()) == 2, "supplementary UTF-16 length");
	require(stana::text::utf8Offset(emoji, 2) == emoji.size(), "UTF-16 round trip");
	require(
		!stana::text::utf8Offset(emoji, 1) && !stana::text::utf16Offset(emoji, 1),
		"reject split surrogate/scalar"
	);
	const std::string embeddedNul{"a\0b", 3};
	require(
		stana::text::graphemeBoundaries(embeddedNul).size() == embeddedNul.size() + 1,
		"embedded NUL is content"
	);
	stana::text::TextBuffer joining{"\xCC\x81"};
	require(joining.replaceSelection("e").has_value(), "insert before a combining mark");
	require(
		joining.selection().head.utf8Offset == joining.text().size(),
		"insertion caret stays at a resulting grapheme boundary"
	);
	stana::text::TextBuffer buffer{combining};
	const auto revision = buffer.revision();
	require(
		!buffer.select(
			{.anchor = {.utf8Offset = 1, .revision = revision},
				.head = {.utf8Offset = 1, .revision = revision}}
		),
		"no caret inside grapheme"
	);
	require(
		buffer
			.select(
				{.anchor = {.revision = revision},
					.head = {.utf8Offset = combining.size(), .revision = revision}}
			)
			.has_value(),
		"select cluster"
	);
	require(
		buffer.replaceSelection(emoji).has_value() && buffer.text() == emoji,
		"replace cluster"
	);
	require(
		!buffer.select({.anchor = {.revision = revision}, .head = {.revision = revision}}),
		"reject stale revision"
	);
	require(buffer.undo() && buffer.text() == combining, "undo full cluster");
	require(buffer.redo() && buffer.text() == emoji, "redo full cluster");
}

/// @brief Ensures equal external binding echoes do not cancel a native IME composition.
void compositionAndUndo() {

	stana::text::TextBuffer buffer;
	require(buffer.replaceSelection("a").has_value(), "initial insertion");
	require(buffer.replaceSelection("b", true).has_value(), "grouped insertion");
	require(buffer.undo() && buffer.text().empty(), "one undo per group");
	require(buffer.redo() && buffer.text() == "ab", "redo group");
	require(buffer.setMarkedText("候補").has_value(), "provisional text");
	const auto revision = buffer.revision();
	require(
		buffer.setText("ab").has_value() && buffer.markedText().has_value() &&
			buffer.revision() == revision,
		"equal echo preserves composition"
	);
	require(
		buffer.setText("external").has_value() && !buffer.markedText(),
		"different binding cancels composition"
	);
	require(
		buffer.setMarkedText("文").has_value() && buffer.commitMarkedText().has_value(),
		"commit IME"
	);
	require(
		buffer.text() == "文external" && !buffer.markedText(),
		"committed model and composition"
	);
	require(!buffer.replaceSelection("\n"), "single-line insertion policy");
}
} // namespace

auto main() -> int {
	try {
		unicodeBoundaries();
		compositionAndUndo();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
