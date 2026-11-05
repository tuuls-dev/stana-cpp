#ifndef STANA_SCENE_CONVERSION_HPP
#define STANA_SCENE_CONVERSION_HPP

// Included after imports; shared by the production bridge and private native fixtures.
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
	case stana::rendering::PaintKind::popClip:
		operation = stana::platform::detail::PaintOperation::popClip;
		break;
	}
	return {
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
}

[[nodiscard]] inline auto convertRect(Rect rect) -> stana::platform::detail::LogicalRect {
	return {
		.origin = {.x = rect.origin.x, .y = rect.origin.y},
		.size = {.width = rect.size.width, .height = rect.size.height}
	};
}

/// @brief One window's last paired roots; no process-global cache or borrowed-address lookup.
struct SceneConversion {
	GPUConversion gpuConversion;
	std::shared_ptr<const stana::rendering::detail::RenderSnapshot> source;
	std::shared_ptr<const stana::platform::detail::Scene> converted;

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
