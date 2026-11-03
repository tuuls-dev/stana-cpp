module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module stana.core;
import stana.primitives;
import stana.reactive;
import stana.graphics;

namespace stana {
namespace {
constexpr double defaultGlyphAdvance = 0.56;
constexpr double lineHeightMultiplier = 1.4;

[[nodiscard]] auto resolveLength(Length length, double intrinsic, double available) -> double {
	detail::requireNonnegative(length.value);
	switch (length.kind) {
	case Length::Kind::automatic:
		return intrinsic;
	case Length::Kind::points:
		return length.value;
	case Length::Kind::fill:
		return available;
	case Length::Kind::percent:
		constexpr double percentDenominator = 100;
		return available * length.value / percentDenominator;
	}
	throw std::logic_error("Unknown Stana length");
}

[[nodiscard]] auto
constrain(double value, std::optional<double> minimum, std::optional<double> maximum) -> double {
	if (minimum) {
		detail::requireNonnegative(*minimum);
		value = std::max(value, *minimum);
	}
	if (maximum) {
		detail::requireNonnegative(*maximum);
		value = std::min(value, *maximum);
	}
	return std::max(0.0, value);
}
} // namespace

namespace detail {
auto Document::measure(NodeId identity, Size available) -> Size {
	auto *node = find(identity);
	if (node == nullptr || node->visibility == Visibility::collapsed) {
		return {};
	}
	const auto padding = node->padding;
	const Size contentAvailable{
		.width = std::max(0.0, available.width - padding.leading - padding.trailing),
		.height = std::max(0.0, available.height - padding.top - padding.bottom)
	};
	Size intrinsic;
	if (node->kind == NodeKind::text || node->kind == NodeKind::button ||
		node->kind == NodeKind::toggle) {
		auto font = node->font;
		font.size *= environment_.textScale;
		if (measureText_) {
			intrinsic = measureText_(node->text, font, contentAvailable.width);
		} else {
			intrinsic = {
				.width = std::min(
					contentAvailable.width,
					static_cast<double>(node->text.size()) * font.size * defaultGlyphAdvance
				),
				.height = font.size * lineHeightMultiplier
			};
		}
	} else {
		bool firstChild = true;
		for (const auto child : node->children) {
			const auto *childNode = find(child);
			if (childNode == nullptr || childNode->visibility == Visibility::collapsed) {
				continue;
			}
			const auto size = measure(child, contentAvailable);
			const double spacing = firstChild ? 0 : node->spacing;
			firstChild = false;
			if (node->kind == NodeKind::vstack || node->kind == NodeKind::group ||
				node->kind == NodeKind::scroll) {
				intrinsic.height += size.height + spacing;
				intrinsic.width = std::max(intrinsic.width, size.width);
			} else if (node->kind == NodeKind::hstack) {
				intrinsic.width += size.width + spacing;
				intrinsic.height = std::max(intrinsic.height, size.height);
			} else {
				intrinsic.width = std::max(intrinsic.width, size.width);
				intrinsic.height = std::max(intrinsic.height, size.height);
			}
		}
	}
	intrinsic.width += padding.leading + padding.trailing;
	intrinsic.height += padding.top + padding.bottom;
	node->measured = {
		.width = constrain(
			resolveLength(node->frame.width, intrinsic.width, available.width),
			node->frame.minWidth,
			node->frame.maxWidth
		),
		.height = constrain(
			resolveLength(node->frame.height, intrinsic.height, available.height),
			node->frame.minHeight,
			node->frame.maxHeight
		)
	};
	return node->measured;
}

void Document::arrange(NodeId identity, Rect bounds) {
	auto *node = find(identity);
	if (node == nullptr) {
		return;
	}
	node->bounds = bounds;
	const double left = environment_.direction == Direction::leftToRight ? node->padding.leading
																		 : node->padding.trailing;
	const Rect content{
		.origin = {.x = bounds.origin.x + left, .y = bounds.origin.y + node->padding.top},
		.size = {
			.width =
				std::max(0.0, bounds.size.width - node->padding.leading - node->padding.trailing),
			.height = std::max(0.0, bounds.size.height - node->padding.top - node->padding.bottom)
		}
	};
	const bool horizontal = node->kind == NodeKind::hstack;
	const bool vertical = node->kind == NodeKind::vstack || node->kind == NodeKind::group ||
		node->kind == NodeKind::scroll;
	double offset = 0;
	if (node->kind == NodeKind::scroll) {
		double extent = 0;
		for (const auto child : node->children) {
			if (const auto *record = find(child)) {
				extent += record->measured.height;
			}
		}
		node->scrollOffset =
			std::clamp(node->scrollOffset, 0.0, std::max(0.0, extent - content.size.height));
		offset = -node->scrollOffset;
	}
	for (const auto child : node->children) {
		auto *record = find(child);
		if (record == nullptr) {
			continue;
		}
		if (record->visibility == Visibility::collapsed) {
			arrange(child, {});
			continue;
		}
		auto childSize = record->measured;
		if (node->kind == NodeKind::root || record->frame.width.kind == Length::Kind::fill) {
			childSize.width = content.size.width;
		}
		if (node->kind == NodeKind::root || record->frame.height.kind == Length::Kind::fill) {
			childSize.height = content.size.height;
		}
		Point origin = content.origin;
		if (horizontal) {
			origin.x += environment_.direction == Direction::leftToRight
				? offset
				: content.size.width - offset - childSize.width;
			offset += childSize.width + node->spacing;
		} else if (vertical) {
			origin.y += offset;
			offset += childSize.height + node->spacing;
		} else if (node->kind != NodeKind::root) {
			origin.y += (content.size.height - childSize.height) / 2;
		}
		if (!horizontal && node->kind != NodeKind::root) {
			auto alignment = node->alignment;
			if (environment_.direction == Direction::rightToLeft) {
				if (alignment == Alignment::leading) {
					alignment = Alignment::trailing;
				} else if (alignment == Alignment::trailing) {
					alignment = Alignment::leading;
				}
			}
			if (alignment == Alignment::center) {
				origin.x += (content.size.width - childSize.width) / 2;
			} else if (alignment == Alignment::trailing) {
				origin.x += content.size.width - childSize.width;
			}
		}
		arrange(child, {.origin = origin, .size = childSize});
	}
}
} // namespace detail
} // namespace stana
