module;

#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

module stana.ui;
import stana.core;
import stana.primitives;
import stana.reactive;

namespace stana {
namespace detail {
namespace {
void validateColor(Color color) {
	for (const double component : {color.red, color.green, color.blue, color.alpha}) {
		requireNonnegative(component);
		if (component > 1) {
			throw std::invalid_argument("Color exceeds one");
		}
	}
}
} // namespace

void applyLayout(const LayoutProperties &properties, NodeId identity, MountContext &context) {
	if (properties.padding) {
		context.document->bind(
			identity,
			*properties.padding,
			[](Node &node, Insets value) -> detail::Dirty {
				requireNonnegative(value.top);
				requireNonnegative(value.leading);
				requireNonnegative(value.bottom);
				requireNonnegative(value.trailing);
				return detail::assignRetained(
					node.padding,
					value,
					detail::Dirty::measure | detail::Dirty::arrange | detail::Dirty::paint |
						detail::Dirty::snapshot
				);
			}
		);
	}
	if (properties.frame) {
		context.document->bind(
			identity,
			*properties.frame,
			[](Node &node, const Frame &value) -> detail::Dirty {
				requireNonnegative(value.width.value);
				requireNonnegative(value.height.value);
				for (const auto limit :
					{value.minWidth, value.minHeight, value.maxWidth, value.maxHeight}) {
					if (limit) {
						requireNonnegative(*limit);
					}
				}
				return detail::assignRetained(
					node.frame,
					value,
					detail::Dirty::measure | detail::Dirty::arrange | detail::Dirty::paint |
						detail::Dirty::snapshot
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
			[](Node &node, Font value) -> detail::Dirty {
				requireNonnegative(value.size);
				requireFinite(value.weight);
				return detail::assignRetained(
					node.font,
					value,
					detail::Dirty::measure | detail::Dirty::paint
				);
			}
		);
	}
	if (properties.foreground) {
		context.document->bind(
			identity,
			*properties.foreground,
			[](Node &node, Color value) -> detail::Dirty {
				validateColor(value);
				const bool wasExplicit = node.isForegroundExplicit;
				const auto changed =
					detail::assignRetained(node.foreground, value, detail::Dirty::paint);
				node.isForegroundExplicit = true;
				return wasExplicit ? changed : detail::Dirty::paint;
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
			[](Node &node, Color value) -> detail::Dirty {
				validateColor(value);
				return detail::assignRetained(node.background, value, detail::Dirty::paint);
			}
		);
	}
	if (properties.opacity) {
		context.document->bind(
			identity,
			*properties.opacity,
			[](Node &node, double value) -> detail::Dirty {
				requireNonnegative(value);
				if (value > 1) {
					throw std::invalid_argument("Opacity exceeds one");
				}
				return detail::assignRetained(node.opacity, value, detail::Dirty::paint);
			}
		);
	}
	if (properties.borderColor) {
		context.document->bind(
			identity,
			*properties.borderColor,
			[](Node &node, Color value) -> detail::Dirty {
				validateColor(value);
				return detail::assignRetained(node.borderColor, value, detail::Dirty::paint);
			}
		);
	}
	if (properties.borderWidth) {
		context.document->bind(
			identity,
			*properties.borderWidth,
			[](Node &node, double value) -> detail::Dirty {
				requireNonnegative(value);
				return detail::assignRetained(node.borderWidth, value, detail::Dirty::paint);
			}
		);
	}
	if (properties.cornerRadius) {
		context.document->bind(
			identity,
			*properties.cornerRadius,
			[](Node &node, double value) -> detail::Dirty {
				requireNonnegative(value);
				return detail::assignRetained(node.cornerRadius, value, detail::Dirty::paint);
			}
		);
	}
	if (properties.visibility) {
		context.document->bind(
			identity,
			*properties.visibility,
			[](Node &node, Visibility value) -> detail::Dirty {
				if (node.visibility == value) {
					return detail::Dirty::none;
				}
				const bool collapsed =
					node.visibility == Visibility::collapsed || value == Visibility::collapsed;
				node.visibility = value;
				return collapsed ? detail::Dirty::measure | detail::Dirty::arrange |
						detail::Dirty::paint | detail::Dirty::snapshot
								 : detail::Dirty::paint | detail::Dirty::snapshot;
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
		[](Node &node, bool value) -> detail::Dirty {
			return detail::assignRetained(
				node.isEnabled,
				value,
				detail::Dirty::paint | detail::Dirty::snapshot
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
		detail::ReactiveRuntime::current().effects.emplace_back(
			[handle, action = properties.onMount] -> void {
				if (handle.isAlive()) {
					action(handle);
				}
			}
		);
	}
}
} // namespace detail

auto mount(const Empty &, MountContext &context) -> NodeHandle {
	return context.document->handle(
		context.document->createNode(detail::NodeKind::group, context.parent)
	);
}

auto mount(const Text &description, MountContext &context) -> NodeHandle {
	const auto identity = detail::createConfigured(description, detail::NodeKind::text, context);
	context.document->bind(
		identity,
		description.text,
		[](detail::Node &node, const std::string &value) -> detail::Dirty {
			return detail::assignRetained(
				node.text,
				value,
				detail::Dirty::measure | detail::Dirty::paint | detail::Dirty::snapshot
			);
		}
	);
	return context.document->handle(identity);
}

auto mount(const Button &description, MountContext &context) -> NodeHandle {
	const auto identity = detail::createConfigured(description, detail::NodeKind::button, context);
	context.document->bind(
		identity,
		description.text,
		[](detail::Node &node, const std::string &value) -> detail::Dirty {
			return detail::assignRetained(
				node.text,
				value,
				detail::Dirty::measure | detail::Dirty::paint | detail::Dirty::snapshot
			);
		}
	);
	context.document->find(identity)->onClick = description.onClick;
	return context.document->handle(identity);
}

auto mount(const Toggle &description, MountContext &context) -> NodeHandle {
	const auto identity = detail::createConfigured(description, detail::NodeKind::toggle, context);
	context.document->bind(
		identity,
		description.text,
		[](detail::Node &node, const std::string &value) -> detail::Dirty {
			return detail::assignRetained(
				node.text,
				value,
				detail::Dirty::measure | detail::Dirty::paint | detail::Dirty::snapshot
			);
		}
	);
	const auto value = description.value;
	context.document->bind(
		identity,
		Property<bool>{computed([value] -> bool {
			return value.get();
		})},
		[](detail::Node &node, bool checked) -> detail::Dirty {
			return detail::assignRetained(node.checked, checked, detail::Dirty::snapshot);
		}
	);
	context.document->find(identity)->onClick = [value] -> void {
		value.set(!value.get());
	};
	return context.document->handle(identity);
}

auto mount(const Spacer &description, MountContext &context) -> NodeHandle {
	const auto identity = detail::createConfigured(description, detail::NodeKind::spacer, context);
	return context.document->handle(identity);
}

auto mount(const CPUCanvas &description, MountContext &context) -> NodeHandle {
	const auto identity =
		detail::createConfigured(description, detail::NodeKind::cpuCanvas, context);
	context.document->find(identity)->clip = description.appearance.clip.value_or(true);
	context.document->bind(
		identity,
		description.drawing,
		[](detail::Node &node, const Drawing &value) -> detail::Dirty {
			value.validate();
			if (node.drawing == value) {
				return detail::Dirty::none;
			}
			Drawing prepared = value;
			node.drawing.commands.swap(prepared.commands);
			return detail::Dirty::paint;
		}
	);
	return context.document->handle(identity);
}

auto mount(const GPUCanvas &description, MountContext &context) -> NodeHandle {
	const auto identity =
		detail::createConfigured(description, detail::NodeKind::gpuCanvas, context);
	context.document->find(identity)->clip = description.appearance.clip.value_or(true);
	context.document->find(identity)->onGPUError = description.onError;
	context.document->bind(
		identity,
		description.drawing,
		[](detail::Node &node, const GPUDrawing &value) -> detail::Dirty {
			if (node.gpuDrawing && *node.gpuDrawing == value) {
				return detail::Dirty::none;
			}
			node.gpuDrawing = value;
			return detail::Dirty::paint;
		}
	);
	return context.document->handle(identity);
}
} // namespace stana
