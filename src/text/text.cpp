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

module stana;
import :text;
import :primitives;

namespace stana::text {
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

auto scalarBoundaries(const std::string &text) -> std::vector<ScalarBoundary> {
	if (!isValidUtf8(text) ||
		text.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
		throw std::invalid_argument("Invalid UTF-8 scalar map");
	}
	std::vector<ScalarBoundary> result{{}};
	std::size_t bytes = 0;
	std::size_t units = 0;
	while (bytes < text.size()) {
		const auto leading = static_cast<unsigned char>(text.at(bytes));
		const std::size_t width = leading < asciiLimit
			? 1
			: (leading <= twoByteMaximum ? 2 : (leading <= threeByteMaximum ? 3 : 4));
		bytes += width;
		units += width == 4 ? 2 : 1;
		result.push_back({.utf8Offset = bytes, .utf16Offset = units});
	}
	return result;
}

auto graphemeBoundaries(const std::string &text) -> std::vector<std::size_t> {
	const auto unicode = unicodeText(text);
	const auto scalars = scalarBoundaries(text);
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
		const auto found = std::ranges::lower_bound(
			scalars,
			static_cast<std::size_t>(boundary),
			{},
			&ScalarBoundary::utf16Offset
		);
		if (found == scalars.end() || !std::cmp_equal(found->utf16Offset, boundary)) {
			throw std::runtime_error("ICU split a scalar boundary");
		}
		boundaries.push_back(found->utf8Offset);
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

namespace detail {
namespace {
[[nodiscard]] auto breaks(const std::string &text, bool words) -> std::vector<std::size_t> {
	const auto unicode = unicodeText(text);
	const auto scalars = scalarBoundaries(text);
	UErrorCode status = U_ZERO_ERROR;
	const std::unique_ptr<icu::BreakIterator> iterator{
		words ? icu::BreakIterator::createWordInstance(icu::Locale::getRoot(), status)
			  : icu::BreakIterator::createLineInstance(icu::Locale::getRoot(), status)
	};
	if (U_FAILURE(status) != 0 || !iterator) {
		throw std::runtime_error("Unable to create ICU text iterator");
	}
	iterator->setText(unicode);
	std::vector<std::size_t> result;
	for (auto boundary = iterator->first(); boundary != icu::BreakIterator::DONE;
		boundary = iterator->next()) {
		const auto found = std::ranges::lower_bound(
			scalars,
			static_cast<std::size_t>(boundary),
			{},
			&ScalarBoundary::utf16Offset
		);
		if (found == scalars.end() || !std::cmp_equal(found->utf16Offset, boundary)) {
			throw std::runtime_error("ICU split a scalar boundary");
		}
		result.push_back(found->utf8Offset);
	}
	return result;
}

[[nodiscard]] auto validRange(const std::string &text, TextRange range, bool graphemes) -> bool {
	if (range.start > range.end || range.end > text.size()) {
		return false;
	}
	if (!utf16Offset(text, range.start) || !utf16Offset(text, range.end)) {
		return false;
	}
	if (graphemes) {
		const auto boundaries = graphemeBoundaries(text);
		return std::ranges::binary_search(boundaries, range.start) &&
			std::ranges::binary_search(boundaries, range.end);
	}
	return true;
}
} // namespace
} // namespace detail

auto wordBoundaries(const std::string &text) -> std::vector<std::size_t> {
	return detail::breaks(text, true);
}

auto lineBoundaries(const std::string &text) -> std::vector<std::size_t> {
	return detail::breaks(text, false);
}

auto normalizePaste(std::string_view text) -> std::expected<std::string, Error> {
	if (!isValidUtf8(text)) {
		return std::unexpected(Error::invalidValue);
	}
	std::string result;
	for (std::size_t index = 0; index < text.size();) {
		const auto tail = text.substr(index);
		std::size_t length = 0;
		if (tail.starts_with("\r\n") || tail.starts_with("\xC2\x85")) {
			length = 2;
		} else if (tail.starts_with('\r') || tail.starts_with('\n')) {
			length = 1;

		} else if (tail.starts_with("\xE2\x80\xA8") || tail.starts_with("\xE2\x80\xA9")) {
			length = 3;
		}
		if (length != 0) {
			result.push_back(' ');
			index += length;
		} else {
			result.push_back(text.at(index));
			++index;
		}
	}
	return result;
}

auto isSingleLine(std::string_view text) noexcept -> bool {
	return isValidUtf8(text) && text.find_first_of("\r\n") == std::string_view::npos &&
		text.find("\xC2\x85") == std::string_view::npos &&
		text.find("\xE2\x80\xA8") == std::string_view::npos &&
		text.find("\xE2\x80\xA9") == std::string_view::npos &&
		text.size() <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
}

TextBuffer::TextBuffer(std::string text)
	: storeRevision_(stana::detail::nextIdentity()),
	  text_(std::move(text)),
	  revision_(stana::detail::nextIdentity()) {
	if (!isSingleLine(text_)) {
		throw std::invalid_argument("Malformed initial editor text");
	}
}

auto TextBuffer::selection() const noexcept -> TextSelection {
	return {
		.anchor = {.utf8Offset = anchor_, .revision = revision_},
		.head = {.utf8Offset = head_, .revision = revision_}
	};
}

auto TextBuffer::snapshot() const -> StoreSnapshot {
	return {
		.stamp = stamp(),
		.text = text_,
		.selection = selection(),
		.composition = composition_,
		.canUndo = !undo_.empty(),
		.canRedo = !redo_.empty()
	};
}

auto TextBuffer::displayedText() const -> std::string {
	auto result = text_;
	if (composition_) {
		result.replace(
			composition_->replacement.start,
			composition_->replacement.end - composition_->replacement.start,
			composition_->text
		);
	}
	return result;
}

auto TextBuffer::displayedSelection() const -> TextRange {
	if (composition_) {
		return {
			.start = composition_->replacement.start + composition_->selection.start,
			.end = composition_->replacement.start + composition_->selection.end
		};
	}
	return {.start = anchor_, .end = head_};
}

auto TextBuffer::select(TextSelection value) -> std::expected<void, Error> {
	if (value.anchor.revision != revision_ || value.head.revision != revision_) {
		return std::unexpected(Error::invalidTextRange);
	}
	const auto a = value.anchor.utf8Offset;
	const auto h = value.head.utf8Offset;
	if (!detail::validRange(text_, {.start = std::min(a, h), .end = std::max(a, h)}, true)) {
		return std::unexpected(Error::invalidTextRange);
	}
	if (anchor_ != a || head_ != h || composition_) {
		cancelMarkedText();
		anchor_ = a;
		head_ = h;
		storeRevision_ = stana::detail::nextIdentity();
	}
	return {};
}

auto TextBuffer::currentState() const -> EditState {
	return {.text = text_, .anchor = anchor_, .head = head_};
}

void TextBuffer::restore(EditState state) {
	const bool changed = text_ != state.text;
	text_ = std::move(state.text);
	anchor_ = state.anchor;
	head_ = state.head;
	if (changed) {
		revision_ = stana::detail::nextIdentity();
	}
	storeRevision_ = stana::detail::nextIdentity();
	marked_.reset();
	composition_.reset();
}

auto TextBuffer::replaceSelection(const std::string &insertion, bool groupWithPrevious)
	-> std::expected<void, Error> {
	if (!isSingleLine(insertion)) {
		return std::unexpected(Error::invalidValue);
	}
	const auto begin = std::min(anchor_, head_);
	const auto end = std::max(anchor_, head_);
	auto replacement = text_;
	replacement.replace(begin, end - begin, insertion);
	if (!isSingleLine(replacement)) {
		return std::unexpected(Error::invalidValue);
	}
	const auto boundaries = graphemeBoundaries(replacement);
	const auto caret = std::ranges::lower_bound(boundaries, begin + insertion.size());
	const auto position = caret == boundaries.end() ? replacement.size() : *caret;
	if (replacement == text_) {
		if (anchor_ != position || head_ != position || composition_) {
			restore({.text = text_, .anchor = position, .head = position});
		}
		return {};
	}
	if (!groupWithPrevious || undo_.empty()) {
		undo_.push_back(currentState());
	}
	redo_.clear();
	restore({.text = std::move(replacement), .anchor = position, .head = position});
	return {};
}

auto TextBuffer::setText(std::string value) -> std::expected<void, Error> {
	if (!isSingleLine(value)) {
		return std::unexpected(Error::invalidValue);
	}
	if (value != text_) {
		restore({.text = std::move(value)});
		undo_.clear();
		redo_.clear();
	}
	return {};
}

auto TextBuffer::setMarkedText(std::string value) -> std::expected<void, Error> {
	const auto end = value.size();
	return apply(
		{.kind = StoreEdit::Kind::compose,
			.stamp = stamp(),
			.text = std::move(value),
			.selection = {.start = end, .end = end}}
	);
}

void TextBuffer::cancelMarkedText() {
	if (composition_) {
		anchor_ = composition_->originalSelection.anchor.utf8Offset;
		head_ = composition_->originalSelection.head.utf8Offset;
		composition_.reset();
		marked_.reset();
		storeRevision_ = stana::detail::nextIdentity();
	}
}

auto TextBuffer::commitMarkedText() -> std::expected<void, Error> {
	if (!composition_) {
		return {};
	}
	const auto saved = *composition_;
	const auto original = currentState();
	anchor_ = saved.replacement.start;
	head_ = saved.replacement.end;
	const auto result = replaceSelection(saved.text);
	if (result && text_ != original.text && !undo_.empty()) {
		undo_.back() = original;
	}
	return result;
}

auto TextBuffer::apply(const StoreEdit &edit) -> std::expected<void, Error> {
	if (edit.stamp != stamp()) {
		return std::unexpected(Error::invalidTextRange);
	}
	using enum StoreEdit::Kind;
	if (edit.kind == cancel) {
		cancelMarkedText();
		return {};
	}
	if (edit.kind == commit) {
		return commitMarkedText();
	}
	if ((edit.kind == replace || edit.kind == compose) && !isSingleLine(edit.text)) {
		return std::unexpected(Error::invalidValue);
	}
	const auto display = displayedText();
	auto range = edit.replacement.value_or(composition_
		? TextRange{.start=composition_->replacement.start,
			.end=composition_->replacement.start + composition_->text.size()}
		: TextRange{.start=std::min(anchor_, head_), .end=std::max(anchor_, head_)});
	if (edit.kind == select) {
		range = {
			.start = std::min(edit.selection.start, edit.selection.end),
			.end = std::max(edit.selection.start, edit.selection.end)
		};
	}
	const auto &source = edit.space == TextSpace::displayed ? display : text_;
	if (!detail::validRange(source, range, false)) {
		return std::unexpected(Error::invalidTextRange);
	}
	if (edit.kind == compose && !detail::validRange(edit.text, edit.selection, false)) {
		return std::unexpected(Error::invalidTextRange);
	}
	if (edit.kind == select && composition_ && edit.space == TextSpace::displayed) {
		const auto start = composition_->replacement.start;
		const auto end = start + composition_->text.size();
		if (range.start >= start && range.end <= end) {
			const TextRange relative{
				.start = edit.selection.start - start,
				.end = edit.selection.end - start
			};
			if (relative != composition_->selection) {
				composition_->selection = relative;
				storeRevision_ = stana::detail::nextIdentity();
			}
			return {};
		}
		if (range.start < end && range.end > start) {
			return std::unexpected(Error::invalidTextRange);
		}
	}
	auto value = edit.text;
	auto relativeSelection = edit.selection;
	if (composition_ && edit.space == TextSpace::displayed) {
		const auto &old = *composition_;
		const auto start = old.replacement.start;
		const auto end = start + old.text.size();
		if (range.start <= end && range.end >= start && edit.kind != select) {
			const auto prefix = range.start > start ? old.text.substr(0, range.start - start) : "";
			const auto suffix = range.end < end ? old.text.substr(range.end - start) : "";
			value = prefix + value + suffix;
			relativeSelection.start += prefix.size();
			relativeSelection.end += prefix.size();
			range = {
				.start = std::min(range.start, start),
				.end =
					range.end <= end ? old.replacement.end : old.replacement.end + range.end - end
			};
		} else if (range.start >= end) {
			range = {
				.start = old.replacement.end + range.start - end,
				.end = old.replacement.end + range.end - end
			};
		}
	}
	if (!detail::validRange(text_, range, edit.kind == select)) {
		return std::unexpected(Error::invalidTextRange);
	}
	if (edit.kind == select) {
		const bool reversed = edit.selection.start > edit.selection.end;
		return this->select(
			{.anchor = {.utf8Offset = reversed ? range.end : range.start, .revision = revision_},
				.head = {.utf8Offset = reversed ? range.start : range.end, .revision = revision_}}
		);
	}
	if (edit.kind == compose) {
		auto candidate = text_;
		candidate.replace(range.start, range.end - range.start, value);
		if (!isSingleLine(candidate)) {
			return std::unexpected(Error::invalidValue);
		}
		const Composition next{
			.replacement = range,
			.originalSelection = composition_ ? composition_->originalSelection : selection(),
			.text = value,
			.selection = relativeSelection
		};
		if (composition_ != next) {
			composition_ = next;
			marked_ = value;
			storeRevision_ = stana::detail::nextIdentity();
		}
		return {};
	}
	const auto original = currentState();
	cancelMarkedText();
	anchor_ = range.start;
	head_ = range.end;
	const auto result = replaceSelection(value, edit.groupWithPrevious);
	if (result && text_ != original.text && !edit.groupWithPrevious && !undo_.empty()) {
		undo_.back() = original;
	}
	return result;
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
} // namespace stana::text
