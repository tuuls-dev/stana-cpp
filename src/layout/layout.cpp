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
	const bool text = node->kind == NodeKind::textInput || node->kind == NodeKind::textLabel ||
		node->kind == NodeKind::button || node->kind == NodeKind::toggle;
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
	const Points offeredWidth = Points{stana::layout::detail::constrain(
		stana::layout::detail::resolveLength(
			node->frame.width,
			available.width.value(),
			available.width.value()
		),
		node->frame.minWidth,
		node->frame.maxWidth
	)};
	const Size contentAvailable{
		.width = Points{std::max(
			0.0,
			offeredWidth.value() - padding.leading.value() - padding.trailing.value()
		)},
		.height = Points{
			std::max(0.0, available.height.value() - padding.top.value() - padding.bottom.value())
		}
	};
	Size intrinsic;
	if (node->kind == NodeKind::textInput || node->kind == NodeKind::textLabel ||
		node->kind == NodeKind::button || node->kind == NodeKind::toggle) {
		const auto nodeId = node->id;
		Points width = contentAvailable.width;
		if (node->frame.width.kind == Length::Kind::points) {
			width = Points{std::max(
				0.0,
				node->frame.width.absolute.value() - padding.leading.value() -
					padding.trailing.value()
			)};
		}
		if (node->frame.maxWidth) {
			width = Points{std::min(
				width.value(),
				std::max(
					0.0,
					node->frame.maxWidth->value() - padding.leading.value() -
						padding.trailing.value()
				)
			)};
		}
		const auto layout = resolveTextLayout(*node, width);
		node = find(nodeId);
		if (node == nullptr) {
			throw std::runtime_error("Text occurrence retired during layout");
		}
		node->textLayout = layout;
		intrinsic = layout->size;
		if (node->editor) {
			if (node->editor->buffer.displayedText().empty() &&
				!node->editor->placeholder.empty()) {
				const auto placeholder = resolveTextLayout(*node, std::nullopt, true);
				node = find(nodeId);
				if (node == nullptr || !node->editor) {
					throw std::runtime_error("Editor retired during placeholder layout");
				}
				node->editor->placeholderLayout = placeholder;
			} else {
				node->editor->placeholderLayout.reset();
			}
			constexpr Points preferredInputWidth{160};
			intrinsic.width = preferredInputWidth;
		}
		if (measureText_ && !node->editor) {
			const auto inputText = node->text;
			auto font = node->font;
			font.size *= environment_.textScale;
			const auto provider = measureText_;
			const auto stamp = mutationStamp_;
			node = nullptr;
			intrinsic = provider(inputText, font, width);
			node = find(nodeId);
			if (mutationStamp_ != stamp || node == nullptr ||
				!stana::reactive::detail::ReactiveRuntime::current().pending.empty()) {
				throw std::runtime_error("Retained inputs changed during text measurement");
			}
		}
	} else if (node->kind == NodeKind::symbol) {
		const Points side = node->symbol ? node->symbol->size : Points{};
		intrinsic = {.width = side, .height = side};
	} else {
		if (node->kind == NodeKind::hstack) {
			const auto children = node->children;
			std::vector<NodeId> fills;
			double reserved = 0;
			std::size_t participants = 0;
			for (const auto child : children) {
				auto *record = find(child);
				if (record == nullptr || record->visibility == Visibility::collapsed) {
					continue;
				}
				++participants;
				if (record->frame.width.kind == Length::Kind::fill) {
					record->allocatedWidth = record->frame.minWidth.value_or(Points{});
					reserved += record->allocatedWidth->value();
					fills.push_back(child);
				} else {
					record->allocatedWidth.reset();
					reserved += measure(child, contentAvailable).width.value();
				}
			}
			node = find(identity);
			if (node == nullptr) {
				throw std::runtime_error("Row retired during allocation");
			}
			if (participants > 1) {
				reserved += static_cast<double>(participants - 1) * node->spacing.value();
			}
			double remainder = std::max(0.0, contentAvailable.width.value() - reserved);
			while (!fills.empty() && remainder > 0) {
				const double share = remainder / static_cast<double>(fills.size());
				double distributed = 0;
				std::vector<NodeId> growing;
				for (const auto child : fills) {
					auto *record = find(child);
					if (record == nullptr) {
						continue;
					}
					if (!record->allocatedWidth) {
						throw std::logic_error("Missing retained fill allocation");
					}
					const double current = record->allocatedWidth->value();
					const double next = record->frame.maxWidth
						? std::min(current + share, record->frame.maxWidth->value())
						: current + share;
					record->allocatedWidth = Points{next};
					distributed += next - current;
					if (!record->frame.maxWidth || next < record->frame.maxWidth->value()) {
						growing.push_back(child);
					}
				}
				if (distributed <= 0) {
					break;
				}
				remainder = std::max(0.0, remainder - distributed);
				if (growing.size() == fills.size()) {
					break;
				}
				fills = std::move(growing);
			}
		}
		bool firstChild = true;
		for (const auto child : node->children) {
			const auto *childNode = find(child);
			if (childNode == nullptr) {
				continue;
			}
			auto childOffer = contentAvailable;
			if (node->kind == NodeKind::hstack && childNode->allocatedWidth) {
				childOffer.width = *childNode->allocatedWidth;
			}
			const auto size = measure(child, childOffer);
			node = find(identity);
			childNode = find(child);
			if (node == nullptr || childNode == nullptr) {
				throw std::runtime_error("Child retired during measurement");
			}
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
	if (node->textLayout) {
		if (!node->editor && !measureText_ &&
			node->textLayout->request.width != std::optional{content.size.width}) {
			const auto layout = resolveTextLayout(*node, content.size.width);
			node = find(identity);
			if (node == nullptr) {
				throw std::runtime_error("Text retired during arrangement");
			}
			node->textLayout = layout;
		}
		if (node->editor) {
			const auto selection = node->editor->buffer.displayedSelection();
			const auto caret = node->textLayout->caret(selection.end);
			const double maximum = node->editor->buffer.displayedText().empty()
				? 0
				: std::max(
					  0.0,
					  node->textLayout->size.width.value() - content.size.width.value() +
						  caret.size.width.value()
				  );
			double scroll = node->editor->scroll.value();
			if (caret.origin.x.value() < scroll) {
				scroll = caret.origin.x.value();
			}
			if (caret.origin.x.value() + caret.size.width.value() >
				scroll + content.size.width.value()) {
				scroll =
					caret.origin.x.value() + caret.size.width.value() - content.size.width.value();
			}
			node->editor->scroll = Points{std::clamp(scroll, 0.0, maximum)};
		}
	}
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
		if (record->frame.width.kind == Length::Kind::fill) {
			childSize.width = Points{stana::layout::detail::constrain(
				record->allocatedWidth.value_or(content.size.width).value(),
				record->frame.minWidth,
				record->frame.maxWidth
			)};
		}
		if (record->frame.height.kind == Length::Kind::fill) {
			childSize.height = Points{stana::layout::detail::constrain(
				content.size.height.value(),
				record->frame.minHeight,
				record->frame.maxHeight
			)};
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
