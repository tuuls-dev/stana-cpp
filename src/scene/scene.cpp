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
#include <set>
#include <span>
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
namespace {
[[nodiscard]] auto intersect(Rect left, Rect right) -> Rect {
	const double x = std::max(left.origin.x, right.origin.x);
	const double y = std::max(left.origin.y, right.origin.y);
	return {
		.origin = {.x = x, .y = y},
		.size = {
			.width = std::max(
				0.0,
				std::min(left.origin.x + left.size.width, right.origin.x + right.size.width) - x
			),
			.height = std::max(
				0.0,
				std::min(left.origin.y + left.size.height, right.origin.y + right.size.height) - y
			)
		}
	};
}

/// @brief Reject invalid local streams before a reusable branch can leak graphics state.
void validateDrawing(const Drawing &drawing) {
	std::size_t depth = 0;
	for (const auto &command : drawing.commands) {
		if (command.kind == PaintKind::popClip) {
			if (depth == 0) {
				throw std::invalid_argument("Canvas clip underflow");
			}
			--depth;
			continue;
		}
		requireFinite(command.bounds.origin.x);
		requireFinite(command.bounds.origin.y);
		requireFinite(command.bounds.origin.x + command.bounds.size.width);
		requireFinite(command.bounds.origin.y + command.bounds.size.height);
		if (command.kind == PaintKind::line) {
			requireFinite(command.bounds.size.width);
			requireFinite(command.bounds.size.height);
		} else {
			requireNonnegative(command.bounds.size.width);
			requireNonnegative(command.bounds.size.height);
		}
		requireNonnegative(command.cornerRadius);
		requireNonnegative(command.lineWidth);
		requireNonnegative(command.font.size);
		requireFinite(command.font.weight);
		for (const auto component :
			{command.color.red, command.color.green, command.color.blue, command.color.alpha}) {
			requireNonnegative(component);
			if (component > 1) {
				throw std::invalid_argument("Color exceeds one");
			}
		}
		if (command.kind == PaintKind::pushClip) {
			++depth;
		}
	}
	if (depth != 0) {
		throw std::invalid_argument("Unbalanced Canvas clips");
	}
}

void flattenPaint(
	const std::shared_ptr<const SceneBranch> &branch,
	std::vector<PaintCommand> &commands
) {
	if (!branch || !branch->context.visible) {
		return;
	}
	commands.insert(commands.end(), branch->paint->prefix.begin(), branch->paint->prefix.end());
	for (const auto &child : branch->children) {
		flattenPaint(child, commands);
	}
	commands.insert(commands.end(), branch->paint->suffix.begin(), branch->paint->suffix.end());
}

void flattenNodes(
	const std::shared_ptr<const SceneBranch> &branch,
	std::vector<NodeSnapshot> &nodes
) {
	if (!branch || !branch->context.visible) {
		return;
	}
	nodes.push_back(*branch->snapshot);
	for (const auto &child : branch->children) {
		flattenNodes(child, nodes);
	}
}

void eraseGeometry(const std::shared_ptr<const SceneBranch> &branch, GeometryRoot &geometry) {
	if (!branch) {
		return;
	}
	geometry = geometryErase(geometry, branch->id);
	for (const auto &child : branch->children) {
		eraseGeometry(child, geometry);
	}
}
} // namespace

auto Document::project(
	NodeId identity,
	ProjectionContext incoming,
	GeometryRoot &geometry,
	std::vector<std::pair<NodeId, std::shared_ptr<const SceneBranch>>> &promotions
) -> std::shared_ptr<const SceneBranch> {
	const auto *node = find(identity);
	if (node == nullptr) {
		return {};
	}
	auto previous = node->work.projection;
	auto context = incoming;
	context.opacity *= node->opacity * (node->isEnabled ? 1 : disabledOpacity);
	context.visible = incoming.visible && node->visibility == Visibility::visible;
	context.eligible = incoming.eligible && context.visible && node->isEnabled && !node->inert;
	if (previous && node->work.local == Dirty::none && node->work.projectionChildren.empty() &&
		std::is_eq(previous->context <=> context)) {
		return previous;
	}
	auto branch = std::make_shared<SceneBranch>();
	branch->routeGeneration = previous && previous->context.parentRoute == context.parentRoute &&
			previous->context.siblingOrder == context.siblingOrder
		? previous->routeGeneration
		: nextIdentity();
	branch->id = identity;
	branch->generation = nextIdentity();
	branch->context = context;
	++metrics_.projectionBuilds;
	const bool needsPaint = !previous || has(node->work.local, Dirty::paint) ||
		std::is_neq(previous->context.opacity <=> context.opacity);
	if (!needsPaint) {
		branch->paint = previous->paint;
	} else {
		validateDrawing(node->drawing);
		++metrics_.paintBuilds;
		auto paint = std::make_shared<PaintSegments>();
		paint->identity = nextIdentity();
		SceneSnapshot scene;
		const double opacity = context.opacity;

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
			requireNonnegative(font.size);
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
			requireFinite(command.bounds.origin.x);
			requireFinite(command.bounds.origin.y);
			command.color = withOpacity(command.color, opacity);
			scene.commands.push_back(std::move(command));
		}
		paint->prefix = std::move(scene.commands);
		if (node->clip || node->kind == NodeKind::scroll) {
			paint->childClipBounds = node->bounds;
		}

		if (node->clip || node->kind == NodeKind::scroll) {
			paint->suffix.push_back({.kind = PaintKind::popClip});
		}
		if (node->borderWidth > 0 || focused_ == identity) {
			paint->suffix.push_back(
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
		branch->paint = std::move(paint);
	}
	if (previous && !has(node->work.local, Dirty::snapshot) &&
		previous->context.visible == context.visible &&
		previous->context.eligible == context.eligible) {
		branch->snapshot = previous->snapshot;
	} else {
		branch->snapshot = std::make_shared<NodeSnapshot>(NodeSnapshot{
			.id = node->id,
			.kind = nodeKindName(node->kind),
			.text = node->text,
			.bounds = node->bounds,
			.isEnabled = context.eligible,
			.isFocused = focused_ == identity,
			.accessibilityLabel = node->accessibilityLabel
		});
	}
	if (context.visible) {
		const auto previousGeometry = geometryFind(geometry, identity);
		const auto orderedChildren = previousGeometry && previousGeometry->children &&
				*previousGeometry->children == node->children
			? previousGeometry->children
			: std::make_shared<const std::vector<NodeId>>(node->children);
		geometry = geometrySet(
			geometry,
			identity,
			{.routeGeneration = branch->routeGeneration,
				.bounds = node->bounds,
				.rectangularClip = incoming.rectangularClip,
				.parent = node->parent,
				.eligible = context.eligible,
				.children = orderedChildren}
		);
	} else {
		geometry = geometryErase(geometry, identity);
	}
	auto childContext = context;
	childContext.parentRoute = branch->routeGeneration;
	if (branch->paint->childClipBounds) {
		childContext.rectangularClip =
			intersect(incoming.rectangularClip, *branch->paint->childClipBounds);
	}
	for (std::size_t index = 0; index < node->children.size(); ++index) {
		childContext.siblingOrder = index;
		branch->children.push_back(
			project(node->children.at(index), childContext, geometry, promotions)
		);
	}
	if (previous && has(node->work.local, Dirty::structure)) {
		const std::set<NodeId> current{node->children.begin(), node->children.end()};
		for (const auto &child : previous->children) {
			if (!current.contains(child->id)) {
				eraseGeometry(child, geometry);
			}
		}
	}
	promotions.emplace_back(identity, branch);
	return branch;
}

auto Document::scene() const -> std::shared_ptr<const SceneSnapshot> {
	if (!render_) {
		return {};
	}
	if (!scene_) {
		auto result = std::make_shared<SceneSnapshot>();
		result->revision = render_->revision;
		result->size = render_->size;
		result->background = render_->background;
		flattenPaint(render_->root, result->commands);
		scene_ = std::move(result);
		++metrics_.sceneMaterializations;
	}
	return scene_;
}

auto Document::snapshots() const -> const std::vector<NodeSnapshot> & {
	if (!hasNodeMaterialization_ && render_) {
		snapshots_.clear();
		flattenNodes(render_->root, snapshots_);
		hasNodeMaterialization_ = true;
		++metrics_.nodeMaterializations;
	}
	return snapshots_;
}

void Document::commit() {
	auto &runtime = ReactiveRuntime::current();
	if (isClosed_ || isCommitting_ || runtime.depth != 0) {
		return;
	}
	// Direct native/test commits enter the same existing settlement path as bindings.
	if (!runtime.flushing) {
		runtime.flush();
	}
	if (isClosed_ || !isDirty_ || failedStamp_ == mutationStamp_ || !runtime.pending.empty()) {
		return;
	}

	isCommitting_ = true;
	try {
		constexpr unsigned settlementLimit = 10000; // Match the existing reactive settlement bound.
		for (unsigned iteration = 0; iteration < settlementLimit; ++iteration) {
			const auto stamp = mutationStamp_;
			const auto previousMeasurements = metrics_.measurements;
			const auto previousArrangements = metrics_.arrangements;
			try {
				static_cast<void>(measure(root_, viewport_));
				arrange(root_, {.size = viewport_});
			} catch (...) {
				// Defer to the existing runtime before any publication or effects.
				if (!runtime.pending.empty()) {
					isCommitting_ = false;
					return;
				}

				// Providers may reenter removal or mutation. Never continue using their borrowed
				// inputs.
				if (isClosed_) {
					isCommitting_ = false;
					return;
				}
				if (stamp != mutationStamp_ && !isClosed_) {
					continue;
				}
				throw;
			}
			if (isClosed_) {
				isCommitting_ = false;
				return;
			}
			auto draft = std::make_shared<RenderSnapshot>();
			if (metrics_.sceneRevisions == std::numeric_limits<std::uint64_t>::max()) {
				throw std::overflow_error("Scene revision exhausted");
			}
			draft->revision = metrics_.sceneRevisions + 1;
			draft->size = viewport_;
			draft->background = environment_.theme == Theme::dark ? Color::black() : Color::white();
			draft->textEnvironment = textEnvironment_;
			draft->geometry = render_ ? render_->geometry : GeometryRoot{};
			std::vector<std::pair<NodeId, std::shared_ptr<const SceneBranch>>> promotions;
			draft->root = project(
				root_,
				{.rectangularClip = {.size = viewport_}},
				draft->geometry,
				promotions
			);
			// Projection has no application callbacks. Only now promote its caches and publication.
			for (auto &[identity, branch] : promotions) {
				if (auto *node = find(identity)) {
					node->work.projection = std::move(branch);
					node->work.local = Dirty::none;
					node->work.projectionChildren.clear();
				}
			}
			render_ = std::move(draft);
			scene_.reset();
			snapshots_.clear();
			hasNodeMaterialization_ = false;
			++metrics_.sceneRevisions;
			if (metrics_.measurements != previousMeasurements ||
				metrics_.arrangements != previousArrangements) {
				++metrics_.layoutPasses;
			}
			isDirty_ = false;
			failedStamp_.reset();
			rememberGeometry();
			isCommitting_ = false;
			if (onPublish_) {
				onPublish_();
			}
			return;
		}
		throw std::runtime_error("Text measurement did not settle");
	} catch (...) {
		isCommitting_ = false;
		failedStamp_ = mutationStamp_;
		// Keep failed input diagnosed and caches invalid until a new retained/service stamp
		// arrives.
		for (const auto &[serial, node] : nodes_) {
			static_cast<void>(serial);
			node->work.local = fullWork;
			node->work.measuredAt.reset();
			node->work.arrangedAt.reset();
		}
		throw;
	}
}
} // namespace detail
} // namespace stana
