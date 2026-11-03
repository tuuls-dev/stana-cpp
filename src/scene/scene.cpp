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
constexpr double disabledOpacity = 0.45;
constexpr double focusBorderWidth = 2;

[[nodiscard]] auto nodeKindName(detail::NodeKind kind) -> std::string {
	using enum detail::NodeKind;
	switch (kind) {
	case root:
		return "root";
	case group:
		return "group";
	case box:
		return "box";
	case vstack:
		return "vstack";
	case hstack:
		return "hstack";
	case zstack:
		return "zstack";
	case spacer:
		return "spacer";
	case text:
		return "text";
	case button:
		return "button";
	case toggle:
		return "toggle";
	case scroll:
		return "scroll";
	case canvas:
		return "canvas";
	}
	throw std::logic_error("Unknown Stana node kind");
}

[[nodiscard]] auto withOpacity(Color color, double opacity) -> Color {
	color.alpha *= opacity;
	return color;
}

/// @brief Label color for text that has no author-supplied foreground.
[[nodiscard]] auto themeLabel(Theme theme) -> Color {
	return theme == Theme::dark ? Color::white() : Color::black();
}
} // namespace

namespace detail {
void Document::paint(NodeId identity, SceneSnapshot &scene, double opacity) {
	const auto *node = find(identity);
	if (node == nullptr || node->visibility != Visibility::visible) {
		return;
	}
	opacity *= node->opacity;
	if (!node->isEnabled) {
		opacity *= disabledOpacity;
	}
	if (node->background.alpha > 0) {
		scene.commands.push_back(
			{.kind = PaintKind::fillRect,
				.bounds = node->bounds,
				.color = withOpacity(node->background, opacity),
				.cornerRadius = node->cornerRadius}
		);
	}
	if (node->clip || node->kind == NodeKind::scroll) {
		scene.commands.push_back(
			{.kind = PaintKind::pushClip,
				.bounds = node->bounds,
				.cornerRadius = node->cornerRadius}
		);
	}
	if (node->kind == NodeKind::text || node->kind == NodeKind::button ||
		node->kind == NodeKind::toggle) {
		auto font = node->font;
		font.size *= environment_.textScale;
		auto bounds = node->bounds;
		bounds.origin.x += node->padding.leading;
		bounds.origin.y += node->padding.top;
		bounds.size.width =
			std::max(0.0, bounds.size.width - node->padding.leading - node->padding.trailing);
		bounds.size.height =
			std::max(0.0, bounds.size.height - node->padding.top - node->padding.bottom);
		const Color foreground =
			node->isForegroundExplicit ? node->foreground : themeLabel(environment_.theme);
		scene.commands.push_back(
			{.kind = PaintKind::text,
				.bounds = bounds,
				.color = withOpacity(foreground, opacity),
				.text = node->text,
				.font = font}
		);
	}
	for (auto command : node->drawing.commands) {
		command.bounds.origin.x += node->bounds.origin.x;
		command.bounds.origin.y += node->bounds.origin.y;
		command.color = withOpacity(command.color, opacity);
		scene.commands.push_back(std::move(command));
	}
	for (const auto child : node->children) {
		paint(child, scene, opacity);
	}
	if (node->clip || node->kind == NodeKind::scroll) {
		scene.commands.push_back({.kind = PaintKind::popClip});
	}
	if (node->borderWidth > 0 || focused_ == identity) {
		scene.commands.push_back(
			{.kind = PaintKind::strokeRect,
				.bounds = node->bounds,
				.color = withOpacity(
					focused_ == identity ? Color::accent() : node->borderColor,
					opacity
				),
				.cornerRadius = node->cornerRadius,
				.lineWidth = focused_ == identity ? focusBorderWidth : node->borderWidth}
		);
	}
}

void Document::snapshot(NodeId identity) {
	const auto *node = find(identity);
	if (node == nullptr || node->visibility != Visibility::visible) {
		return;
	}
	snapshots_.push_back(
		{.id = node->id,
			.kind = nodeKindName(node->kind),
			.text = node->text,
			.bounds = node->bounds,
			.isEnabled = isEligible(identity),
			.isFocused = focused_ == identity,
			.accessibilityLabel = node->accessibilityLabel}
	);
	for (const auto child : node->children) {
		snapshot(child);
	}
}

void Document::commit() {
	if (isClosed_ || !isDirty_ || ReactiveRuntime::current().depth != 0) {
		return;
	}
	isDirty_ = false;
	++metrics_.layoutPasses;
	static_cast<void>(measure(root_, viewport_));
	arrange(root_, {.size = viewport_});
	snapshots_.clear();
	snapshot(root_);
	auto next = std::make_shared<SceneSnapshot>();
	next->revision = ++metrics_.sceneRevisions;
	next->size = viewport_;
	next->background = environment_.theme == Theme::dark ? Color::black() : Color::white();
	paint(root_, *next, 1);
	scene_ = std::move(next);
	rememberGeometry();
	if (onPublish_) {
		onPublish_();
	}
}
} // namespace detail
} // namespace stana
