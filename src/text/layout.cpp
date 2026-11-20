module;
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
module stana;
import :text;
import :primitives;

namespace stana::text {
namespace detail {
namespace {
[[nodiscard]] auto deterministicLine(const std::string &text, const Font &font, Direction direction)
	-> ShapedLine {
	constexpr double baseSize = 14;
	const double scale = font.size.value() / baseSize;
	ShapedLine result{
		.ascent = Points{15 * scale},
		.descent = Points{5 * scale},
		.xHeight = Points{7 * scale},
		.underlineThickness = Points{scale}
	};
	const auto graphemes = graphemeBoundaries(text);
	Points x{};
	for (std::size_t offset = 0; offset <= text.size(); ++offset) {
		if (!utf16Offset(text, offset)) {
			continue;
		}
		result.carets.push_back(
			{.offset = offset,
				.primary = x,
				.secondary = x,
				.isGrapheme = std::ranges::binary_search(graphemes, offset)}
		);
		if (offset != text.size()) {
			x += Points{8 * scale};
		}
	}
	result.width = x;
	const bool rtl = direction == Direction::rightToLeft;
	if (rtl) {
		for (auto &stop : result.carets) {
			stop.primary = x - stop.primary;
			stop.secondary = stop.primary;
		}
	}
	result.runs.push_back({.source = {0, text.size()}, .width = x, .rightToLeft = rtl});
	return result;
}

[[nodiscard]] auto hardBreakSize(const std::string &text, std::size_t offset) -> std::size_t {
	const auto tail = std::string_view{text}.substr(offset);
	if (tail.starts_with("\r\n")) {
		return 2;
	}
	if (tail.starts_with('\r') || tail.starts_with('\n')) {
		return 1;
	}
	if (tail.starts_with("\xC2\x85")) {
		return 2;
	}
	return tail.starts_with("\xE2\x80\xA8") || tail.starts_with("\xE2\x80\xA9") ? 3 : 0;
}

[[nodiscard]] auto stopX(const ShapedLine &line, std::size_t offset, bool secondary = false)
	-> Points {
	for (const auto &stop : line.carets) {
		if (stop.offset == offset) {
			return secondary ? stop.secondary : stop.primary;
		}
	}
	return line.width;
}
} // namespace
} // namespace detail

auto layoutText(const LayoutRequest &request, const LineShaper &shaper)
	-> std::shared_ptr<const TextLayout> {
	if (!isValidUtf8(request.text) || (request.font.family && !isValidUtf8(*request.font.family))) {
		throw std::invalid_argument("Invalid text layout UTF-8");
	}
	stana::detail::requireNonnegative(request.font.size);
	stana::detail::requireFinite(request.font.weight.value());
	if (request.width) {
		stana::detail::requireNonnegative(*request.width);
	}
	if (request.lineHeight) {
		stana::detail::requireNonnegative(*request.lineHeight);
		if (request.lineHeight->value() <= 0) {
			throw std::invalid_argument("Explicit line height must be positive");
		}
	}
	if (request.maxLines && *request.maxLines == 0) {
		throw std::invalid_argument("Maximum lines must be positive");
	}
	auto result = std::make_shared<TextLayout>();
	result->identity = stana::detail::nextIdentity();
	result->request = request;
	if (request.font.size.value() <= 0) {
		return result;
	}
	const auto shape = [&](const std::string &value) -> ShapedLine {
		return shaper ? shaper(value, request.font, request.direction)
					  : detail::deterministicLine(value, request.font, request.direction);
	};
	const auto graphemes = graphemeBoundaries(request.text);
	const auto opportunities = lineBoundaries(request.text);
	std::size_t start = 0;
	while (start <= request.text.size()) {
		std::size_t hardEnd = start;
		while (hardEnd < request.text.size() && detail::hardBreakSize(request.text, hardEnd) == 0) {
			++hardEnd;
		}
		const auto breakSize = detail::hardBreakSize(request.text, hardEnd);
		auto end = hardEnd;
		if (request.wrap != Wrap::none && request.width && request.width->value() > 0) {
			std::size_t fitting = start;
			std::size_t wordEnd = start;
			for (const auto boundary : graphemes) {
				if (boundary <= start || boundary > hardEnd) {
					continue;
				}
				if (shape(request.text.substr(start, boundary - start)).width > *request.width) {
					end = fitting == start ? boundary : fitting;
					if (request.wrap == Wrap::word && wordEnd > start) {
						end = wordEnd;
					}
					break;
				}
				fitting = boundary;
				if (std::ranges::binary_search(opportunities, boundary)) {
					wordEnd = boundary;
				}
			}
		}
		auto line = shape(request.text.substr(start, end - start));
		const Points natural = line.ascent + line.descent + line.leading;
		const Points height = request.lineHeight.value_or(natural);
		const Points baseline =
			result->size.height + Points{(height - natural).value() / 2} + line.ascent;
		const Points extent = request.width.value_or(line.width);
		Points x{};
		auto alignment = request.alignment;
		if (request.direction == Direction::rightToLeft && alignment != Alignment::center) {
			alignment = alignment == Alignment::leading ? Alignment::trailing : Alignment::leading;
		}
		if (alignment == Alignment::center) {
			x = Points{(extent - line.width).value() / 2};
		} else if (alignment == Alignment::trailing) {
			x = extent - line.width;
		}
		result->lines.push_back(
			{.source = {start, end == hardEnd ? end + breakSize : end},
				.inkEnd = end,
				.origin = {.x = x, .y = result->size.height},
				.baseline = baseline,
				.height = height,
				.shape = std::move(line)}
		);
		result->size.width =
			Points{std::max(result->size.width.value(), result->lines.back().shape.width.value())};
		result->size.height += height;
		if ((request.maxLines && result->lines.size() >= *request.maxLines) ||
			(end == request.text.size())) {
			break;
		}
		start = end == hardEnd ? end + breakSize : end;
	}
	if (request.width) {
		result->size.width = Points{std::min(result->size.width.value(), request.width->value())};
	}
	return result;
}

auto TextLayout::hit(Point point, bool graphemesOnly) const -> std::size_t {
	if (lines.empty()) {
		return 0;
	}
	const LayoutLine *line = &lines.back();
	for (const auto &candidate : lines) {
		if (point.y < candidate.origin.y + candidate.height) {
			line = &candidate;
			break;
		}
	}
	double distance = std::numeric_limits<double>::max();
	std::size_t result = line->source.start;
	for (const auto &stop : line->shape.carets) {
		if (graphemesOnly && !stop.isGrapheme) {
			continue;
		}
		for (const auto x : {stop.primary, stop.secondary}) {
			const double next = std::abs((point.x - line->origin.x - x).value());
			if (next < distance) {
				distance = next;
				result = line->source.start + stop.offset;
			}
		}
	}
	return result;
}

auto TextLayout::caret(std::size_t offset, bool secondary) const -> Rect {
	for (const auto &line : lines) {
		if (offset >= line.source.start && offset <= line.inkEnd) {
			return {
				.origin =
					{.x = line.origin.x +
							detail::stopX(line.shape, offset - line.source.start, secondary),
						.y = line.origin.y},
				.size = {.width = Points{1}, .height = line.height}
			};
		}
	}
	return {};
}

auto TextLayout::selection(TextRange range) const -> std::vector<Rect> {
	std::vector<Rect> result;
	for (const auto &line : lines) {
		for (const auto &run : line.shape.runs) {
			const auto start = std::max(range.start, line.source.start + run.source.start);
			const auto end = std::min(range.end, line.source.start + run.source.end);
			if (start >= end) {
				continue;
			}
			const auto first = detail::stopX(line.shape, start - line.source.start);
			const auto last = detail::stopX(line.shape, end - line.source.start);
			result.push_back(
				{.origin =
						{.x = line.origin.x + Points{std::min(first.value(), last.value())},
							.y = line.origin.y},
					.size =
						{.width = Points{std::abs((last - first).value())}, .height = line.height}}
			);
		}
	}
	return result;
}
} // namespace stana::text
