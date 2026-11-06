module;

#include <algorithm>
#include <cmath>
#include <compare>
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

module stana;
import :document;
import :primitives;
import :reactive;
import :rendering;
import :symbol_geometry;

namespace stana::rendering::detail {
namespace {
constexpr double disabledOpacity = 0.45;
constexpr double focusBorderWidth = 2;

[[nodiscard]] auto nodeKindName(stana::document::detail::NodeKind kind) -> std::string {
	using enum stana::document::detail::NodeKind;
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
	case cpuCanvas:
		return "cpuCanvas";
	case gpuCanvas:
		return "gpuCanvas";
	case symbol:
		return "symbol";
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

void flattenPaint(
	const std::shared_ptr<const stana::rendering::detail::SceneBranch> &branch,
	std::vector<stana::rendering::PaintCommand> &commands
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
	const std::shared_ptr<const stana::rendering::detail::SceneBranch> &branch,
	std::vector<stana::document::NodeSnapshot> &nodes
) {
	if (!branch || !branch->context.visible) {
		return;
	}
	nodes.push_back(*branch->snapshot);
	for (const auto &child : branch->children) {
		flattenNodes(child, nodes);
	}
}

void eraseGeometry(
	const std::shared_ptr<const stana::rendering::detail::SceneBranch> &branch,
	stana::rendering::detail::GeometryRoot &geometry
) {
	if (!branch) {
		return;
	}
	geometry = stana::rendering::detail::geometryErase(geometry, branch->id);
	for (const auto &child : branch->children) {
		eraseGeometry(child, geometry);
	}
}
} // namespace

} // namespace stana::rendering::detail

namespace stana::document::detail {
namespace {
[[nodiscard]] auto symbolRectEmpty(Rect rect) -> bool {
	return std::is_eq(rect.size.width <=> 0.0) || std::is_eq(rect.size.height <=> 0.0);
}

[[nodiscard]] auto uniteSymbol(Rect left, Rect right) -> Rect {
	if (symbolRectEmpty(left)) {
		return right;
	}
	if (symbolRectEmpty(right)) {
		return left;
	}
	const double x = std::min(left.origin.x, right.origin.x);
	const double y = std::min(left.origin.y, right.origin.y);
	const double rightEdge =
		std::max(left.origin.x + left.size.width, right.origin.x + right.size.width);
	const double bottomEdge =
		std::max(left.origin.y + left.size.height, right.origin.y + right.size.height);
	return {.origin = {.x = x, .y = y}, .size = {.width = rightEdge - x, .height = bottomEdge - y}};
}
} // namespace

auto Document::project(
	NodeId identity,
	stana::rendering::detail::ProjectionContext incoming,
	stana::rendering::detail::GeometryRoot &geometry,
	std::vector<std::pair<NodeId, std::shared_ptr<const stana::rendering::detail::SceneBranch>>>
		&promotions
) -> std::shared_ptr<const stana::rendering::detail::SceneBranch> {
	const auto *node = find(identity);
	if (node == nullptr) {
		return {};
	}
	auto previous = node->work.projection;
	auto context = incoming;
	context.opacity *=
		node->opacity * (node->isEnabled ? 1 : stana::rendering::detail::disabledOpacity);
	context.visible = incoming.visible && node->visibility == Visibility::visible;
	context.eligible = incoming.eligible && context.visible && node->isEnabled && !node->inert;
	if (previous && node->work.local == Dirty::none && node->work.projectionChildren.empty() &&
		std::is_eq(previous->context <=> context)) {
		return previous;
	}
	auto branch = std::make_shared<stana::rendering::detail::SceneBranch>();
	branch->routeGeneration = previous && previous->context.parentRoute == context.parentRoute &&
			previous->context.siblingOrder == context.siblingOrder
		? previous->routeGeneration
		: stana::detail::nextIdentity();
	branch->id = identity;
	branch->generation = stana::detail::nextIdentity();
	branch->context = context;
	++metrics_.projectionBuilds;
	const bool needsPaint = !previous || has(node->work.local, Dirty::paint) ||
		std::is_neq(previous->context.opacity <=> context.opacity);
	if (!needsPaint) {
		branch->paint = previous->paint;
	} else {
		node->drawing.validate();
		++metrics_.paintBuilds;
		auto paint = std::make_shared<stana::rendering::detail::PaintSegments>();
		paint->identity = stana::detail::nextIdentity();
		stana::rendering::SceneSnapshot scene;
		const double opacity = context.opacity;

		const auto radii = node->cornerRadii.value_or(CornerRadii::all(node->cornerRadius));
		const auto widths = node->borderWidths.value_or(BorderWidths::all(node->borderWidth));
		const auto geometry = stana::rendering::detail::boxGeometry(
			node->bounds,
			radii,
			widths,
			environment_.direction
		);
		const bool rich = node->cornerRadii.has_value() || node->borderWidths.has_value();
		const auto shadows = stana::rendering::detail::prepareBoxShadows(
			node->bounds,
			node->boxShadows,
			radii,
			widths,
			node->isForegroundExplicit ? node->foreground : Color::text(),
			environment_
		);
		const auto appendShadows = [&](bool inset) -> void {
			for (auto shadow : shadows) {
				if (shadow.isInset == inset) {
					shadow.color.alpha *= opacity;
					scene.commands.push_back(
						{.kind = stana::rendering::PaintKind::boxShadow,
							.bounds = shadow.ink,
							.shadow = std::move(shadow)}
					);
				}
			}
		};
		appendShadows(false);
		if (node->background.alpha > 0) {
			scene.commands.push_back(
				{.kind = stana::rendering::PaintKind::fillRect,
					.bounds = node->bounds,
					.color = stana::rendering::detail::withOpacity(
						node->background.resolved(environment_.theme, environment_.highContrast),
						opacity
					),
					.cornerRadius = node->cornerRadius,
					.boxGeometry = rich ? std::optional{geometry} : std::nullopt}
			);
		}
		appendShadows(true);
		if (node->clip) {
			scene.commands.push_back(
				{.kind = stana::rendering::PaintKind::pushClip,
					.bounds = node->bounds,
					.cornerRadius = node->cornerRadius,
					.boxGeometry = rich ? std::optional{geometry} : std::nullopt}
			);
		}
		if (node->kind == NodeKind::text || node->kind == NodeKind::button ||
			node->kind == NodeKind::toggle) {
			auto font = node->font;
			font.size *= environment_.textScale;
			stana::detail::requireNonnegative(font.size);
			const auto bounds = stana::layout::detail::contentRect(
				node->bounds,
				node->padding,
				environment_.direction
			);
			const Color foreground = node->isForegroundExplicit
				? node->foreground
				: stana::rendering::detail::themeLabel(environment_.theme);
			scene.commands.push_back(
				{.kind = stana::rendering::PaintKind::text,
					.bounds = bounds,
					.color = stana::rendering::detail::withOpacity(
						foreground.resolved(environment_.theme, environment_.highContrast),
						opacity
					),
					.text = node->text,
					.font = font}
			);
		}
		const auto content =
			stana::layout::detail::contentRect(node->bounds, node->padding, environment_.direction);
		for (auto command : node->drawing.commands) {
			if (command.kind != stana::rendering::PaintKind::popClip) {
				command.bounds.origin.x += content.origin.x;
				command.bounds.origin.y += content.origin.y;
				stana::detail::requireFinite(command.bounds.origin.x);
				stana::detail::requireFinite(command.bounds.origin.y);
				stana::detail::requireFinite(command.bounds.origin.x + command.bounds.size.width);
				stana::detail::requireFinite(command.bounds.origin.y + command.bounds.size.height);
				if (command.kind == stana::rendering::PaintKind::fillRect ||
					command.kind == stana::rendering::PaintKind::strokeRect ||
					command.kind == stana::rendering::PaintKind::pushClip) {
					command.cornerRadius = std::min(
						{command.cornerRadius,
							command.bounds.size.width / 2,
							command.bounds.size.height / 2}
					);
				}
				command.color = stana::rendering::detail::withOpacity(
					command.color.resolved(environment_.theme, environment_.highContrast),
					opacity
				);
			}
			scene.commands.push_back(std::move(command));
		}
		if (node->gpuDrawing) {
			paint->gpu = stana::rendering::detail::GPUCanvasLeaf{
				.instance = identity,
				.bounds = content,
				.drawing = *node->gpuDrawing,
				.environment = environment_,
				.opacity = opacity
			};
		}
		if (node->kind == NodeKind::symbol && node->symbol) {
			const auto &symbol = *node->symbol;
			const auto fitted = stana::symbol::square(symbol.size, content);
			auto presentation = stana::symbol::presentationFor(
				symbol.asset,
				symbol.size,
				content,
				environment_.direction,
				symbol.effect,
				symbol.color.resolved(environment_.theme, environment_.highContrast),
				symbol.strokeWidth,
				symbol.generation,
				symbol.available,
				symbol.ink
			);
			if (presentation) {
				const Rect viewport{.size = viewport_};
				Rect overflow = node->bounds;
				std::optional<Rect> mapped;
				if (!presentation->preparationLimited && symbol.ink) {
					mapped = stana::symbol::enclosing(presentation->transform, *symbol.ink);
				}
				overflow = uniteSymbol(overflow, mapped.value_or(viewport));
				paint->symbol = stana::rendering::detail::SymbolLeaf{
					.instance = identity,
					.bounds = fitted,
					.ink = overflow,
					.presentation = std::move(*presentation),
					.opacity = opacity
				};
			}
		}
		paint->prefix = std::move(scene.commands);
		if (node->clip) {
			paint->childClipBounds = node->bounds;
		}

		if (node->clip) {
			paint->suffix.push_back({.kind = stana::rendering::PaintKind::popClip});
		}
		if (node->borderWidth > 0 || node->borderWidths || focused_ == identity) {
			paint->suffix.push_back(
				{.kind = stana::rendering::PaintKind::strokeRect,
					.bounds = node->bounds,
					.color = stana::rendering::detail::withOpacity(
						(focused_ == identity ? Color::accent() : node->borderColor)
							.resolved(environment_.theme, environment_.highContrast),
						opacity
					),
					.cornerRadius = node->cornerRadius,
					.lineWidth = focused_ == identity ? stana::rendering::detail::focusBorderWidth
													  : node->borderWidth,
					.boxGeometry =
						rich && focused_ != identity ? std::optional{geometry} : std::nullopt}
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
			.kind = stana::rendering::detail::nodeKindName(node->kind),
			.text = node->text,
			.bounds = node->bounds,
			.isEnabled = context.eligible,
			.isFocused = focused_ == identity,
			.accessibilityLabel = node->accessibilityLabel
		});
	}
	if (context.visible) {
		const auto previousGeometry = stana::rendering::detail::geometryFind(geometry, identity);
		const auto orderedChildren = previousGeometry && previousGeometry->children &&
				*previousGeometry->children == node->children
			? previousGeometry->children
			: std::make_shared<const std::vector<NodeId>>(node->children);
		geometry = stana::rendering::detail::geometrySet(
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
		geometry = stana::rendering::detail::geometryErase(geometry, identity);
	}
	auto childContext = context;
	childContext.parentRoute = branch->routeGeneration;
	if (branch->paint->childClipBounds) {
		childContext.rectangularClip = stana::rendering::detail::intersect(
			incoming.rectangularClip,
			*branch->paint->childClipBounds
		);
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
				stana::rendering::detail::eraseGeometry(child, geometry);
			}
		}
	}
	promotions.emplace_back(identity, branch);
	return branch;
}

auto Document::scene() const -> std::shared_ptr<const stana::rendering::SceneSnapshot> {
	if (!render_) {
		return {};
	}
	if (!scene_) {
		auto result = std::make_shared<stana::rendering::SceneSnapshot>();
		result->revision = render_->revision;
		result->size = render_->size;
		result->background = render_->background;
		stana::rendering::detail::flattenPaint(render_->root, result->commands);
		scene_ = std::move(result);
		++metrics_.sceneMaterializations;
	}
	return scene_;
}

auto Document::snapshots() const -> const std::vector<NodeSnapshot> & {
	if (!hasNodeMaterialization_ && render_) {
		snapshots_.clear();
		stana::rendering::detail::flattenNodes(render_->root, snapshots_);
		hasNodeMaterialization_ = true;
		++metrics_.nodeMaterializations;
	}
	return snapshots_;
}

void Document::commit() {
	auto &runtime = stana::reactive::detail::ReactiveRuntime::current();
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
				reconcileSymbols();
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
			auto draft = std::make_shared<stana::rendering::detail::RenderSnapshot>();
			if (metrics_.sceneRevisions == std::numeric_limits<std::uint64_t>::max()) {
				throw std::overflow_error("Scene revision exhausted");
			}
			draft->revision = metrics_.sceneRevisions + 1;
			draft->size = viewport_;
			draft->background = environment_.theme == Theme::dark ? Color::black() : Color::white();
			draft->textEnvironment = textEnvironment_;
			draft->geometry =
				render_ ? render_->geometry : stana::rendering::detail::GeometryRoot{};
			std::vector<
				std::pair<NodeId, std::shared_ptr<const stana::rendering::detail::SceneBranch>>>
				promotions;
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

void Document::reconcileSymbols() {
	for (const auto &[serial, node] : nodes_) {
		static_cast<void>(serial);
		if (node->kind != NodeKind::symbol || !node->symbol) {
			continue;
		}
		auto &symbol = *node->symbol;
		const auto effectBefore = symbol.effect;
		const auto availableBefore = symbol.available;
		const auto inkBefore = symbol.ink;
		const auto generationBefore = symbol.generation;
		bool visible = hostVisible_;
		for (const Node *ancestor = node.get(); ancestor != nullptr;
			ancestor = find(ancestor->parent)) {
			if (ancestor->visibility != Visibility::visible) {
				visible = false;
				break;
			}
		}
		const auto content =
			stana::layout::detail::contentRect(node->bounds, node->padding, environment_.direction);
		const auto fitted = stana::symbol::square(symbol.size, content);
		const auto box = symbol.asset.viewBox();
		const double longest = std::max(box.size.width, box.size.height);
		const double fitScale =
			fitted.size.width > 0.0 && longest > 0.0 ? fitted.size.width / longest : 0.0;
		const std::optional<double> strokeKey =
			symbol.asset.hasStroke() ? symbol.strokeWidth : std::nullopt;
		const bool assetChanged = symbol.asset != symbol.previousAsset;
		const double previousStroke = symbol.previousStroke.value_or(0.0);
		const double nextStroke = strokeKey.value_or(0.0);
		const bool strokeChanged = symbol.previousStroke.has_value() != strokeKey.has_value() ||
			(symbol.previousStroke.has_value() && !std::is_eq(previousStroke <=> nextStroke));
		const double previousScale = symbol.previousScale.value_or(fitScale);
		const bool scaleChanged =
			!symbol.previousScale.has_value() || !std::is_eq(previousScale <=> fitScale);
		if (assetChanged || strokeChanged || scaleChanged) {
			symbol.available = true;
			symbol.failed.clear();
			symbol.generation = stana::detail::nextIdentity();
			symbol.ink = stana::symbol::inkBounds(symbol.asset, symbol.strokeWidth);
		}
		symbol.playbackState.reconcile(
			assetChanged,
			symbol.playback,
			environment_.reducedMotion,
			visible,
			symbol.available
		);
		if (symbol.playbackState.resetClock()) {
			resetFrameTimestamp(symbol.frameRegistration);
		}
		symbol.effect = symbol.playbackState.sample();
		symbol.previousAsset = symbol.asset;
		symbol.previousStroke = strokeKey;
		symbol.previousScale = fitScale;
		if (symbol.effect != effectBefore || symbol.available != availableBefore ||
			symbol.ink != inkBefore || symbol.generation != generationBefore) {
			invalidate(node->id, Dirty::paint);
		}
	}
	refreshFrameDemand();
}
} // namespace stana::document::detail
