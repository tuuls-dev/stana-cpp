#ifndef STANA_SCENE_CONVERSION_HPP
#define STANA_SCENE_CONVERSION_HPP

// Included after imports; shared by the production bridge and private native fixtures.
// Standard headers belong in the includer, before its module declaration.
namespace stana::application::detail {
[[nodiscard]] inline auto convertCommand(const stana::rendering::PaintCommand &command)
	-> stana::platform::detail::Command {

	stana::platform::detail::PaintOperation operation{};
	switch (command.kind) {
	case stana::rendering::PaintKind::fillRect:
		operation = stana::platform::detail::PaintOperation::fill;
		break;
	case stana::rendering::PaintKind::strokeRect:
		operation = stana::platform::detail::PaintOperation::stroke;
		break;
	case stana::rendering::PaintKind::text:
		operation = stana::platform::detail::PaintOperation::text;
		break;
	case stana::rendering::PaintKind::line:
		operation = stana::platform::detail::PaintOperation::line;
		break;
	case stana::rendering::PaintKind::pushClip:
		operation = stana::platform::detail::PaintOperation::pushClip;
		break;
	case stana::rendering::PaintKind::boxShadow:
		operation = stana::platform::detail::PaintOperation::shadow;
		break;
	case stana::rendering::PaintKind::popClip:
		operation = stana::platform::detail::PaintOperation::popClip;
		break;
	}
	stana::platform::detail::Command result{
		.operation = operation,
		.x = command.bounds.origin.x,
		.y = command.bounds.origin.y,
		.width = command.bounds.size.width,
		.height = command.bounds.size.height,
		.red = command.color.red,
		.green = command.color.green,
		.blue = command.color.blue,
		.alpha = command.color.alpha,
		.radius = command.cornerRadius,
		.lineWidth = command.lineWidth,
		.text = command.text,
		.fontSize = command.font.size,
		.fontWeight = command.font.weight
	};
	if (command.boxGeometry) {
		result.boxGeometry = stana::gpu::detail::BoxGeometry{
			.outer = convertContour(command.boxGeometry->outer),
			.padding = convertContour(command.boxGeometry->padding)
		};
	}
	if (command.shadow) {
		result.shadow = convertShadow(*command.shadow);
	}
	return result;
}

[[nodiscard]] inline auto convertRect(Rect rect) -> stana::platform::detail::LogicalRect {
	return {
		.origin = {.x = rect.origin.x, .y = rect.origin.y},
		.size = {.width = rect.size.width, .height = rect.size.height}
	};
}

[[nodiscard]] inline auto convertAffine(stana::AffineTransform value)
	-> stana::platform::detail::SymbolAffine {
	return {.a = value.a, .b = value.b, .c = value.c, .d = value.d, .tx = value.tx, .ty = value.ty};
}

namespace symbol_transport {
inline constexpr std::uint64_t hashOffset = 14695981039346656037ULL;
inline constexpr std::uint64_t hashPrime = 1099511628211ULL;
inline constexpr std::uint64_t groupTag = 1;
inline constexpr std::uint64_t pathTag = 2;
inline constexpr std::uint64_t presentTag = 3;
inline constexpr std::uint64_t absentTag = 4;

struct Hasher {
	std::uint64_t commands = hashOffset;
	std::uint64_t stroke = hashOffset;
	std::uint64_t content = hashOffset;

	static auto step(std::uint64_t hash, std::uint64_t bits) -> std::uint64_t {
		hash ^= bits;
		hash *= hashPrime;
		return hash;
	}

	void mixCommand(std::uint64_t bits) {
		commands = step(commands, bits);
		content = step(content, bits);
	}

	void mixStroke(std::uint64_t bits) {
		stroke = step(stroke, bits);
		content = step(content, bits);
	}

	void mixContent(std::uint64_t bits) { content = step(content, bits); }

	void mixCommand(double value) { mixCommand(std::bit_cast<std::uint64_t>(value)); }

	void mixStroke(double value) { mixStroke(std::bit_cast<std::uint64_t>(value)); }

	void mixContent(double value) { mixContent(std::bit_cast<std::uint64_t>(value)); }
};

inline void mixAffine(Hasher &hasher, stana::platform::detail::SymbolAffine value) {
	hasher.mixContent(value.a);
	hasher.mixContent(value.b);
	hasher.mixContent(value.c);
	hasher.mixContent(value.d);
	hasher.mixContent(value.tx);
	hasher.mixContent(value.ty);
}

inline void mixRect(Hasher &hasher, stana::platform::detail::LogicalRect rect) {
	hasher.mixContent(rect.origin.x);
	hasher.mixContent(rect.origin.y);
	hasher.mixContent(rect.size.width);
	hasher.mixContent(rect.size.height);
}

inline void mixColor(Hasher &hasher, stana::platform::detail::SceneColor color) {
	hasher.mixContent(color.red);
	hasher.mixContent(color.green);
	hasher.mixContent(color.blue);
	hasher.mixContent(color.alpha);
}

[[nodiscard]] inline auto discriminant(auto value) -> std::uint64_t {
	return static_cast<std::uint64_t>(static_cast<std::uint8_t>(value));
}

[[nodiscard]] inline auto
resolvePaint(const stana::symbol::Paint &paint, stana::Color tint, double opacity)
	-> std::optional<stana::platform::detail::SceneColor> {
	return std::visit(
		[&](const auto &value) -> std::optional<stana::platform::detail::SceneColor> {
			using Paint = std::decay_t<decltype(value)>;
			if constexpr (std::is_same_v<Paint, stana::symbol::NoPaint>) {
				return std::nullopt;
			} else if constexpr (std::is_same_v<Paint, stana::symbol::CurrentColor>) {
				return stana::platform::detail::SceneColor{
					.red = tint.red,
					.green = tint.green,
					.blue = tint.blue,
					.alpha = tint.alpha * opacity
				};
			} else {
				static_assert(std::is_same_v<Paint, stana::symbol::RgbaColor>);
				return stana::platform::detail::SceneColor{
					.red = value.red,
					.green = value.green,
					.blue = value.blue,
					.alpha = value.alpha * opacity
				};
			}
		},
		paint
	);
}

[[nodiscard]] inline auto convertRule(stana::symbol::FillRule rule)
	-> stana::platform::detail::SymbolFillRule {
	switch (rule) {
	case stana::symbol::FillRule::nonzero:
		return stana::platform::detail::SymbolFillRule::nonzero;
	case stana::symbol::FillRule::evenodd:
		return stana::platform::detail::SymbolFillRule::evenodd;
	}
	throw std::logic_error("Unknown symbol fill rule");
}

[[nodiscard]] inline auto convertCap(stana::symbol::LineCap cap)
	-> stana::platform::detail::SymbolLineCap {
	switch (cap) {
	case stana::symbol::LineCap::butt:
		return stana::platform::detail::SymbolLineCap::butt;
	case stana::symbol::LineCap::round:
		return stana::platform::detail::SymbolLineCap::round;
	case stana::symbol::LineCap::square:
		return stana::platform::detail::SymbolLineCap::square;
	}
	throw std::logic_error("Unknown symbol line cap");
}

[[nodiscard]] inline auto convertJoin(stana::symbol::LineJoin join)
	-> stana::platform::detail::SymbolLineJoin {
	switch (join) {
	case stana::symbol::LineJoin::miter:
		return stana::platform::detail::SymbolLineJoin::miter;
	case stana::symbol::LineJoin::round:
		return stana::platform::detail::SymbolLineJoin::round;
	case stana::symbol::LineJoin::bevel:
		return stana::platform::detail::SymbolLineJoin::bevel;
	}
	throw std::logic_error("Unknown symbol line join");
}

inline void mixVerb(Hasher &hasher, const stana::platform::detail::SymbolVerbData &verb) {
	hasher.mixCommand(discriminant(verb.kind));
	hasher.mixCommand(verb.x);
	hasher.mixCommand(verb.y);
	hasher.mixCommand(verb.c1x);
	hasher.mixCommand(verb.c1y);
	hasher.mixCommand(verb.c2x);
	hasher.mixCommand(verb.c2y);
	hasher.mixCommand(verb.rotation);
	hasher.mixCommand(verb.sweep);
}

[[nodiscard]] inline auto
convertCommands(const std::vector<stana::symbol::PathCommand> &commands, Hasher &hasher)
	-> std::vector<stana::platform::detail::SymbolVerbData> {
	std::vector<stana::platform::detail::SymbolVerbData> converted;
	converted.reserve(commands.size());
	for (const auto &command : commands) {
		std::visit(
			[&](const auto &verb) -> void {
				using Verb = std::decay_t<decltype(verb)>;
				stana::platform::detail::SymbolVerbData data{};
				if constexpr (std::is_same_v<Verb, stana::symbol::MoveTo>) {
					data = {
						.kind = stana::platform::detail::SymbolVerb::move,
						.x = verb.x,
						.y = verb.y
					};
				} else if constexpr (std::is_same_v<Verb, stana::symbol::LineTo>) {
					data = {
						.kind = stana::platform::detail::SymbolVerb::line,
						.x = verb.x,
						.y = verb.y
					};
				} else if constexpr (std::is_same_v<Verb, stana::symbol::QuadraticTo>) {
					data = {
						.kind = stana::platform::detail::SymbolVerb::quadratic,
						.x = verb.x,
						.y = verb.y,
						.c1x = verb.controlX,
						.c1y = verb.controlY
					};
				} else if constexpr (std::is_same_v<Verb, stana::symbol::CubicTo>) {
					data = {
						.kind = stana::platform::detail::SymbolVerb::cubic,
						.x = verb.x,
						.y = verb.y,
						.c1x = verb.control1X,
						.c1y = verb.control1Y,
						.c2x = verb.control2X,
						.c2y = verb.control2Y
					};
				} else if constexpr (std::is_same_v<Verb, stana::symbol::ArcTo>) {
					data = {
						.kind = stana::platform::detail::SymbolVerb::arc,
						.x = verb.centerX,
						.y = verb.centerY,
						.c1x = verb.radiusX,
						.c1y = verb.radiusY,
						.c2x = verb.startAngle,
						.rotation = verb.rotation,
						.sweep = verb.sweepAngle
					};
				} else {
					static_assert(std::is_same_v<Verb, stana::symbol::ClosePath>);
					data = {.kind = stana::platform::detail::SymbolVerb::close};
				}
				mixVerb(hasher, data);
				converted.push_back(data);
			},
			command
		);
	}
	return converted;
}

[[nodiscard]] inline auto visitElements(
	const std::vector<stana::symbol::Element> &elements,
	stana::AffineTransform parent,
	stana::Color tint,
	Hasher &hasher,
	bool &anyStroke
) -> std::vector<stana::platform::detail::SymbolNode> {
	std::vector<stana::platform::detail::SymbolNode> nodes;
	nodes.reserve(elements.size());
	for (const auto &element : elements) {
		if (const auto *group = std::get_if<stana::symbol::Element::Group>(&element.value)) {
			hasher.mixContent(groupTag);
			mixAffine(hasher, convertAffine(group->transform));
			hasher.mixContent(group->opacity);
			auto children = visitElements(
				group->children,
				parent.concatenating(group->transform),
				tint,
				hasher,
				anyStroke
			);
			nodes.push_back(
				{.value = stana::platform::detail::SymbolNode::Group{
					 .transform = convertAffine(group->transform),
					 .opacity = group->opacity,
					 .children = std::move(children)
				 }}
			);
			continue;
		}
		const auto &path = std::get<stana::symbol::Element::Path>(element.value);
		const auto cumulative = parent.concatenating(path.transform);
		const auto fillRule = convertRule(path.fillRule);
		const auto lineCap = convertCap(path.lineCap);
		const auto lineJoin = convertJoin(path.lineJoin);
		const auto fill = resolvePaint(path.fill, tint, path.fillOpacity);
		const auto stroke = resolvePaint(path.stroke, tint, path.strokeOpacity);
		hasher.mixCommand(pathTag);
		auto commands = convertCommands(path.commands, hasher);
		hasher.mixCommand(cumulative.maximumScale());
		if (stroke) {
			anyStroke = true;
			hasher.mixStroke(presentTag);
			hasher.mixStroke(path.strokeWidth);
			hasher.mixStroke(discriminant(lineCap));
			hasher.mixStroke(discriminant(lineJoin));
			hasher.mixStroke(path.miterLimit);
		}
		hasher.mixContent(path.opacity);
		hasher.mixContent(path.fillOpacity);
		hasher.mixContent(path.strokeOpacity);
		hasher.mixContent(discriminant(fillRule));
		mixAffine(hasher, convertAffine(path.transform));
		if (fill) {
			hasher.mixContent(presentTag);
			mixColor(hasher, *fill);
		} else {
			hasher.mixContent(absentTag);
		}
		if (stroke) {
			hasher.mixContent(presentTag);
			mixColor(hasher, *stroke);
		} else {
			hasher.mixContent(absentTag);
		}
		nodes.push_back(
			{.value = stana::platform::detail::SymbolNode::Path{
				 .commands = std::move(commands),
				 .transform = convertAffine(path.transform),
				 .localMaximumScale = cumulative.maximumScale(),
				 .fill = fill,
				 .fillRule = fillRule,
				 .fillOpacity = path.fillOpacity,
				 .stroke = stroke,
				 .strokeOpacity = path.strokeOpacity,
				 .strokeWidth = path.strokeWidth,
				 .lineCap = lineCap,
				 .lineJoin = lineJoin,
				 .miterLimit = path.miterLimit,
				 .opacity = path.opacity
			 }}
		);
	}
	return nodes;
}
} // namespace symbol_transport

[[nodiscard]] inline auto convertSymbol(const stana::rendering::detail::SymbolLeaf &leaf)
	-> stana::platform::detail::SymbolLeaf {
	const auto &presentation = leaf.presentation;
	stana::platform::detail::SymbolLeaf result{
		.instance = {.document = leaf.instance.document, .serial = leaf.instance.serial},
		.bounds = convertRect(leaf.bounds),
		.ink = convertRect(leaf.ink),
		.transform = convertAffine(presentation.transform),
		.strokeWidth = presentation.strokeWidth,
		.generation = presentation.generation,
		.preparationLimited = presentation.preparationLimited,
		.preparationScale = presentation.preparationScale,
		.opacity = leaf.opacity,
		.estimatedBytes = presentation.asset.estimatedBytes()
	};
	symbol_transport::Hasher hasher;
	bool anyStroke = false;
	result.elements = symbol_transport::visitElements(
		presentation.asset.elements(),
		stana::AffineTransform::identity(),
		presentation.color,
		hasher,
		anyStroke
	);
	symbol_transport::mixAffine(hasher, result.transform);
	hasher.mixContent(result.opacity);
	hasher.mixContent(result.preparationScale);
	hasher.mixContent(result.generation);
	hasher.mixContent(
		result.preparationLimited ? symbol_transport::presentTag : symbol_transport::absentTag
	);
	if (result.strokeWidth) {
		hasher.mixContent(symbol_transport::presentTag);
		hasher.mixContent(*result.strokeWidth);
	} else {
		hasher.mixContent(symbol_transport::absentTag);
	}
	symbol_transport::mixRect(hasher, result.bounds);
	symbol_transport::mixRect(hasher, result.ink);
	result.anyStroke = anyStroke;
	result.commandHash = hasher.commands;
	result.strokeHash = hasher.stroke;
	result.contentHash = hasher.content;
	return result;
}

/// @brief One window's last paired roots; no process-global cache or borrowed-address lookup.
struct SceneConversion {
	GPUConversion gpuConversion;
	std::shared_ptr<const stana::rendering::detail::RenderSnapshot> source;
	std::shared_ptr<const stana::platform::detail::Scene> converted;
	std::function<void(stana::platform::detail::SymbolFailure)> symbolError{};

	[[nodiscard]] auto branch(
		const std::shared_ptr<const stana::rendering::detail::SceneBranch> &next,
		const std::shared_ptr<const stana::rendering::detail::SceneBranch> &old,
		const std::shared_ptr<const stana::platform::detail::SceneBranch> &previous
	) -> std::shared_ptr<const stana::platform::detail::SceneBranch> {
		if (!next) {
			return {};
		}
		if (old && previous && next->id == old->id && next->generation == old->generation) {
			return previous;
		}
		auto result = std::make_shared<stana::platform::detail::SceneBranch>();
		result->id = {.document = next->id.document, .serial = next->id.serial};
		result->generation = next->generation;
		result->visible = next->context.visible;
		if (old && previous && next->paint->identity == old->paint->identity) {
			result->paint = previous->paint;
		} else {
			auto paint = std::make_shared<stana::platform::detail::PaintSegments>();
			paint->identity = next->paint->identity;
			for (const auto &command : next->paint->prefix) {
				paint->prefix.push_back(convertCommand(command));
			}
			for (const auto &command : next->paint->suffix) {
				paint->suffix.push_back(convertCommand(command));
			}
			if (next->paint->childClipBounds) {
				paint->childClipBounds = convertRect(*next->paint->childClipBounds);
			}
			if (const auto &gpu = next->paint->gpu) {
				paint->gpu = stana::platform::detail::GPUCanvasLeaf{
					.instance =
						{.document = gpu->instance.document, .serial = gpu->instance.serial},
					.bounds = convertRect(gpu->bounds),
					.drawing = gpuConversion.drawing(gpu->drawing, gpu->environment),
					.opacity = gpu->opacity
				};
			}
			if (const auto &symbol = next->paint->symbol) {
				paint->symbol = convertSymbol(*symbol);
			}
			result->paint = std::move(paint);
		}
		std::map<NodeId, std::size_t> pairs;
		if (old && previous) {
			for (std::size_t index = 0; index < old->children.size(); ++index) {
				pairs.emplace(old->children.at(index)->id, index);
			}
		}
		for (const auto &child : next->children) {
			const auto paired = pairs.find(child->id);
			result->children.push_back(
				paired == pairs.end() ? branch(child, {}, {})
									  : branch(
											child,
											old->children.at(paired->second),
											previous->children.at(paired->second)
										)
			);
		}
		return result;
	}

	[[nodiscard]] auto convert(std::shared_ptr<const stana::rendering::detail::RenderSnapshot> next)
		-> std::shared_ptr<const stana::platform::detail::Scene> {
		if (next == source) {
			return converted;
		}
		auto scene = std::make_shared<stana::platform::detail::Scene>();
		scene->revision = next->revision;
		scene->size = {.width = next->size.width, .height = next->size.height};
		scene->background = {
			.red = next->background.red,
			.green = next->background.green,
			.blue = next->background.blue,
			.alpha = next->background.alpha
		};
		scene->textEnvironment = next->textEnvironment;
		scene->symbolError = symbolError;
		scene->root = branch(
			next->root,
			source ? source->root : nullptr,
			converted ? converted->root : nullptr
		);
		source = std::move(next);
		converted = std::move(scene);
		return converted;
	}
};
} // namespace stana::application::detail
#endif
