module;

#include <unicode/brkiter.h>
#include <unicode/locid.h>
#include <unicode/stringpiece.h>
#include <unicode/unistr.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module stana.text;
import stana.primitives;

namespace stana {
namespace {
constexpr unsigned char continuationMask = 0xC0;
constexpr unsigned char continuationTag = 0x80;
constexpr unsigned char asciiLimit = 0x80;
constexpr unsigned char twoByteMinimum = 0xC2;
constexpr unsigned char twoByteMaximum = 0xDF;
constexpr unsigned char threeByteMinimum = 0xE0;
constexpr unsigned char threeByteMaximum = 0xEF;
constexpr unsigned char fourByteMinimum = 0xF0;
constexpr unsigned char fourByteMaximum = 0xF4;
constexpr std::uint32_t surrogateStart = 0xD800;
constexpr std::uint32_t surrogateEnd = 0xDFFF;
constexpr std::uint32_t unicodeMaximum = 0x10FFFF;
constexpr std::uint32_t minimumTwoByteScalar = 0x80;
constexpr std::uint32_t minimumThreeByteScalar = 0x800;
constexpr std::uint32_t minimumFourByteScalar = 0x10000;
constexpr unsigned int continuationBits = 6;
constexpr unsigned char continuationValueMask = 0x3F;

/// @brief Validates a native-facing string before ICU can replace malformed input.
[[nodiscard]] auto unicodeText(const std::string &text) -> icu::UnicodeString {
	if (!isValidUtf8(text) ||
		text.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
		throw std::invalid_argument("Stana requires valid UTF-8 within ICU's text limit");
	}
	return icu::UnicodeString::fromUTF8(
		icu::StringPiece{text.data(), static_cast<std::int32_t>(text.size())}
	);
}
} // namespace

auto isValidUtf8(std::string_view text) noexcept -> bool {
	std::size_t remaining = 0;
	std::uint32_t scalar = 0;
	std::uint32_t minimum = 0;
	for (const char character : text) {
		const auto byte = static_cast<unsigned char>(character);
		if (remaining != 0) {
			if ((byte & continuationMask) != continuationTag) {
				return false;
			}
			scalar = (scalar << continuationBits) | (byte & continuationValueMask);
			--remaining;
			if (remaining == 0 &&
				(scalar < minimum || scalar > unicodeMaximum ||
					(scalar >= surrogateStart && scalar <= surrogateEnd))) {
				return false;
			}
		} else if (byte < asciiLimit) {
			continue;
		} else if (byte >= twoByteMinimum && byte <= twoByteMaximum) {
			constexpr unsigned char leadingMask = 0x1F;
			remaining = 1;
			scalar = byte & leadingMask;
			minimum = minimumTwoByteScalar;
		} else if (byte >= threeByteMinimum && byte <= threeByteMaximum) {
			constexpr unsigned char leadingMask = 0x0F;
			remaining = 2;
			scalar = byte & leadingMask;
			minimum = minimumThreeByteScalar;
		} else if (byte >= fourByteMinimum && byte <= fourByteMaximum) {
			constexpr unsigned char leadingMask = 0x07;
			remaining = 3;
			scalar = byte & leadingMask;
			minimum = minimumFourByteScalar;
		} else {
			return false;
		}
	}
	return remaining == 0;
}

auto graphemeBoundaries(const std::string &text) -> std::vector<std::size_t> {
	const auto unicode = unicodeText(text);
	UErrorCode status = U_ZERO_ERROR;
	const std::unique_ptr<icu::BreakIterator> iterator{
		icu::BreakIterator::createCharacterInstance(icu::Locale::getRoot(), status)
	};
	if (U_FAILURE(status) != 0 || !iterator) {
		throw std::runtime_error("Unable to create ICU grapheme iterator");
	}
	iterator->setText(unicode);
	std::vector<std::size_t> boundaries;
	for (auto boundary = iterator->first(); boundary != icu::BreakIterator::DONE;
		boundary = iterator->next()) {
		std::string prefix;
		unicode.tempSubStringBetween(0, boundary).toUTF8String(prefix);
		boundaries.push_back(prefix.size());
	}
	return boundaries;
}

auto utf16Offset(const std::string &text, std::size_t byteOffset)
	-> std::expected<std::size_t, Error> {
	if (!isValidUtf8(text) || byteOffset > text.size()) {
		return std::unexpected(Error::invalidTextRange);
	}
	const auto prefix = text.substr(0, byteOffset);
	if (!isValidUtf8(prefix)) {
		return std::unexpected(Error::invalidTextRange);
	}
	return static_cast<std::size_t>(unicodeText(prefix).length());
}

auto utf8Offset(const std::string &text, std::size_t codeUnitOffset)
	-> std::expected<std::size_t, Error> {
	if (!isValidUtf8(text)) {
		return std::unexpected(Error::invalidTextRange);
	}
	const auto unicode = unicodeText(text);
	if (std::cmp_greater(codeUnitOffset, unicode.length())) {
		return std::unexpected(Error::invalidTextRange);
	}
	if (std::cmp_equal(codeUnitOffset, unicode.length())) {
		return text.size();
	}
	const auto offset = static_cast<std::int32_t>(codeUnitOffset);
	if (unicode.getChar32Start(offset) != offset) {
		return std::unexpected(Error::invalidTextRange);
	}
	std::string prefix;
	unicode.tempSubStringBetween(0, offset).toUTF8String(prefix);
	return prefix.size();
}

TextBuffer::TextBuffer(std::string text)
	: text_(std::move(text)),
	  revision_(detail::nextIdentity()) {
	if (!isValidUtf8(text_) || text_.find_first_of("\r\n") != std::string::npos) {
		throw std::invalid_argument("Malformed initial editor text");
	}
}

auto TextBuffer::selection() const noexcept -> TextSelection {
	return {
		.anchor = {.utf8Offset = anchor_, .revision = revision_},
		.head = {.utf8Offset = head_, .revision = revision_}
	};
}

auto TextBuffer::select(TextSelection selection) -> std::expected<void, Error> {
	if (selection.anchor.revision != revision_ || selection.head.revision != revision_) {
		return std::unexpected(Error::invalidTextRange);
	}
	const auto boundaries = graphemeBoundaries(text_);
	if (!std::ranges::binary_search(boundaries, selection.anchor.utf8Offset) ||
		!std::ranges::binary_search(boundaries, selection.head.utf8Offset)) {
		return std::unexpected(Error::invalidTextRange);
	}
	anchor_ = selection.anchor.utf8Offset;
	head_ = selection.head.utf8Offset;
	return {};
}

auto TextBuffer::currentState() const -> EditState {
	return {.text = text_, .anchor = anchor_, .head = head_};
}

void TextBuffer::restore(EditState state) {
	text_ = std::move(state.text);
	anchor_ = state.anchor;
	head_ = state.head;
	revision_ = detail::nextIdentity();
	marked_.reset();
}

auto TextBuffer::replaceSelection(const std::string &insertion, bool groupWithPrevious)
	-> std::expected<void, Error> {
	if (!isValidUtf8(insertion) || insertion.find_first_of("\r\n") != std::string::npos) {
		return std::unexpected(Error::invalidValue);
	}
	if (!groupWithPrevious || undo_.empty()) {
		undo_.push_back(currentState());
	}
	redo_.clear();
	const auto begin = std::min(anchor_, head_);
	const auto end = std::max(anchor_, head_);
	text_.replace(begin, end - begin, insertion);
	const auto boundaries = graphemeBoundaries(text_);
	const auto caret = std::ranges::lower_bound(boundaries, begin + insertion.size());
	anchor_ = caret == boundaries.end() ? text_.size() : *caret;
	head_ = anchor_;
	revision_ = detail::nextIdentity();
	marked_.reset();
	return {};
}

auto TextBuffer::setText(std::string text) -> std::expected<void, Error> {
	if (!isValidUtf8(text) || text.find_first_of("\r\n") != std::string::npos) {
		return std::unexpected(Error::invalidValue);
	}
	if (text == text_) {
		return {};
	}
	restore({.text = std::move(text)});
	undo_.clear();
	redo_.clear();
	return {};
}

auto TextBuffer::setMarkedText(std::string text) -> std::expected<void, Error> {
	if (!isValidUtf8(text) || text.find_first_of("\r\n") != std::string::npos) {
		return std::unexpected(Error::invalidValue);
	}
	marked_ = std::move(text);
	return {};
}

auto TextBuffer::commitMarkedText() -> std::expected<void, Error> {
	if (!marked_) {
		return {};
	}
	return replaceSelection(*marked_);
}

auto TextBuffer::undo() -> bool {
	if (undo_.empty()) {
		return false;
	}
	redo_.push_back(currentState());
	auto previous = std::move(undo_.back());
	undo_.pop_back();
	restore(std::move(previous));
	return true;
}

auto TextBuffer::redo() -> bool {
	if (redo_.empty()) {
		return false;
	}
	undo_.push_back(currentState());
	auto next = std::move(redo_.back());
	redo_.pop_back();
	restore(std::move(next));
	return true;
}
} // namespace stana
