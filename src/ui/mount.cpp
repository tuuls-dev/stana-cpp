module;

#include <cmath>
#include <compare>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

module stana;
import :ui;
import :document;
import :primitives;
import :theming;
import :reactive;

namespace stana::ui {
namespace detail {
namespace {
void validateColor(const Color &color) {
	if (const auto *rgba = color.asRGBA()) {
		for (const double component :
			{rgba->red.value(), rgba->green.value(), rgba->blue.value(), rgba->alpha.value()}) {
			stana::detail::requireNonnegative(component);
			if (component > 1) {
				throw std::invalid_argument("Color exceeds one");
			}
		}
		return;
	}
	const auto *oklch = color.asOKLCH();
	if (oklch == nullptr) {
		throw std::invalid_argument("Color has no retained representation");
	}
	const double lightness = oklch->lightness.value();
	if (!std::isfinite(lightness) || lightness < 0 || lightness > 1) {
		throw std::invalid_argument("OklchLightness out of range");
	}
	const double chroma = oklch->chroma.value();
	if (!std::isfinite(chroma) || chroma < 0) {
		throw std::invalid_argument("OklchChroma out of range");
	}
	if (!std::isfinite(oklch->hue.radians())) {
		throw std::invalid_argument("OKLCH hue must be finite");
	}
	const double alpha = oklch->alpha.value();
	if (!std::isfinite(alpha) || alpha < 0 || alpha > 1) {
		throw std::invalid_argument("Opacity out of range");
	}
}

void validateColorSource(const theming::ColorSource &source) {
	if (const auto *color = source.asColor()) {
		validateColor(*color);
	}
}

void validateFrameLength(Length length) {
	using enum Length::Kind;
	switch (length.kind) {
	case points:
		stana::detail::requireNonnegative(length.absolute);
		return;
	case percent:
		stana::detail::requireNonnegative(length.relative);
		return;
	case automatic:
	case fill:
		return;
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
				validateFrameLength(value.width);
				validateFrameLength(value.height);
				for (const auto limit :
					{value.minWidth, value.minHeight, value.maxWidth, value.maxHeight}) {
					if (limit) {
						stana::detail::requireNonnegative(*limit);
					}
				}
				if ((value.minWidth && value.maxWidth && *value.minWidth > *value.maxWidth) ||
					(value.minHeight && value.maxHeight && *value.minHeight > *value.maxHeight)) {
					throw std::invalid_argument("Contradictory frame constraints");
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

namespace {
template <typename T, typename Validate>
void bindTypography(
	const std::optional<stana::reactive::Property<T>> &property,
	std::optional<T> stana::document::detail::TypographyOverrides::*member,
	NodeId identity,
	MountContext &context,
	bool placeholder,
	Validate validate
) {
	if (!property) {
		return;
	}
	context.document->bind(
		identity,
		*property,
		[member, placeholder, validate](stana::document::detail::Node &node, const T &value)
			-> stana::document::detail::Dirty {
			validate(value);
			auto &style = placeholder ? node.editor->placeholderStyle : node.textOverrides;
			return assignRetained(
				style.*member,
				std::optional<T>{value},
				stana::document::detail::Dirty::measure | stana::document::detail::Dirty::paint
			);
		}
	);
}

void applyTypography(
	const TextProperties &properties,
	NodeId identity,
	MountContext &context,
	bool placeholder
) {
	using Overrides = stana::document::detail::TypographyOverrides;
	bindTypography(
		properties.fontFamily,
		&Overrides::fontFamily,
		identity,
		context,
		placeholder,
		[](const std::string &value) -> void {
			if (!stana::text::isValidUtf8(value)) {
				throw std::invalid_argument("Invalid UTF-8 font family");
			}
		}
	);
	bindTypography(
		properties.fontSize,
		&Overrides::fontSize,
		identity,
		context,
		placeholder,
		[](Points value) -> void {
			stana::detail::requireNonnegative(value);
		}
	);
	bindTypography(
		properties.fontWeight,
		&Overrides::fontWeight,
		identity,
		context,
		placeholder,
		[](FontWeight value) -> void {
			stana::detail::requireFinite(value.value());
		}
	);
	bindTypography(
		properties.fontStyle,
		&Overrides::fontStyle,
		identity,
		context,
		placeholder,
		[](FontStyle) -> void {}
	);
	bindTypography(
		properties.lineHeight,
		&Overrides::lineHeight,
		identity,
		context,
		placeholder,
		[](Points value) -> void {
			stana::detail::requireNonnegative(value);
			if (value.value() <= 0) {
				throw std::invalid_argument("Explicit line height must be positive");
			}
		}
	);
	if (placeholder) {
		bindTypography(
			properties.isStrikethrough,
			&Overrides::isStrikethrough,
			identity,
			context,
			true,
			[](bool) -> void {}
		);
		bindTypography(
			properties.font,
			&Overrides::font,
			identity,
			context,
			true,
			[](const Font &value) -> void {
				stana::detail::requireNonnegative(value.size);
				if (value.family && !stana::text::isValidUtf8(*value.family)) {
					throw std::invalid_argument("Invalid UTF-8 font family");
				}
				stana::detail::requireFinite(value.weight.value());
				if (value.family && !stana::text::isValidUtf8(*value.family)) {
					throw std::invalid_argument("Invalid UTF-8 font family");
				}
			}
		);
		bindTypography(
			properties.foreground,
			&Overrides::foreground,
			identity,
			context,
			true,
			validateColorSource
		);
	}
}
} // namespace

void applyTextStyle(const TextProperties &properties, NodeId identity, MountContext &context) {
	applyTypography(properties, identity, context, false);
	if (properties.wrap) {
		context.document->bind(
			identity,
			*properties.wrap,
			[](stana::document::detail::Node &node,
				stana::text::Wrap value) -> stana::document::detail::Dirty {
				return assignRetained(
					node.wrap,
					value,
					stana::document::detail::Dirty::measure | stana::document::detail::Dirty::paint
				);
			}
		);
	}
	if (properties.alignment) {
		context.document->bind(
			identity,
			*properties.alignment,
			[](stana::document::detail::Node &node,
				Alignment value) -> stana::document::detail::Dirty {
				return assignRetained(
					node.textAlignment,
					value,
					stana::document::detail::Dirty::measure | stana::document::detail::Dirty::paint
				);
			}
		);
	}
	if (properties.maxLines) {
		context.document->bind(
			identity,
			*properties.maxLines,
			[](stana::document::detail::Node &node,
				std::size_t value) -> stana::document::detail::Dirty {
				if (value == 0) {
					throw std::invalid_argument("Maximum lines must be positive");
				}
				return assignRetained(
					node.maxLines,
					std::optional{value},
					stana::document::detail::Dirty::measure | stana::document::detail::Dirty::paint
				);
			}
		);
	}
	if (properties.isStrikethrough) {
		context.document->bind(
			identity,
			*properties.isStrikethrough,
			[](stana::document::detail::Node &node, bool value) -> stana::document::detail::Dirty {
				return assignRetained(
					node.isStrikethrough,
					value,
					stana::document::detail::Dirty::paint
				);
			}
		);
	}
	if (properties.font) {
		context.document->bind(
			identity,
			*properties.font,
			[](stana::document::detail::Node &node, Font value) -> stana::document::detail::Dirty {
				stana::detail::requireNonnegative(value.size);
				if (value.family && !stana::text::isValidUtf8(*value.family)) {
					throw std::invalid_argument("Invalid UTF-8 font family");
				}
				stana::detail::requireFinite(value.weight.value());
				return stana::ui::detail::assignRetained(
					node.font,
					value,
					stana::document::detail::Dirty::measure | stana::document::detail::Dirty::paint
				);
			}
		);
	}
	if (properties.foreground) {
		auto *document = context.document.get();
		context.document->bind(
			identity,
			*properties.foreground,
			[document](stana::document::detail::Node &node, theming::ColorSource value)
				-> stana::document::detail::Dirty {
				validateColorSource(value);
				const bool wasExplicit = node.isForegroundExplicit;
				const auto changed = stana::ui::detail::assignRetained(
					node.foreground,
					value,
					stana::document::detail::Dirty::paint
				);
				node.isForegroundExplicit = true;
				if (wasExplicit && changed == stana::document::detail::Dirty::none) {
					return changed;
				}
				return document->paintDomainsForBase(
					node,
					stana::document::detail::VisualField::foreground
				);
			}
		);
	}
}

namespace {
template <typename T>
[[nodiscard]] auto commitBase(
	stana::document::detail::Document *document,
	stana::document::detail::Node &node,
	T &target,
	const T &value,
	stana::document::detail::VisualField field
) -> stana::document::detail::Dirty {
	const auto changed = assignRetained(target, value, stana::document::detail::Dirty::paint);
	if (changed == stana::document::detail::Dirty::none) {
		return changed;
	}
	return document->paintDomainsForBase(node, field);
}

template <typename T>
[[nodiscard]] auto commitOverride(
	stana::document::detail::Document *document,
	stana::document::detail::Node &node,
	std::optional<T> &target,
	const T &value,
	stana::document::detail::VisualLayer layer,
	stana::document::detail::VisualField field
) -> stana::document::detail::Dirty {
	const auto changed =
		assignRetained(target, std::optional<T>{value}, stana::document::detail::Dirty::paint);
	if (changed == stana::document::detail::Dirty::none) {
		return changed;
	}
	return document->paintDomainsForOverride(node, layer, field);
}

[[nodiscard]] auto
stateSlot(stana::document::detail::Node &node, stana::document::detail::VisualLayer layer)
	-> stana::document::detail::VisualStateSlot & {
	using enum stana::document::detail::VisualLayer;
	switch (layer) {
	case hovered:
		return node.hoveredStyle;
	case pressed:
		return node.pressedStyle;
	case focused:
		return node.focusedStyle;
	case disabled:
		return node.disabledStyle;
	}
	throw std::logic_error("Unknown visual state");
}
} // namespace

void applyAppearance(
	const AppearanceProperties &properties,
	NodeId identity,
	MountContext &context
) {
	auto *document = context.document.get();
	if (properties.background) {
		context.document->bind(
			identity,
			*properties.background,
			[document](stana::document::detail::Node &node, theming::ColorSource value)
				-> stana::document::detail::Dirty {
				validateColorSource(value);
				return commitBase(
					document,
					node,
					node.background,
					value,
					stana::document::detail::VisualField::background
				);
			}
		);
	}
	if (properties.opacity) {
		context.document->bind(
			identity,
			*properties.opacity,
			[document](stana::document::detail::Node &node, Opacity value)
				-> stana::document::detail::Dirty {
				stana::detail::requireNonnegative(value);
				if (value.value() > 1) {
					throw std::invalid_argument("Opacity exceeds one");
				}
				return commitBase(
					document,
					node,
					node.opacity,
					value,
					stana::document::detail::VisualField::opacity
				);
			}
		);
	}
	if (properties.borderColor) {
		context.document->bind(
			identity,
			*properties.borderColor,
			[document](stana::document::detail::Node &node, theming::ColorSource value)
				-> stana::document::detail::Dirty {
				validateColorSource(value);
				return commitBase(
					document,
					node,
					node.borderColor,
					value,
					stana::document::detail::VisualField::borderColor
				);
			}
		);
	}
	if (properties.borderWidth) {
		context.document->bind(
			identity,
			*properties.borderWidth,
			[document](stana::document::detail::Node &node, Points value)
				-> stana::document::detail::Dirty {
				stana::detail::requireNonnegative(value);
				return commitBase(
					document,
					node,
					node.borderWidth,
					value,
					stana::document::detail::VisualField::borderWidth
				);
			}
		);
	}
	if (properties.cornerRadius) {
		context.document->bind(
			identity,
			*properties.cornerRadius,
			[document](stana::document::detail::Node &node, Points value)
				-> stana::document::detail::Dirty {
				stana::detail::requireNonnegative(value);
				return commitBase(
					document,
					node,
					node.cornerRadius,
					value,
					stana::document::detail::VisualField::cornerRadius
				);
			}
		);
	}
	if (properties.cornerRadii) {
		context.document->bind(
			identity,
			*properties.cornerRadii,
			[document](stana::document::detail::Node &node, CornerRadii value)
				-> stana::document::detail::Dirty {
				stana::rendering::detail::validateCornerRadii(value);
				const auto changed = assignRetained(
					node.cornerRadii,
					std::optional{value},
					stana::document::detail::Dirty::paint
				);
				if (changed == stana::document::detail::Dirty::none) {
					return changed;
				}
				return document->paintDomainsForBase(
					node,
					stana::document::detail::VisualField::cornerRadii
				);
			}
		);
	}
	if (properties.borderWidths) {
		context.document->bind(
			identity,
			*properties.borderWidths,
			[document](stana::document::detail::Node &node, BorderWidths value)
				-> stana::document::detail::Dirty {
				stana::rendering::detail::validateBorderWidths(value);
				const auto changed = assignRetained(
					node.borderWidths,
					std::optional{value},
					stana::document::detail::Dirty::paint
				);
				if (changed == stana::document::detail::Dirty::none) {
					return changed;
				}
				return document->paintDomainsForBase(
					node,
					stana::document::detail::VisualField::borderWidths
				);
			}
		);
	}
	if (properties.boxShadows) {
		context.document->bind(
			identity,
			*properties.boxShadows,
			[document](
				stana::document::detail::Node &node,
				std::vector<stana::rendering::BoxShadow> value
			) -> stana::document::detail::Dirty {
				stana::rendering::detail::validateBoxShadows(value);
				if (node.boxShadows == value) {
					return stana::document::detail::Dirty::none;
				}
				node.boxShadows = std::move(value);
				return document->paintDomainsForBase(
					node,
					stana::document::detail::VisualField::boxShadows
				);
			}
		);
	}
	if (properties.visibility) {
		context.document->bind(
			identity,
			*properties.visibility,
			[document](stana::document::detail::Node &node, Visibility value)
				-> stana::document::detail::Dirty {
				if (node.visibility == value) {
					return stana::document::detail::Dirty::none;
				}
				const bool collapsed =
					node.visibility == Visibility::collapsed || value == Visibility::collapsed;
				node.visibility = value;
				document->notePointerReconcile();
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

void applyStates(const StateProperties &states, NodeId identity, MountContext &context) {
	auto *document = context.document.get();
	const auto bindLayer = [&](const VisualStateProperties &visual,
							   stana::document::detail::VisualLayer layer) -> void {
		const auto &appearance = visual.appearance;
		if (appearance.background) {
			context.document->bind(
				identity,
				*appearance.background,
				[document, layer](stana::document::detail::Node &node, theming::ColorSource value)
					-> stana::document::detail::Dirty {
					validateColorSource(value);
					return commitOverride(
						document,
						node,
						stateSlot(node, layer).background,
						value,
						layer,
						stana::document::detail::VisualField::background
					);
				}
			);
		}
		if (appearance.opacity) {
			context.document->bind(
				identity,
				*appearance.opacity,
				[document, layer](stana::document::detail::Node &node, Opacity value)
					-> stana::document::detail::Dirty {
					stana::detail::requireNonnegative(value);
					if (value.value() > 1) {
						throw std::invalid_argument("Opacity exceeds one");
					}
					return commitOverride(
						document,
						node,
						stateSlot(node, layer).opacity,
						value,
						layer,
						stana::document::detail::VisualField::opacity
					);
				}
			);
		}
		if (appearance.borderColor) {
			context.document->bind(
				identity,
				*appearance.borderColor,
				[document, layer](stana::document::detail::Node &node, theming::ColorSource value)
					-> stana::document::detail::Dirty {
					validateColorSource(value);
					return commitOverride(
						document,
						node,
						stateSlot(node, layer).borderColor,
						value,
						layer,
						stana::document::detail::VisualField::borderColor
					);
				}
			);
		}
		if (appearance.borderWidth) {
			context.document->bind(
				identity,
				*appearance.borderWidth,
				[document, layer](stana::document::detail::Node &node, Points value)
					-> stana::document::detail::Dirty {
					stana::detail::requireNonnegative(value);
					return commitOverride(
						document,
						node,
						stateSlot(node, layer).borderWidth,
						value,
						layer,
						stana::document::detail::VisualField::borderWidth
					);
				}
			);
		}
		if (appearance.cornerRadius) {
			context.document->bind(
				identity,
				*appearance.cornerRadius,
				[document, layer](stana::document::detail::Node &node, Points value)
					-> stana::document::detail::Dirty {
					stana::detail::requireNonnegative(value);
					return commitOverride(
						document,
						node,
						stateSlot(node, layer).cornerRadius,
						value,
						layer,
						stana::document::detail::VisualField::cornerRadius
					);
				}
			);
		}
		if (appearance.cornerRadii) {
			context.document->bind(
				identity,
				*appearance.cornerRadii,
				[document, layer](stana::document::detail::Node &node, CornerRadii value)
					-> stana::document::detail::Dirty {
					stana::rendering::detail::validateCornerRadii(value);
					return commitOverride(
						document,
						node,
						stateSlot(node, layer).cornerRadii,
						value,
						layer,
						stana::document::detail::VisualField::cornerRadii
					);
				}
			);
		}
		if (appearance.borderWidths) {
			context.document->bind(
				identity,
				*appearance.borderWidths,
				[document, layer](stana::document::detail::Node &node, BorderWidths value)
					-> stana::document::detail::Dirty {
					stana::rendering::detail::validateBorderWidths(value);
					return commitOverride(
						document,
						node,
						stateSlot(node, layer).borderWidths,
						value,
						layer,
						stana::document::detail::VisualField::borderWidths
					);
				}
			);
		}
		if (appearance.boxShadows) {
			context.document->bind(
				identity,
				*appearance.boxShadows,
				[document, layer](
					stana::document::detail::Node &node,
					std::vector<stana::rendering::BoxShadow> value
				) -> stana::document::detail::Dirty {
					stana::rendering::detail::validateBoxShadows(value);
					auto &slot = stateSlot(node, layer).boxShadows;
					if (slot && *slot == value) {
						return stana::document::detail::Dirty::none;
					}
					slot = std::move(value);
					return document->paintDomainsForOverride(
						node,
						layer,
						stana::document::detail::VisualField::boxShadows
					);
				}
			);
		}
		if (visual.textStyle.foreground) {
			context.document->bind(
				identity,
				*visual.textStyle.foreground,
				[document, layer](stana::document::detail::Node &node, theming::ColorSource value)
					-> stana::document::detail::Dirty {
					validateColorSource(value);
					return commitOverride(
						document,
						node,
						stateSlot(node, layer).foreground,
						value,
						layer,
						stana::document::detail::VisualField::foreground
					);
				}
			);
		}
	};
	using enum stana::document::detail::VisualLayer;
	bindLayer(states.hovered, hovered);
	bindLayer(states.pressed, pressed);
	bindLayer(states.focused, focused);
	bindLayer(states.disabled, disabled);
}

void applyInteraction(
	const InteractionProperties &properties,
	NodeId identity,
	MountContext &context
) {
	auto *document = context.document.get();
	context.document->bind(
		identity,
		properties.isEnabled,
		[document](stana::document::detail::Node &node, bool value)
			-> stana::document::detail::Dirty {
			const auto changed = stana::ui::detail::assignRetained(
				node.isEnabled,
				value,
				stana::document::detail::Dirty::paint | stana::document::detail::Dirty::snapshot
			);
			if (changed == stana::document::detail::Dirty::none) {
				return changed;
			}
			document->invalidateDescendants(
				node.id,
				stana::document::detail::Dirty::paint | stana::document::detail::Dirty::snapshot
			);
			document->notePointerReconcile();
			return changed;
		}
	);
	context.document->bind(
		identity,
		properties.inert,
		[document](stana::document::detail::Node &node, bool value)
			-> stana::document::detail::Dirty {
			const auto changed = assignRetained(
				node.inert,
				value,
				stana::document::detail::Dirty::paint | stana::document::detail::Dirty::snapshot
			);
			if (changed == stana::document::detail::Dirty::none) {
				return changed;
			}
			document->invalidateDescendants(
				node.id,
				stana::document::detail::Dirty::paint | stana::document::detail::Dirty::snapshot
			);
			document->notePointerReconcile();
			return changed;
		}
	);
}

void applyEvents(const Events &events, NodeId identity, MountContext &context) {
	context.document->find(identity)->events = events.registrations;
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

auto mount(const TextLabel &description, MountContext &context) -> stana::document::NodeHandle {
	const auto identity = stana::ui::detail::createConfigured(
		description,
		stana::document::detail::NodeKind::textLabel,
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

auto mount(const TextInput &description, MountContext &context) -> stana::document::NodeHandle {
	// Validate the binding before registering an occurrence or scheduling mount effects.
	auto editor = std::make_unique<stana::document::detail::EditorState>(description.value);
	const auto identity =
		context.document->createNode(stana::document::detail::NodeKind::textInput, context.parent);
	context.document->find(identity)->editor = std::move(editor);
	detail::configure(description, identity, context);
	{
		auto *node = context.document->find(identity);
		node->text = node->editor->buffer.text();
		node->editor->onSubmit = description.onSubmit;
		node->editor->onCancel = description.onCancel;
	}
	detail::applyTypography(description.placeholderStyle, identity, context, true);
	context.document->bind(
		identity,
		description.placeholder,
		[](stana::document::detail::Node &node,
			const std::string &value) -> stana::document::detail::Dirty {
			if (!stana::text::isSingleLine(value)) {
				throw std::invalid_argument("Invalid placeholder");
			}
			return detail::assignRetained(
				node.editor->placeholder,
				value,
				stana::document::detail::Dirty::measure | stana::document::detail::Dirty::paint
			);
		}
	);
	const auto binding = description.value;
	const auto weak = std::weak_ptr{context.document};
	context.document->bind(
		identity,
		stana::reactive::Property<std::string>{stana::reactive::computed([binding] -> std::string {
			return binding.get();
		})},
		[weak, identity](stana::document::detail::Node &node, const std::string &value)
			-> stana::document::detail::Dirty {
			if (value == node.editor->buffer.text()) {
				return stana::document::detail::Dirty::none;
			}
			if (const auto changed = node.editor->buffer.setText(value); !changed) {
				if (const auto document = weak.lock()) {
					document->report(changed.error());
				}
				return stana::document::detail::Dirty::none;
			}
			node.text = value;
			node.editor->scroll = Points{};
			node.editor->typingGroup = false;
			node.editor->deletionGroup.reset();
			if (const auto document = weak.lock()) {
				document->externalEditorChanged(identity);
			}
			return stana::document::detail::Dirty::measure | stana::document::detail::Dirty::paint |
				stana::document::detail::Dirty::snapshot;
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
	context.document->find(identity)->defaultActivate = [value] -> void {
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

namespace stana::symbol {
namespace {
[[nodiscard]] auto
sameOptional(const std::optional<Points> &left, const std::optional<Points> &right) -> bool {
	if (left.has_value() != right.has_value()) {
		return false;
	}
	return !left || std::is_eq(*left <=> *right);
}
} // namespace

auto mount(const Symbol &description, stana::ui::MountContext &context)
	-> stana::document::NodeHandle {
	if (!description.asset.get().valid()) {
		throw std::invalid_argument("Stana: cannot mount an empty symbol asset");
	}
	const auto identity = stana::ui::detail::createConfigured(
		description,
		stana::document::detail::NodeKind::symbol,
		context
	);
	auto *node = context.document->find(identity);
	node->clip = description.appearance.clip.value_or(false);
	node->symbol = stana::document::detail::SymbolState{};
	node->symbol->onError = description.onError;
	context.document->bind(
		identity,
		description.asset,
		[](stana::document::detail::Node &record,
			const Asset &value) -> stana::document::detail::Dirty {
			if (!value.valid()) {
				throw std::invalid_argument("Stana: cannot mount an empty symbol asset");
			}
			if (!record.symbol || record.symbol->asset == value) {
				return stana::document::detail::Dirty::none;
			}
			record.symbol->asset = value;
			return stana::document::detail::Dirty::paint;
		}
	);
	context.document->bind(
		identity,
		description.size,
		[](stana::document::detail::Node &record, Points value) -> stana::document::detail::Dirty {
			stana::detail::requireNonnegative(value);
			if (!record.symbol || std::is_eq(record.symbol->size <=> value)) {
				return stana::document::detail::Dirty::none;
			}
			record.symbol->size = value;
			return stana::document::detail::Dirty::measure |
				stana::document::detail::Dirty::arrange | stana::document::detail::Dirty::paint |
				stana::document::detail::Dirty::snapshot;
		}
	);
	context.document->bind(
		identity,
		description.color,
		[](stana::document::detail::Node &record,
			const theming::ColorSource &value) -> stana::document::detail::Dirty {
			if (const auto *color = value.asColor()) {
				if (color->asRGBA() == nullptr && color->asOKLCH() == nullptr) {
					throw std::invalid_argument("Color has no retained representation");
				}
			}
			if (!record.symbol || record.symbol->color == value) {
				return stana::document::detail::Dirty::none;
			}
			record.symbol->color = value;
			return stana::document::detail::Dirty::paint;
		}
	);
	context.document->bind(
		identity,
		description.strokeWidth,
		[](stana::document::detail::Node &record,
			const std::optional<Points> &value) -> stana::document::detail::Dirty {
			if (value) {
				stana::detail::requireNonnegative(*value);
			}
			if (!record.symbol || sameOptional(record.symbol->strokeWidth, value)) {
				return stana::document::detail::Dirty::none;
			}
			record.symbol->strokeWidth = value;
			return stana::document::detail::Dirty::paint;
		}
	);
	context.document->bind(
		identity,
		description.animation,
		[](stana::document::detail::Node &record,
			const Playback &value) -> stana::document::detail::Dirty {
			if (!record.symbol || record.symbol->playback == value) {
				return stana::document::detail::Dirty::none;
			}
			record.symbol->playback = value;
			return stana::document::detail::Dirty::paint;
		}
	);
	const auto weak = context.document->weak_from_this();
	node->symbol->frameRegistration = context.document->addFrameRegistration(
		[weak, identity]() -> bool {
			const auto document = weak.lock();
			const auto *live = document ? document->find(identity) : nullptr;
			return live != nullptr && live->scope.alive() && live->symbol &&
				live->symbol->playbackState.needsFrame();
		},
		[weak, identity](stana::document::detail::Document::FrameStamp, FrameDuration elapsed)
			-> void {
			const auto document = weak.lock();
			auto *live = document ? document->find(identity) : nullptr;
			if (live == nullptr || !live->symbol) {
				return;
			}
			const auto before = live->symbol->effect;
			live->symbol->playbackState.advance(elapsed);
			live->symbol->effect = live->symbol->playbackState.sample();
			if (live->symbol->effect != before) {
				document->invalidate(identity, stana::document::detail::Dirty::paint);
			}
			document->refreshFrameDemand();
		}
	);
	static_cast<void>(node->scope.own([weak, identity]() -> void {
		const auto document = weak.lock();
		if (!document) {
			return;
		}
		if (const auto *live = document->find(identity); live != nullptr && live->symbol) {
			document->cancelFrameRegistration(live->symbol->frameRegistration);
		}
	}));
	return context.document->handle(identity);
}
} // namespace stana::symbol
