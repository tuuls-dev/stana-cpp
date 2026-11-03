module;

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
void applyLayout(const LayoutProperties &properties, NodeId identity, MountContext &context) {
	if (properties.padding) {
		context.document->bind(identity, *properties.padding, [](Node &node, Insets value) -> void {
			requireNonnegative(value.top);
			requireNonnegative(value.leading);
			requireNonnegative(value.bottom);
			requireNonnegative(value.trailing);
			node.padding = value;
		});
	}
	if (properties.frame) {
		context.document->bind(
			identity,
			*properties.frame,
			[](Node &node, const Frame &value) -> void {
				node.frame = value;
			}
		);
	}
}

void applyTextStyle(const TextProperties &properties, NodeId identity, MountContext &context) {
	if (properties.font) {
		context.document->bind(identity, *properties.font, [](Node &node, Font value) -> void {
			requireNonnegative(value.size);
			node.font = value;
		});
	}
	if (properties.foreground) {
		context.document->bind(
			identity,
			*properties.foreground,
			[](Node &node, Color value) -> void {
				node.foreground = value;
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
			[](Node &node, Color value) -> void {
				node.background = value;
			}
		);
	}
	if (properties.opacity) {
		context.document->bind(identity, *properties.opacity, [](Node &node, double value) -> void {
			requireNonnegative(value);
			if (value > 1) {
				throw std::invalid_argument("Opacity exceeds one");
			}
			node.opacity = value;
		});
	}
	if (properties.borderColor) {
		context.document->bind(
			identity,
			*properties.borderColor,
			[](Node &node, Color value) -> void {
				node.borderColor = value;
			}
		);
	}
	if (properties.borderWidth) {
		context.document->bind(
			identity,
			*properties.borderWidth,
			[](Node &node, double value) -> void {
				requireNonnegative(value);
				node.borderWidth = value;
			}
		);
	}
	if (properties.cornerRadius) {
		context.document->bind(
			identity,
			*properties.cornerRadius,
			[](Node &node, double value) -> void {
				requireNonnegative(value);
				node.cornerRadius = value;
			}
		);
	}
	if (properties.visibility) {
		context.document->bind(
			identity,
			*properties.visibility,
			[](Node &node, Visibility value) -> void {
				node.visibility = value;
			}
		);
	}
	context.document->find(identity)->clip = properties.clip;
}

void applyInteraction(
	const InteractionProperties &properties,
	NodeId identity,
	MountContext &context
) {
	context.document->bind(identity, properties.isEnabled, [](Node &node, bool value) -> void {
		node.isEnabled = value;
	});
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
		[](detail::Node &node, const std::string &value) -> void {
			node.text = value;
		}
	);
	return context.document->handle(identity);
}

auto mount(const Button &description, MountContext &context) -> NodeHandle {
	const auto identity = detail::createConfigured(description, detail::NodeKind::button, context);
	context.document->bind(
		identity,
		description.text,
		[](detail::Node &node, const std::string &value) -> void {
			node.text = value;
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
		[](detail::Node &node, const std::string &value) -> void {
			node.text = value;
		}
	);
	const auto value = description.value;
	context.document->bind(
		identity,
		Property<bool>{computed([value] -> bool {
			return value.get();
		})},
		[](detail::Node &node, bool checked) -> void {
			node.checked = checked;
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

auto mount(const Canvas &description, MountContext &context) -> NodeHandle {
	const auto identity = detail::createConfigured(description, detail::NodeKind::canvas, context);
	context.document->find(identity)->drawing = description.drawing;
	return context.document->handle(identity);
}
} // namespace stana
