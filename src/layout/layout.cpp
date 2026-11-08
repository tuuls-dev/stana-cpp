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

module stana;
import :document;
import :primitives;
import :reactive;
import :rendering;
import :symbol_geometry;

namespace stana::layout::detail {
namespace {
constexpr double defaultGlyphAdvance = 0.56;
constexpr double lineHeightMultiplier = 1.4;

[[nodiscard]] auto resolveLength(Length length, double intrinsic, double available) -> double {
	switch (length.kind) {
	case Length::Kind::automatic:
		return intrinsic;
	case Length::Kind::points:
		stana::detail::requireNonnegative(length.absolute);
		return length.absolute.value();
	case Length::Kind::fill:
		return available;
	case Length::Kind::percent:
		stana::detail::requireNonnegative(length.relative);
		constexpr double percentDenominator = 100;
		return available * length.relative.value() / percentDenominator;
	}
	throw std::logic_error("Unknown Stana length");
}

[[nodiscard]] auto
constrain(double value, std::optional<Points> minimum, std::optional<Points> maximum) -> double {
	if (minimum) {
		stana::detail::requireNonnegative(*minimum);
		value = std::max(value, minimum->value());
	}
	if (maximum) {
		stana::detail::requireNonnegative(*maximum);
		value = std::min(value, maximum->value());
	}
	return std::max(0.0, value);
}
} // namespace

} // namespace stana::layout::detail

namespace stana::document::detail {
auto Document::measure(NodeId identity, Size available) -> Size {
	auto *node = find(identity);
	if (node == nullptr) {
		return {};
	}
	const bool text = node->kind == NodeKind::text || node->kind == NodeKind::button ||
		node->kind == NodeKind::toggle;
	const stana::layout::detail::MeasureKey key{
		.available = available,
		.localInputs = node->work.measureInputs,
		.descendantInputs = node->work.descendantInputs,
		.textEnvironment = text ? textEnvironment_ : 0
	};
	if (node->work.measuredAt && std::is_eq(*node->work.measuredAt <=> key) &&
		!has(node->work.local, Dirty::measure) && node->work.measureChildren.empty()) {
		return node->measured;
	}
	++metrics_.measurements;
	const auto previous = node->measured;
	if (node->visibility == Visibility::collapsed) {
		node->measured = {};
		node->work.measuredAt = key;
		node->work.local = without(node->work.local, Dirty::measure);
		node->work.measureChildren.clear();
		if (previous != node->measured) {
			invalidate(identity, Dirty::arrange | Dirty::snapshot | Dirty::paint);
			if (auto *parent = find(node->parent)) {
				parent->work.childMeasurements = stana::detail::nextIdentity();
				invalidate(parent->id, Dirty::arrange);
			}
		}
		return {};
	}
	const auto padding = node->padding;
	const Size contentAvailable{
		.width = Points{std::max(
			0.0,
			available.width.value() - padding.leading.value() - padding.trailing.value()
		)},
		.height = Points{
			std::max(0.0, available.height.value() - padding.top.value() - padding.bottom.value())
		}
	};
	Size intrinsic;
	if (node->kind == NodeKind::text || node->kind == NodeKind::button ||
		node->kind == NodeKind::toggle) {
		auto font = node->font;
		font.size *= environment_.textScale;
		stana::detail::requireNonnegative(font.size);
		if (measureText_) {
			const auto inputText = node->text;
			const auto provider = measureText_;
			const auto stamp = mutationStamp_;
			node = nullptr; // Never keep borrowed retained storage across a service callback.
			intrinsic = provider(inputText, font, contentAvailable.width);
			node = find(identity);
			if (mutationStamp_ != stamp || node == nullptr ||
				!stana::reactive::detail::ReactiveRuntime::current().pending.empty()) {
				throw std::runtime_error("Retained inputs changed during text measurement");
			}
			stana::detail::requireNonnegative(intrinsic.width);
			stana::detail::requireNonnegative(intrinsic.height);
		} else {
			intrinsic = {
				.width = Points{std::min(
					contentAvailable.width.value(),
					static_cast<double>(node->text.size()) * font.size.value() *
						stana::layout::detail::defaultGlyphAdvance
				)},
				.height = Points{font.size.value() * stana::layout::detail::lineHeightMultiplier}
			};
		}
	} else if (node->kind == NodeKind::symbol) {
		const Points side = node->symbol ? node->symbol->size : Points{};
		intrinsic = {.width = side, .height = side};
	} else {
		bool firstChild = true;
		for (const auto child : node->children) {
			const auto *childNode = find(child);
			if (childNode == nullptr) {
				continue;
			}
			const auto size = measure(child, contentAvailable);
			if (childNode->visibility == Visibility::collapsed) {
				continue;
			}
			const Points spacing = firstChild ? Points{} : node->spacing;
			firstChild = false;
			if (node->kind == NodeKind::vstack || node->kind == NodeKind::group ||
				node->kind == NodeKind::scroll) {
				intrinsic.height += size.height + spacing;
				intrinsic.width = Points{std::max(intrinsic.width.value(), size.width.value())};
			} else if (node->kind == NodeKind::hstack) {
				intrinsic.width += size.width + spacing;
				intrinsic.height = Points{std::max(intrinsic.height.value(), size.height.value())};
			} else {
				intrinsic.width = Points{std::max(intrinsic.width.value(), size.width.value())};
				intrinsic.height = Points{std::max(intrinsic.height.value(), size.height.value())};
			}
		}
	}
	intrinsic.width += padding.leading + padding.trailing;
	intrinsic.height += padding.top + padding.bottom;
	node->measured = {
		.width = Points{stana::layout::detail::constrain(
			stana::layout::detail::resolveLength(
				node->frame.width,
				intrinsic.width.value(),
				available.width.value()
			),
			node->frame.minWidth,
			node->frame.maxWidth
		)},
		.height = Points{stana::layout::detail::constrain(
			stana::layout::detail::resolveLength(
				node->frame.height,
				intrinsic.height.value(),
				available.height.value()
			),
			node->frame.minHeight,
			node->frame.maxHeight
		)}
	};
	stana::detail::requireNonnegative(node->measured.width);
	stana::detail::requireNonnegative(node->measured.height);
	if (node->kind == NodeKind::symbol && node->symbol) {
		const Rect local{.size = node->measured};
		const auto content =
			stana::layout::detail::contentRect(local, padding, environment_.direction);
		const auto fitted = stana::symbol::square(node->symbol->size, content);
		node->symbol->baseline = fitted.origin.y + fitted.size.height;
	}
	node->work.measuredAt = key;
	node->work.measureChildren.clear();
	node->work.local = without(node->work.local, Dirty::measure);
	if (previous != node->measured) {
		// A fixed aggregate can stop size propagation while its internal placement stays dirty.
		invalidate(identity, Dirty::arrange | Dirty::snapshot | Dirty::paint);
		if (auto *parent = find(node->parent)) {
			parent->work.childMeasurements = stana::detail::nextIdentity();
			invalidate(parent->id, Dirty::arrange);
		}
	}
	return node->measured;
}

void Document::arrange(NodeId identity, Rect bounds) {
	stana::detail::requireFinite(bounds.origin.x);
	stana::detail::requireFinite(bounds.origin.y);
	stana::detail::requireNonnegative(bounds.size.width);
	stana::detail::requireNonnegative(bounds.size.height);
	auto *node = find(identity);
	if (node == nullptr) {
		return;
	}
	const stana::layout::detail::ArrangeKey key{
		.assigned = bounds,
		.placementInputs = node->work.placementInputs,
		.childMeasurements = node->work.childMeasurements,
		.direction = environment_.direction
	};
	if (node->work.arrangedAt && std::is_eq(*node->work.arrangedAt <=> key) &&
		!has(node->work.local, Dirty::arrange)) {
		const auto routes = node->work.arrangeChildren;
		for (const auto child : routes) {
			if (const auto *record = find(child)) {
				arrange(child, record->bounds);
			}
		}
		node->work.arrangeChildren.clear();
		return;
	}
	++metrics_.arrangements;
	if (node->bounds != bounds) {
		invalidate(identity, Dirty::paint | Dirty::snapshot);
	}
	node->bounds = bounds;
	const Points left = environment_.direction == Direction::leftToRight ? node->padding.leading
																		 : node->padding.trailing;
	const Rect content{
		.origin = {.x = bounds.origin.x + left, .y = bounds.origin.y + node->padding.top},
		.size = {
			.width = Points{std::max(
				0.0,
				bounds.size.width.value() - node->padding.leading.value() -
					node->padding.trailing.value()
			)},
			.height = Points{std::max(
				0.0,
				bounds.size.height.value() - node->padding.top.value() -
					node->padding.bottom.value()
			)}
		}
	};
	const bool horizontal = node->kind == NodeKind::hstack;
	const bool vertical = node->kind == NodeKind::vstack || node->kind == NodeKind::group ||
		node->kind == NodeKind::scroll;
	Points offset{};
	if (node->kind == NodeKind::scroll) {
		Points extent{};
		for (const auto child : node->children) {
			if (const auto *record = find(child)) {
				extent += record->measured.height;
			}
		}
		node->scrollOffset = Points{std::clamp(
			node->scrollOffset.value(),
			0.0,
			std::max(0.0, extent.value() - content.size.height.value())
		)};
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
			origin.y += Points{(content.size.height.value() - childSize.height.value()) / 2};
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
				origin.x += Points{(content.size.width.value() - childSize.width.value()) / 2};
			} else if (alignment == Alignment::trailing) {
				origin.x += content.size.width - childSize.width;
			}
		}
		const Rect proposed{.origin = origin, .size = childSize};
		if (record->bounds != proposed || has(record->work.local, Dirty::arrange) ||
			!record->work.arrangeChildren.empty() || !record->work.arrangedAt) {
			arrange(child, proposed);
		}
	}
	node->work.arrangedAt = key;
	node->work.arrangeChildren.clear();
	node->work.local = without(node->work.local, Dirty::arrange);
}
} // namespace stana::document::detail
