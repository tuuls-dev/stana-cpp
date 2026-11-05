module;

#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

module stana;
import :ui;
import :document;
import :primitives;
import :reactive;

namespace stana::ui {
namespace detail {
namespace {
void validateColor(Color color) {
	for (const double component : {color.red, color.green, color.blue, color.alpha}) {
		stana::detail::requireNonnegative(component);
		if (component > 1) {
			throw std::invalid_argument("Color exceeds one");
		}
	}
}
} // namespace

void applyLayout(
	const stana::layout::LayoutProperties &properties,
	NodeId identity,
	MountContext &context
) {
	if (properties.padding) {
		context.document->bind(
			identity,
			*properties.padding,
			[](stana::document::detail::Node &node,
				Insets value) -> stana::document::detail::Dirty {
				stana::detail::requireNonnegative(value.top);
				stana::detail::requireNonnegative(value.leading);
				stana::detail::requireNonnegative(value.bottom);
				stana::detail::requireNonnegative(value.trailing);
				return stana::ui::detail::assignRetained(
					node.padding,
					value,
					stana::document::detail::Dirty::measure |
						stana::document::detail::Dirty::arrange |
						stana::document::detail::Dirty::paint |
						stana::document::detail::Dirty::snapshot
				);
			}
		);
	}
	if (properties.frame) {
		context.document->bind(
			identity,
			*properties.frame,
			[](stana::document::detail::Node &node,
				const Frame &value) -> stana::document::detail::Dirty {
				stana::detail::requireNonnegative(value.width.value);
				stana::detail::requireNonnegative(value.height.value);
				for (const auto limit :
					{value.minWidth, value.minHeight, value.maxWidth, value.maxHeight}) {
					if (limit) {
						stana::detail::requireNonnegative(*limit);
					}
				}
				return stana::ui::detail::assignRetained(
					node.frame,
					value,
					stana::document::detail::Dirty::measure |
						stana::document::detail::Dirty::arrange |
						stana::document::detail::Dirty::paint |
						stana::document::detail::Dirty::snapshot
				);
			}
		);
	}
}

void applyTextStyle(const TextProperties &properties, NodeId identity, MountContext &context) {
	if (properties.font) {
		context.document->bind(
			identity,
			*properties.font,
			[](stana::document::detail::Node &node, Font value) -> stana::document::detail::Dirty {
				stana::detail::requireNonnegative(value.size);
				stana::detail::requireFinite(value.weight);
				return stana::ui::detail::assignRetained(
					node.font,
					value,
					stana::document::detail::Dirty::measure | stana::document::detail::Dirty::paint
				);
			}
		);
	}
	if (properties.foreground) {
		context.document->bind(
			identity,
			*properties.foreground,
			[](stana::document::detail::Node &node, Color value) -> stana::document::detail::Dirty {
				validateColor(value);
				const bool wasExplicit = node.isForegroundExplicit;
				const auto changed = stana::ui::detail::assignRetained(
					node.foreground,
					value,
					stana::document::detail::Dirty::paint
				);
				node.isForegroundExplicit = true;
				return wasExplicit ? changed : stana::document::detail::Dirty::paint;
			}
		);
	}
}

void applyAppearance(
	const AppearanceProperties &properties,
	NodeId identity,
	MountContext &context
) {
	if (properties.background) {
		context.document->bind(
			identity,
			*properties.background,
			[](stana::document::detail::Node &node, Color value) -> stana::document::detail::Dirty {
				validateColor(value);
				return stana::ui::detail::assignRetained(
					node.background,
					value,
					stana::document::detail::Dirty::paint
				);
			}
		);
	}
	if (properties.opacity) {
		context.document->bind(
			identity,
			*properties.opacity,
			[](stana::document::detail::Node &node,
				double value) -> stana::document::detail::Dirty {
				stana::detail::requireNonnegative(value);
				if (value > 1) {
					throw std::invalid_argument("Opacity exceeds one");
				}
				return stana::ui::detail::assignRetained(
					node.opacity,
					value,
					stana::document::detail::Dirty::paint
				);
			}
		);
	}
	if (properties.borderColor) {
		context.document->bind(
			identity,
			*properties.borderColor,
			[](stana::document::detail::Node &node, Color value) -> stana::document::detail::Dirty {
				validateColor(value);
				return stana::ui::detail::assignRetained(
					node.borderColor,
					value,
					stana::document::detail::Dirty::paint
				);
			}
		);
	}
	if (properties.borderWidth) {
		context.document->bind(
			identity,
			*properties.borderWidth,
			[](stana::document::detail::Node &node,
				double value) -> stana::document::detail::Dirty {
				stana::detail::requireNonnegative(value);
				return stana::ui::detail::assignRetained(
					node.borderWidth,
					value,
					stana::document::detail::Dirty::paint
				);
			}
		);
	}
	if (properties.cornerRadius) {
		context.document->bind(
			identity,
			*properties.cornerRadius,
			[](stana::document::detail::Node &node,
				double value) -> stana::document::detail::Dirty {
				stana::detail::requireNonnegative(value);
				return stana::ui::detail::assignRetained(
					node.cornerRadius,
					value,
					stana::document::detail::Dirty::paint
				);
			}
		);
	}
	if (properties.visibility) {
		context.document->bind(
			identity,
			*properties.visibility,
			[](stana::document::detail::Node &node,
				Visibility value) -> stana::document::detail::Dirty {
				if (node.visibility == value) {
					return stana::document::detail::Dirty::none;
				}
				const bool collapsed =
					node.visibility == Visibility::collapsed || value == Visibility::collapsed;
				node.visibility = value;
				return collapsed ? stana::document::detail::Dirty::measure |
						stana::document::detail::Dirty::arrange |
						stana::document::detail::Dirty::paint |
						stana::document::detail::Dirty::snapshot
								 : stana::document::detail::Dirty::paint |
						stana::document::detail::Dirty::snapshot;
			}
		);
	}
	if (properties.clip.has_value()) {
		context.document->find(identity)->clip = *properties.clip;
	}
}

void applyInteraction(
	const InteractionProperties &properties,
	NodeId identity,
	MountContext &context
) {
	context.document->bind(
		identity,
		properties.isEnabled,
		[](stana::document::detail::Node &node, bool value) -> stana::document::detail::Dirty {
			return stana::ui::detail::assignRetained(
				node.isEnabled,
				value,
				stana::document::detail::Dirty::paint | stana::document::detail::Dirty::snapshot
			);
		}
	);
	auto *node = context.document->find(identity);
	node->inert = properties.inert;
	node->onPointerDown = properties.onPointerDown;
	node->onPointerDownCapture = properties.onPointerDownCapture;
}

void applyAccessibility(
	const AccessibilityProperties &properties,
	NodeId identity,
	MountContext &context
) {
	context.document->find(identity)->accessibilityLabel = properties.label;
}

void applyLifecycle(const LifecycleProperties &properties, NodeId identity, MountContext &context) {
	auto *node = context.document->find(identity);
	node->diagnosticName = properties.diagnosticName;
	if (properties.onDispose) {
		static_cast<void>(node->scope.own(properties.onDispose));
	}
	if (properties.onMount) {
		const auto handle = context.document->handle(identity);
		stana::reactive::detail::ReactiveRuntime::current().effects.emplace_back(
			[handle, action = properties.onMount] -> void {
				if (handle.isAlive()) {
					action(handle);
				}
			}
		);
	}
}
} // namespace detail

auto mount(const Empty &, MountContext &context) -> stana::document::NodeHandle {
	return context.document->handle(
		context.document->createNode(stana::document::detail::NodeKind::group, context.parent)
	);
}

auto mount(const Text &description, MountContext &context) -> stana::document::NodeHandle {
	const auto identity = stana::ui::detail::createConfigured(
		description,
		stana::document::detail::NodeKind::text,
		context
	);
	context.document->bind(
		identity,
		description.text,
		[](stana::document::detail::Node &node,
			const std::string &value) -> stana::document::detail::Dirty {
			return stana::ui::detail::assignRetained(
				node.text,
				value,
				stana::document::detail::Dirty::measure | stana::document::detail::Dirty::paint |
					stana::document::detail::Dirty::snapshot
			);
		}
	);
	return context.document->handle(identity);
}

auto mount(const Button &description, MountContext &context) -> stana::document::NodeHandle {
	const auto identity = stana::ui::detail::createConfigured(
		description,
		stana::document::detail::NodeKind::button,
		context
	);
	context.document->bind(
		identity,
		description.text,
		[](stana::document::detail::Node &node,
			const std::string &value) -> stana::document::detail::Dirty {
			return stana::ui::detail::assignRetained(
				node.text,
				value,
				stana::document::detail::Dirty::measure | stana::document::detail::Dirty::paint |
					stana::document::detail::Dirty::snapshot
			);
		}
	);
	context.document->find(identity)->onClick = description.onClick;
	return context.document->handle(identity);
}

auto mount(const Toggle &description, MountContext &context) -> stana::document::NodeHandle {
	const auto identity = stana::ui::detail::createConfigured(
		description,
		stana::document::detail::NodeKind::toggle,
		context
	);
	context.document->bind(
		identity,
		description.text,
		[](stana::document::detail::Node &node,
			const std::string &value) -> stana::document::detail::Dirty {
			return stana::ui::detail::assignRetained(
				node.text,
				value,
				stana::document::detail::Dirty::measure | stana::document::detail::Dirty::paint |
					stana::document::detail::Dirty::snapshot
			);
		}
	);
	const auto value = description.value;
	context.document->bind(
		identity,
		stana::reactive::Property<bool>{stana::reactive::computed([value] -> bool {
			return value.get();
		})},
		[](stana::document::detail::Node &node, bool checked) -> stana::document::detail::Dirty {
			return stana::ui::detail::assignRetained(
				node.checked,
				checked,
				stana::document::detail::Dirty::snapshot
			);
		}
	);
	context.document->find(identity)->onClick = [value] -> void {
		value.set(!value.get());
	};
	return context.document->handle(identity);
}

auto mount(const Spacer &description, MountContext &context) -> stana::document::NodeHandle {
	const auto identity = stana::ui::detail::createConfigured(
		description,
		stana::document::detail::NodeKind::spacer,
		context
	);
	return context.document->handle(identity);
}

auto mount(const CPUCanvas &description, MountContext &context) -> stana::document::NodeHandle {
	const auto identity = stana::ui::detail::createConfigured(
		description,
		stana::document::detail::NodeKind::cpuCanvas,
		context
	);
	context.document->find(identity)->clip = description.appearance.clip.value_or(true);
	context.document->bind(
		identity,
		description.drawing,
		[](stana::document::detail::Node &node,
			const stana::rendering::Drawing &value) -> stana::document::detail::Dirty {
			value.validate();
			if (node.drawing == value) {
				return stana::document::detail::Dirty::none;
			}
			stana::rendering::Drawing prepared = value;
			node.drawing.commands.swap(prepared.commands);
			return stana::document::detail::Dirty::paint;
		}
	);
	return context.document->handle(identity);
}

auto mount(const GPUCanvas &description, MountContext &context) -> stana::document::NodeHandle {
	const auto identity = stana::ui::detail::createConfigured(
		description,
		stana::document::detail::NodeKind::gpuCanvas,
		context
	);
	context.document->find(identity)->clip = description.appearance.clip.value_or(true);
	context.document->find(identity)->onGPUError = description.onError;
	context.document->bind(
		identity,
		description.drawing,
		[](stana::document::detail::Node &node,
			const stana::rendering::GPUDrawing &value) -> stana::document::detail::Dirty {
			if (node.gpuDrawing && *node.gpuDrawing == value) {
				return stana::document::detail::Dirty::none;
			}
			node.gpuDrawing = value;
			return stana::document::detail::Dirty::paint;
		}
	);
	return context.document->handle(identity);
}
} // namespace stana::ui
