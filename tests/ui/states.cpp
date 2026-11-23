#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <ranges>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

import stana;

namespace {
constexpr double side = 80;
constexpr double hostSide = 240;
constexpr double outside = 220;
constexpr double focusWidth = 2;
constexpr double disabledDim = 0.45;
constexpr double authoredOpacity = 0.5;
constexpr double childOpacity = 0.8;

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void requireNear(double actual, double expected, const std::string &message) {
	if (std::abs(actual - expected) > 1e-9) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] auto centerOf(stana::Rect bounds) -> stana::Point {
	constexpr double half = 2;
	return {
		.x = bounds.origin.x + bounds.size.width / half,
		.y = bounds.origin.y + bounds.size.height / half
	};
}

[[nodiscard]] auto fixed(double width, double height) -> stana::Frame {
	return {
		.width = stana::Length::points(stana::Points{width}),
		.height = stana::Length::points(stana::Points{height})
	};
}

[[nodiscard]] auto findText(const stana::testing::TestHost &host, const std::string &text)
	-> stana::document::NodeSnapshot {
	const auto nodes = host.nodes();
	const auto found = std::ranges::find(nodes, text, &stana::document::NodeSnapshot::text);
	if (found == nodes.end()) {
		throw std::runtime_error("Missing text: " + text);
	}
	return *found;
}

[[nodiscard]] auto findKind(const stana::testing::TestHost &host, const std::string &kind)
	-> stana::document::NodeSnapshot {
	const auto nodes = host.nodes();
	const auto found = std::ranges::find(nodes, kind, &stana::document::NodeSnapshot::kind);
	if (found == nodes.end()) {
		throw std::runtime_error("Missing node: " + kind);
	}
	return *found;
}

[[nodiscard]] auto hovered(const stana::testing::TestHost &host, const std::string &text) -> bool {
	return findText(host, text).isHovered;
}

[[nodiscard]] auto commandFor(
	const stana::testing::TestHost &host,
	stana::rendering::PaintKind kind,
	stana::Rect bounds
) -> stana::rendering::PaintCommand {
	const auto scene = host.scene();
	require(static_cast<bool>(scene), "scene published");
	for (const auto &command : scene->commands) {
		if (command.kind == kind && command.bounds == bounds) {
			return command;
		}
	}
	throw std::runtime_error("Missing paint command");
}

[[nodiscard]] auto fillFor(const stana::testing::TestHost &host, stana::Rect bounds)
	-> stana::rendering::PaintCommand {
	return commandFor(host, stana::rendering::PaintKind::fillRect, bounds);
}

[[nodiscard]] auto strokeFor(const stana::testing::TestHost &host, stana::Rect bounds)
	-> stana::rendering::PaintCommand {
	return commandFor(host, stana::rendering::PaintKind::strokeRect, bounds);
}

[[nodiscard]] auto textColor(const stana::testing::TestHost &host, stana::Rect bounds)
	-> stana::theming::ColorSource {
	const auto scene = host.scene();
	require(static_cast<bool>(scene), "scene published");
	for (const auto &command : scene->commands) {
		if (command.kind == stana::rendering::PaintKind::text &&
			bounds.contains(command.bounds.origin)) {
			return command.color;
		}
	}
	throw std::runtime_error("Missing text command");
}

[[nodiscard]] auto alphaOf(const stana::rendering::PaintCommand &command) -> double {
	const auto *color = command.color.asColor();
	require(color != nullptr, "paint color is concrete");
	return color->alpha().value();
}

[[nodiscard]] auto rgb(double red, double green, double blue) -> stana::Color {
	return stana::Color::rgb(red, green, blue);
}

[[nodiscard]] auto source(stana::Color color) -> stana::theming::ColorSource {
	return stana::theming::ColorSource{color};
}

[[nodiscard]] auto accent() -> stana::theming::ColorSource {
	return source(stana::theming::resolveColor(stana::theming::ColorToken::accent(), {}));
}

[[nodiscard]] auto shadowCount(const stana::testing::TestHost &host) -> std::size_t {
	const auto scene = host.scene();
	require(static_cast<bool>(scene), "scene published");
	std::size_t count = 0;
	for (const auto &command : scene->commands) {
		if (command.kind == stana::rendering::PaintKind::boxShadow) {
			++count;
		}
	}
	return count;
}

struct Card {
	stana::ui::StateProperties states{};

	[[nodiscard]] auto render(stana::ui::ComponentContext &) const -> stana::ui::Button {
		return {.text = "Card", .layout = {.frame = fixed(side, side)}};
	}
};

template <typename T> constexpr bool hasStates = requires(T value) { value.states; };

template <typename T> constexpr bool hasVisibility = requires(T value) { value.visibility; };

template <typename T> constexpr bool hasClip = requires(T value) { value.clip; };

template <typename T> constexpr bool hasFont = requires(T value) { value.font; };

template <typename T> constexpr bool hasLayout = requires(T value) { value.layout; };

void stateMembersAreRestricted() {
	static_assert(requires(stana::ui::TextLabel value) {
		value.states.hovered.textStyle.foreground;
	});
	static_assert(requires(stana::ui::Button value) {
		value.states.pressed.appearance.background;
	});
	static_assert(requires(stana::ui::Toggle value) {
		value.states.focused.appearance.borderWidth;
	});
	static_assert(requires(stana::ui::CPUCanvas value) {
		value.states.disabled.appearance.opacity;
	});
	static_assert(requires(stana::ui::GPUCanvas value) {
		value.states.hovered.appearance.cornerRadius;
	});
	static_assert(requires(stana::ui::VStack<std::tuple<stana::ui::Empty>> value) {
		value.states;
	});
	static_assert(requires(stana::ui::HStack<std::tuple<stana::ui::Empty>> value) {
		value.states;
	});
	static_assert(requires(stana::ui::ZStack<std::tuple<stana::ui::Empty>> value) {
		value.states;
	});
	static_assert(requires(stana::ui::Box<std::tuple<stana::ui::Empty>> value) { value.states; });
	static_assert(requires(stana::ui::ScrollView<stana::ui::Empty> value) { value.states; });
	static_assert(requires(stana::symbol::Symbol value) {
		value.states.hovered.appearance.boxShadows;
	});
	static_assert(!hasVisibility<stana::ui::VisualStateAppearance>);
	static_assert(!hasClip<stana::ui::VisualStateAppearance>);
	static_assert(!hasFont<stana::ui::VisualStateText>);
	static_assert(!hasLayout<stana::ui::StateProperties>);
	static_assert(!hasStates<stana::ui::TextInput>);
	static_assert(!hasStates<stana::ui::Spacer>);
	static_assert(!hasStates<stana::ui::Empty>);
}

/// @brief Omitted .states keeps today's controls, including the text input focus stroke.
void omissionStaysCompatible() {
	stana::reactive::Signal<std::string> editorText{std::string{}};
	stana::testing::TestHost host{
		{.width = stana::Points{hostSide}, .height = stana::Points{hostSide}}
	};
	host.mount(
		stana::ui::VStack{
			.children = stana::ui::children(
				stana::ui::Button{.text = "Plain", .layout = {.frame = fixed(side, side)}},
				stana::ui::TextInput{
					.value = editorText.binding(),
					.layout = {.frame = fixed(side, 32)}
				},
				stana::ui::TextLabel{.text = "Read"}
			)
		}
	);
	const auto label = findText(host, "Read");
	const auto rejected = host.handle(label.id).requestFocus();
	require(!rejected && rejected.error() == stana::Error::notFocusable, "labels stay unfocusable");
	const auto button = findText(host, "Plain");
	require(host.handle(button.id).requestFocus().has_value(), "button still focuses");
	require(findText(host, "Plain").isFocused, "focus published");
	const auto stroke = strokeFor(host, findText(host, "Plain").bounds);
	require(stroke.color == accent(), "omitted focus color is accent");
	require(std::is_eq(stroke.lineWidth <=> stana::Points{focusWidth}), "omitted focus width is 2");
	const auto editor = findKind(host, "textInput");
	require(host.handle(editor.id).requestFocus().has_value(), "editor focuses");
	const auto inputStroke = strokeFor(host, findKind(host, "textInput").bounds);
	require(inputStroke.color == accent(), "text input keeps the accent stroke");
	require(
		std::is_eq(inputStroke.lineWidth <=> stana::Points{focusWidth}),
		"text input keeps the 2pt stroke"
	);
}

/// @brief Containers, labels, symbols, canvases, and custom wrappers accept .states.
void statesAttachToListedComponents() {
	constexpr auto icon = stana::symbol::compile<R"(
<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"><path d="M0 0h4v5z"/></svg>
)">();
	require(icon.valid(), "symbol fixture");
	const auto hover = rgb(0.15, 0.25, 0.35);
	const auto card = rgb(0.45, 0.15, 0.55);
	stana::testing::TestHost host{
		{.width = stana::Points{hostSide}, .height = stana::Points{hostSide}}
	};
	host.mount(
		stana::ui::VStack{
			.states = {.hovered = {.appearance = {.background = rgb(0.05, 0.05, 0.05)}}},
			.children = stana::ui::children(
				stana::ui::TextLabel{
					.text = "Label",
					.layout = {.frame = fixed(side, 24)},
					.states = {.hovered = {.textStyle = {.foreground = hover}}}
				},
				stana::ui::CPUCanvas{
					.drawing = stana::rendering::Drawing{},
					.layout = {.frame = fixed(side, 24)},
					.states = {.hovered = {.appearance = {.background = hover}}}
				},
				stana::symbol::Symbol{
					.asset = icon,
					.layout = {.frame = fixed(side, side)},
					.states = {.hovered = {.appearance = {.background = hover}}}
				},
				Card{.states = {.hovered = {.appearance = {.background = card}}}}
			)
		}
	);
	host.pointerMove(centerOf(findText(host, "Label").bounds));
	require(hovered(host, "Label"), "label hover");
	require(textColor(host, findText(host, "Label").bounds) == source(hover), "label foreground");
	require(findKind(host, "vstack").isHovered, "ancestor stack stays hovered");
	host.pointerMove(centerOf(findKind(host, "cpuCanvas").bounds));
	require(findKind(host, "cpuCanvas").isHovered, "canvas hover");
	require(
		fillFor(host, findKind(host, "cpuCanvas").bounds).color == source(hover),
		"canvas fill"
	);
	host.pointerMove(centerOf(findKind(host, "symbol").bounds));
	require(findKind(host, "symbol").isHovered, "symbol hover");
	require(fillFor(host, findKind(host, "symbol").bounds).color == source(hover), "symbol fill");
	host.pointerMove(centerOf(findText(host, "Card").bounds));
	require(hovered(host, "Card"), "custom content hover");
	bool cardFill = false;
	const auto scene = host.scene();
	require(static_cast<bool>(scene), "card scene");
	for (const auto &node : host.nodes()) {
		if (node.kind != "group" || !node.isHovered) {
			continue;
		}
		for (const auto &command : scene->commands) {
			if (command.kind == stana::rendering::PaintKind::fillRect &&
				command.bounds == node.bounds && command.color == source(card)) {
				cardFill = true;
			}
		}
	}
	require(cardFill, "custom wrapper receives states");
}

/// @brief Hover follows the eligible route and ignores an unknown pointer.
void hoverRouteAndUnknownPointer() {
	const auto top = rgb(0, 0.4, 0.8);
	const auto bottom = rgb(0.8, 0.2, 0);
	stana::testing::TestHost host{
		{.width = stana::Points{hostSide}, .height = stana::Points{hostSide}}
	};
	host.mount(
		stana::ui::ZStack{
			.layout = {.frame = fixed(side, side)},
			.children = stana::ui::children(
				stana::ui::Box<std::tuple<stana::ui::TextLabel>>{
					.layout = {.frame = fixed(side, side)},
					.states = {.hovered = {.appearance = {.background = bottom}}},
					.children = stana::ui::children(stana::ui::TextLabel{.text = "Bottom"})
				},
				stana::ui::Box<std::tuple<stana::ui::TextLabel>>{
					.layout = {.frame = fixed(side, side)},
					.states = {.hovered = {.appearance = {.background = top}}},
					.children = stana::ui::children(stana::ui::TextLabel{.text = "Top"})
				}
			)
		}
	);
	require(
		!findText(host, "Top").isHovered && !findText(host, "Bottom").isHovered,
		"no seeded hover"
	);
	const auto point = centerOf(findText(host, "Top").bounds);
	host.pointerMove(point);
	require(hovered(host, "Top") && !hovered(host, "Bottom"), "topmost wins");
	require(findKind(host, "zstack").isHovered, "ancestor remains hovered");
	host.pointerLeave();
	require(!hovered(host, "Top") && !findKind(host, "zstack").isHovered, "leave clears the route");
}

/// @brief Clip, scroll, and later geometry drop a stationary pointer that no longer hits.
void stationaryPointerFollowsGeometry() {
	stana::reactive::Signal spacer{fixed(side, 10)};
	stana::testing::TestHost host{
		{.width = stana::Points{hostSide}, .height = stana::Points{hostSide}}
	};
	host.mount(
		stana::ui::VStack{
			.children = stana::ui::children(
				stana::ui::Spacer{.layout = {.frame = spacer}},
				stana::ui::Button{.text = "Move", .layout = {.frame = fixed(side, 32)}}
			)
		}
	);
	const auto point = centerOf(findText(host, "Move").bounds);
	host.pointerMove(point);
	require(hovered(host, "Move"), "initial hover");
	spacer.set(fixed(side, 160));
	require(!hovered(host, "Move"), "moved control leaves the stationary pointer");

	host.mount(
		stana::ui::ScrollView<stana::ui::VStack<std::tuple<stana::ui::Button, stana::ui::Spacer>>>{
			.layout = {.frame = fixed(side, 40)},
			.content = stana::ui::VStack{
				.children = stana::ui::children(
					stana::ui::Button{.text = "Scroll", .layout = {.frame = fixed(side, 32)}},
					stana::ui::Spacer{.layout = {.frame = fixed(side, 400)}}
				)
			}
		}
	);
	const auto scrolled = centerOf(findText(host, "Scroll").bounds);
	host.pointerMove(scrolled);
	require(hovered(host, "Scroll"), "scroll content hover");
	host.scroll(scrolled, stana::Points{200});
	require(!hovered(host, "Scroll"), "scroll retargets a stationary pointer");

	host.mount(
		stana::ui::Box<std::tuple<stana::ui::Button>>{
			.layout = {.frame = fixed(40, 40)},
			.appearance = {.clip = true},
			.children = stana::ui::children(
				stana::ui::Button{.text = "Clip", .layout = {.frame = fixed(160, 32)}}
			)
		}
	);
	const auto inside = findText(host, "Clip").bounds.origin;
	host.pointerMove({.x = inside.x + stana::Points{8}, .y = inside.y + stana::Points{8}});
	require(hovered(host, "Clip"), "point inside the clip hits");
	host.pointerMove({.x = inside.x + stana::Points{120}, .y = inside.y + stana::Points{8}});
	require(!hovered(host, "Clip"), "clipped overflow is not hovered");
}

/// @brief Unconfirmed presentation geometry does not invent a hover, and confirmation does.
void presentationGatesHover() {
	stana::testing::TestHost host{
		{.width = stana::Points{hostSide}, .height = stana::Points{hostSide}}
	};
	host.enablePresentationTesting();
	host.mount(stana::ui::Button{.text = "Gate", .layout = {.frame = fixed(side, side)}});
	const auto point = centerOf(findText(host, "Gate").bounds);
	host.pointerMove(point);
	require(!hovered(host, "Gate"), "unconfirmed geometry has no hover");
	static_cast<void>(host.render(true));
	require(hovered(host, "Gate"), "confirmed geometry reconciles the stationary pointer");
}

/// @brief A nested hover mutation is reconciled before the route is published.
void hoverReentrancyRetargets() {
	stana::reactive::Signal shown{stana::Visibility::visible};
	stana::testing::TestHost host{
		{.width = stana::Points{hostSide}, .height = stana::Points{hostSide}}
	};
	host.mount(
		stana::ui::ZStack{
			.layout = {.frame = fixed(side, side)},
			.children = stana::ui::children(
				stana::ui::Button{.text = "Under", .layout = {.frame = fixed(side, side)}},
				stana::ui::Button{
					.text = "Cover",
					.layout = {.frame = fixed(side, side)},
					.appearance = {.visibility = shown},
					.events = {stana::ui::on(
						stana::ui::Event::pointerEnter,
						[&](stana::document::PointerEvent &) -> void {
							shown.set(stana::Visibility::hidden);
						}
					)}
				}
			)
		}
	);
	host.pointerMove(centerOf(findText(host, "Under").bounds));
	require(hovered(host, "Under"), "follow-up hover reaches the revealed control");
}

/// @brief Pointer press visuals follow the hit, without changing click or toggle timing.
void pointerPressVisuals() {
	int clicks = 0;
	stana::reactive::Signal value{false};
	const auto pressed = rgb(0.9, 0.1, 0.1);
	const auto base = rgb(0.1, 0.1, 0.9);
	stana::testing::TestHost host{
		{.width = stana::Points{hostSide}, .height = stana::Points{hostSide}}
	};
	host.mount(
		stana::ui::VStack{
			.children = stana::ui::children(
				stana::ui::Button{
					.text = "Press",
					.layout = {.frame = fixed(side, 36)},
					.appearance = {.background = base},
					.states = {.pressed = {.appearance = {.background = pressed}}},
					.events = {stana::ui::on(
						stana::ui::Event::click,
						[&] -> void {
							++clicks;
						}
					)}
				},
				stana::ui::Toggle{
					.text = "Flag",
					.value = value.binding(),
					.layout = {.frame = fixed(side, 36)},
					.states = {.pressed = {.appearance = {.background = pressed}}}
				}
			)
		}
	);
	const auto point = centerOf(findText(host, "Press").bounds);
	const auto paints = host.metrics().paintBuilds;
	const auto layouts = host.metrics().layoutPasses;
	host.pointerDown(point);
	require(
		findText(host, "Press").isPressed && findText(host, "Press").isFocused,
		"press and focus"
	);
	require(fillFor(host, findText(host, "Press").bounds).color == source(pressed), "pressed fill");
	require(host.metrics().layoutPasses == layouts, "press does not layout");
	require(host.metrics().paintBuilds > paints, "press rebuilds paint");
	host.pointerMove({.x = stana::Points{outside}, .y = stana::Points{outside}});
	require(!findText(host, "Press").isPressed, "drag out hides the press");
	host.pointerMove(point);
	require(findText(host, "Press").isPressed, "drag in shows the press");
	host.pointerUp(point);
	require(clicks == 1 && !findText(host, "Press").isPressed, "release inside activates once");

	host.pointerDown(point);
	host.pointerUp({.x = stana::Points{outside}, .y = stana::Points{outside}});
	require(clicks == 1, "release outside does not activate");

	host.pointerDown(point);
	host.cancelPointer();
	require(!findText(host, "Press").isPressed && hovered(host, "Press"), "cancel keeps hover");
	host.pointerUp(point);
	require(clicks == 1, "cancel consumes the press");

	host.mount(
		stana::ui::Button{
			.text = "Blocked",
			.layout = {.frame = fixed(side, 36)},
			.states = {.pressed = {.appearance = {.background = pressed}}},
			.events = {stana::ui::on(
				stana::ui::Event::pointerDown,
				[](stana::document::PointerEvent &event) -> void {
					event.preventDefault();
				}
			)}
		}
	);
	host.pointerDown(centerOf(findText(host, "Blocked").bounds));
	require(
		!findText(host, "Blocked").isPressed && !findText(host, "Blocked").isFocused,
		"prevented press"
	);

	int toggles = 0;
	host.mount(
		stana::ui::Toggle{
			.text = "Flag",
			.value = value.binding(),
			.layout = {.frame = fixed(side, 36)},
			.events = {stana::ui::on(
				stana::ui::Event::click,
				[&](stana::document::ClickEvent &event) -> void {
					++toggles;
					event.preventDefault();
				}
			)}
		}
	);
	const auto before = value.get();
	host.click(centerOf(findText(host, "Flag").bounds));
	require(toggles == 1 && value.get() == before, "prevented click does not flip");
	host.mount(
		stana::ui::Toggle{
			.text = "Flag",
			.value = value.binding(),
			.layout = {.frame = fixed(side, 36)}
		}
	);
	host.click(centerOf(findText(host, "Flag").bounds));
	require(value.get() != before, "toggle still flips on click");
}

/// @brief Removing the hit during press drops that occurrence and retargets ancestors.
void removalDuringPress() {
	stana::reactive::Signal present{true};
	stana::testing::TestHost host{
		{.width = stana::Points{hostSide}, .height = stana::Points{hostSide}}
	};
	host.mount(
		stana::ui::VStack{
			.layout = {.frame = fixed(side, side)},
			.states = {.hovered = {.appearance = {.background = rgb(0.2, 0.2, 0.2)}}},
			.children = stana::ui::children(
				stana::ui::If{
					.condition = present,
					.thenContent = stana::ui::Button{
						.text = "Gone",
						.layout = {.frame = fixed(side, 32)},
						.events = {stana::ui::on(
							stana::ui::Event::pointerDown,
							[&](stana::document::PointerEvent &) -> void {
								present.set(false);
							}
						)}
					}
				}
			)
		}
	);
	const auto id = findText(host, "Gone").id;
	host.pointerDown(centerOf(findText(host, "Gone").bounds));
	host.pointerUp(centerOf(findKind(host, "vstack").bounds));
	require(!host.handle(id).isAlive(), "press target was removed");
	require(findKind(host, "vstack").isHovered, "ancestor remains under the pointer");
}

/// @brief Space and Enter show press without a second activation path.
void keyboardPressVisuals() {
	int clicks = 0;
	const auto pressed = rgb(0.3, 0.8, 0.2);
	stana::testing::TestHost host{
		{.width = stana::Points{hostSide}, .height = stana::Points{hostSide}}
	};
	host.mount(
		stana::ui::VStack{
			.children = stana::ui::children(
				stana::ui::Button{
					.text = "Key",
					.layout = {.frame = fixed(side, 32)},
					.states = {.pressed = {.appearance = {.background = pressed}}},
					.events = {stana::ui::on(
						stana::ui::Event::click,
						[&] -> void {
							++clicks;
						}
					)}
				},
				stana::ui::Button{.text = "Next", .layout = {.frame = fixed(side, 32)}}
			)
		}
	);
	require(host.handle(findText(host, "Key").id).requestFocus().has_value(), "focus key target");
	require(host.activate(findText(host, "Key").id).has_value(), "programmatic activate");
	require(clicks == 1 && !findText(host, "Key").isPressed, "activate does not latch press");
	host.keyDown(stana::document::Key::space);
	require(findText(host, "Key").isPressed && clicks == 2, "space presses and activates");
	require(fillFor(host, findText(host, "Key").bounds).color == source(pressed), "space fill");
	host.keyDown(stana::document::Key::space);
	require(findText(host, "Key").isPressed && clicks == 2, "repeat stays pressed once");
	host.keyDown(stana::document::Key::enter);
	require(clicks == 3 && findText(host, "Key").isPressed, "enter is a separate latch");
	host.keyUp(stana::document::Key::space);
	require(findText(host, "Key").isPressed && clicks == 3, "remaining latch stays up");
	host.keyUp(stana::document::Key::enter);
	require(!findText(host, "Key").isPressed && clicks == 3, "key release does not activate");

	host.keyDown(stana::document::Key::space);
	host.pointerDown(centerOf(findText(host, "Key").bounds));
	host.keyUp(stana::document::Key::space);
	require(findText(host, "Key").isPressed, "pointer press survives the key release");
	host.pointerUp(centerOf(findText(host, "Key").bounds));
	require(!findText(host, "Key").isPressed && clicks == 5, "both sources activated");

	host.keyDown(stana::document::Key::space);
	host.tab();
	require(
		findText(host, "Next").isFocused && !findText(host, "Key").isPressed,
		"focus loss clears the latch"
	);
	host.keyUp(stana::document::Key::space);
	require(clicks == 6, "key up after blur does not activate");

	host.mount(
		stana::ui::Button{
			.text = "Skip",
			.layout = {.frame = fixed(side, 32)},
			.states = {.pressed = {.appearance = {.background = pressed}}},
			.events = {stana::ui::on(
				stana::ui::Event::keyDown,
				[](stana::document::KeyEvent &event) -> void {
					event.preventDefault();
				}
			)}
		}
	);
	require(host.handle(findText(host, "Skip").id).requestFocus().has_value(), "focus skipped");
	host.keyDown(stana::document::Key::space);
	require(!findText(host, "Skip").isPressed, "prevented key does not press");
}

/// @brief Explicit focus fields replace the accent outline and still compose with hover and press.
void focusOutlineComposes() {
	const auto hover = rgb(0.2, 0.6, 0.2);
	const auto pressed = rgb(0.6, 0.2, 0.2);
	const auto border = rgb(0, 0.7, 0.7);
	stana::testing::TestHost host{
		{.width = stana::Points{hostSide}, .height = stana::Points{hostSide}}
	};
	host.mount(
		stana::ui::Button{
			.text = "Focus",
			.layout = {.frame = fixed(side, 36)},
			.states = {
				.hovered = {.appearance = {.background = hover}},
				.pressed = {.appearance = {.background = pressed}},
				.focused = {.appearance = {.borderColor = border, .borderWidth = stana::Points{4}}}
			}
		}
	);
	const auto point = centerOf(findText(host, "Focus").bounds);
	require(host.handle(findText(host, "Focus").id).requestFocus().has_value(), "focus");
	auto stroke = strokeFor(host, findText(host, "Focus").bounds);
	require(stroke.color == source(border), "explicit focus color");
	require(std::is_eq(stroke.lineWidth <=> stana::Points{4}), "explicit focus width");
	host.pointerMove(point);
	require(
		fillFor(host, findText(host, "Focus").bounds).color == source(hover),
		"hover fill while focused"
	);
	stroke = strokeFor(host, findText(host, "Focus").bounds);
	require(stroke.color == source(border), "hover does not remove the outline");
	host.pointerDown(point);
	require(
		fillFor(host, findText(host, "Focus").bounds).color == source(pressed),
		"press fill while focused"
	);
	require(
		strokeFor(host, findText(host, "Focus").bounds).color == source(border),
		"press keeps the outline"
	);
	host.pointerUp(point);

	host.mount(
		stana::ui::Button{
			.text = "Zero",
			.layout = {.frame = fixed(side, 36)},
			.states = {.focused = {.appearance = {.borderWidth = stana::Points{0}}}}
		}
	);
	require(host.handle(findText(host, "Zero").id).requestFocus().has_value(), "focus zero");
	bool stroked = true;
	try {
		static_cast<void>(strokeFor(host, findText(host, "Zero").bounds));
	} catch (const std::runtime_error &) {
		stroked = false;
	}
	require(!stroked, "explicit zero width removes the outline");
}

/// @brief Disabled style is inherited, distinct from inert and hidden, and dims once.
void disabledStyle() {
	stana::reactive::Signal enabled{true};
	stana::reactive::Signal inert{false};
	stana::reactive::Signal shown{stana::Visibility::visible};
	const auto disabled = rgb(0.55, 0.55, 0.1);
	const auto hover = rgb(0.1, 0.55, 0.55);
	stana::testing::TestHost host{{.width = stana::Points{hostSide}, .height = stana::Points{320}}};
	host.mount(
		stana::ui::VStack{
			.appearance = {.background = rgb(1, 1, 1)},
			.states =
				{.disabled =
						{.appearance =
								{.background = disabled,
									.opacity = stana::Opacity{authoredOpacity}}}},
			.interaction = {.isEnabled = enabled},
			.children = stana::ui::children(
				stana::ui::Button{
					.text = "Child",
					.layout = {.frame = fixed(side, 32)},
					.appearance =
						{.background = rgb(1, 0, 0), .opacity = stana::Opacity{childOpacity}},
					.states =
						{.hovered = {.appearance = {.background = hover}},
							.disabled = {.appearance = {.background = disabled}}}
				},
				stana::ui::Button{
					.text = "Inert",
					.layout = {.frame = fixed(side, 32)},
					.appearance = {.background = rgb(0, 0, 1)},
					.states = {.disabled = {.appearance = {.background = disabled}}},
					.interaction = {.inert = inert}
				},
				stana::ui::Button{
					.text = "Hidden",
					.layout = {.frame = fixed(side, 32)},
					.appearance = {.visibility = shown},
					.states = {.disabled = {.appearance = {.background = disabled}}}
				}
			)
		}
	);
	const auto childPoint = centerOf(findText(host, "Child").bounds);
	host.pointerMove(childPoint);
	require(
		hovered(host, "Child") && !findText(host, "Child").isDisabled,
		"enabled child can hover"
	);
	shown.set(stana::Visibility::hidden);
	for (const auto &node : host.nodes()) {
		require(node.text != "Hidden", "hidden is omitted");
		require(!node.isDisabled, "hiding is not the disabled style");
	}
	shown.set(stana::Visibility::visible);
	require(!findText(host, "Hidden").isDisabled, "shown again");
	enabled.set(false);
	require(findText(host, "Child").isDisabled && !hovered(host, "Child"), "inherited disabled");
	require(findKind(host, "vstack").isDisabled, "disabled ancestor");
	require(
		fillFor(host, findText(host, "Child").bounds).color.asColor() != nullptr,
		"child color"
	);
	require(
		fillFor(host, findText(host, "Child").bounds).color ==
			source(disabled.withAlpha(stana::Opacity{authoredOpacity * childOpacity})),
		"disabled fill replaces hover and is not dimmed twice"
	);
	requireNear(
		alphaOf(fillFor(host, findKind(host, "vstack").bounds)),
		authoredOpacity,
		"authored disabled opacity replaces the 0.45 dim"
	);
	enabled.set(true);
	require(!findText(host, "Child").isDisabled, "enabling clears disabled");

	host.mount(
		stana::ui::VStack{
			.layout = {.frame = fixed(200, 120)},
			.appearance = {.background = rgb(1, 1, 1)},
			.interaction = {.isEnabled = false},
			.children = stana::ui::children(
				stana::ui::Button{
					.text = "Dim",
					.layout = {.frame = fixed(side, 32)},
					.appearance = {.background = rgb(0, 1, 0)}
				}
			)
		}
	);
	require(findText(host, "Dim").isDisabled, "descendant is effectively disabled");
	requireNear(alphaOf(fillFor(host, findKind(host, "vstack").bounds)), disabledDim, "local dim");
	requireNear(
		alphaOf(fillFor(host, findText(host, "Dim").bounds)),
		disabledDim,
		"enabled child is not dimmed again"
	);

	inert.set(true);
	host.mount(
		stana::ui::Button{
			.text = "Quiet",
			.layout = {.frame = fixed(side, 32)},
			.appearance = {.background = rgb(0, 0, 1)},
			.states = {.disabled = {.appearance = {.background = disabled}}},
			.interaction = {.inert = true}
		}
	);
	host.pointerMove(centerOf(findText(host, "Quiet").bounds));
	require(
		!findText(host, "Quiet").isDisabled && !findText(host, "Quiet").isHovered,
		"inert is not disabled"
	);
	require(
		fillFor(host, findText(host, "Quiet").bounds).color == source(rgb(0, 0, 1)),
		"inert keeps its fill"
	);
}

/// @brief Fields layer independently, and masked or inactive writes stay off the paint path.
void fieldPrecedenceAndMasking() {
	stana::reactive::Signal base{rgb(0.1, 0, 0)};
	stana::reactive::Signal hoveredColor{rgb(0.2, 0, 0)};
	stana::reactive::Signal pressedColor{rgb(0.3, 0, 0)};
	const auto focusedColor = rgb(0.4, 0, 0);
	const auto disabledColor = rgb(0.5, 0, 0);
	stana::reactive::Signal enabled{true};
	stana::testing::TestHost host{
		{.width = stana::Points{hostSide}, .height = stana::Points{hostSide}}
	};
	host.mount(
		stana::ui::Button{
			.text = "Layer",
			.layout = {.frame = fixed(side, 36)},
			.appearance = {.background = base},
			.states =
				{
					.hovered = {.appearance = {.background = hoveredColor}},
					.pressed = {.appearance = {.background = pressedColor}},
					.focused =
						{.appearance = {.background = focusedColor, .borderColor = rgb(0, 1, 1)}},
					.disabled =
						{.appearance = {.background = disabledColor, .opacity = stana::Opacity{1}}}
				},
			.interaction = {.isEnabled = enabled}
		}
	);
	const auto point = centerOf(findText(host, "Layer").bounds);
	require(fillFor(host, findText(host, "Layer").bounds).color == source(base.get()), "base fill");
	const auto idlePaint = host.metrics().paintBuilds;
	hoveredColor.set(rgb(0.22, 0, 0));
	require(host.metrics().paintBuilds == idlePaint, "inactive hover write skips paint");
	host.pointerMove(point);
	require(
		fillFor(host, findText(host, "Layer").bounds).color == source(hoveredColor.get()),
		"hover wins"
	);
	const auto hoverPaint = host.metrics().paintBuilds;
	const auto hoverLayout = host.metrics().layoutPasses;
	base.set(rgb(0.12, 0, 0));
	require(host.metrics().paintBuilds == hoverPaint, "masked base write skips paint");
	require(host.metrics().layoutPasses == hoverLayout, "state writes do not layout");
	host.pointerDown(point);
	require(
		fillFor(host, findText(host, "Layer").bounds).color == source(focusedColor),
		"focus beats press"
	);
	require(
		strokeFor(host, findText(host, "Layer").bounds).color == source(rgb(0, 1, 1)),
		"focus border composes with the winning fill"
	);
	host.pointerUp({.x = stana::Points{outside}, .y = stana::Points{outside}});
	enabled.set(false);
	require(findText(host, "Layer").isDisabled, "disabled flag");
	require(
		fillFor(host, findText(host, "Layer").bounds).color == source(disabledColor),
		"disabled wins"
	);
	enabled.set(true);
	host.pointerLeave();
	require(
		fillFor(host, findText(host, "Layer").bounds).color == source(base.get()),
		"latest base returns"
	);

	const stana::reactive::Signal shadows{std::vector<stana::rendering::BoxShadow>{
		{.offset = {.x = stana::Points{2}, .y = stana::Points{2}}, .blurRadius = stana::Points{4}}
	}};
	host.mount(
		stana::ui::Button{
			.text = "Shade",
			.layout = {.frame = fixed(side, 36)},
			.appearance = {.boxShadows = shadows},
			.states = {
				.hovered = {
					.appearance = {.boxShadows = std::vector<stana::rendering::BoxShadow>{}}
				}
			}
		}
	);
	require(shadowCount(host) == 1, "base shadow");
	host.pointerMove(centerOf(findText(host, "Shade").bounds));
	require(shadowCount(host) == 0, "empty hover list clears shadows");

	host.mount(
		stana::ui::Button{
			.text = "Rich",
			.layout = {.frame = fixed(side, 36)},
			.appearance =
				{.cornerRadii =
						stana::CornerRadii{
							.topLeading = {.width = stana::Points{20}, .height = stana::Points{10}}
						}},
			.states = {.hovered = {.appearance = {.cornerRadius = stana::Points{3}}}}
		}
	);
	host.pointerMove(centerOf(findText(host, "Rich").bounds));
	const auto fill = fillFor(host, findText(host, "Rich").bounds);
	require(fill.boxGeometry.has_value(), "rich geometry remains");
	require(
		std::is_eq(fill.boxGeometry->outer.radii.at(0).width <=> stana::Points{20}),
		"rich radii beat the hovered scalar"
	);

	const stana::reactive::Signal foreground{
		stana::theming::ColorSource{stana::theming::ColorToken::accent()}
	};
	host.mount(
		stana::ui::TextLabel{
			.text = "Theme",
			.layout = {.frame = fixed(side, 24)},
			.textStyle = {.foreground = rgb(0, 0, 0)},
			.states = {.hovered = {.textStyle = {.foreground = foreground}}}
		}
	);
	host.pointerMove(centerOf(findText(host, "Theme").bounds));
	require(textColor(host, findText(host, "Theme").bounds) == accent(), "token foreground");
	host.setEnvironment({.theme = stana::Theme::dark});
	const auto dark = source(
		stana::theming::resolveColor(
			stana::theming::ColorToken::accent(),
			{.theme = stana::Theme::dark}
		)
	);
	require(
		textColor(host, findText(host, "Theme").bounds) == dark,
		"theme resolves the hovered token"
	);
}

/// @brief Occurrences do not share visual state, and keyed identity survives reorder.
void occurrencesAreIndependent() {
	const auto hover = rgb(0.7, 0.2, 0.4);
	const auto label = stana::ui::TextLabel{
		.text = "Shared",
		.layout = {.frame = fixed(side, 24)},
		.states = {.hovered = {.textStyle = {.foreground = hover}}}
	};
	stana::testing::TestHost host{
		{.width = stana::Points{hostSide}, .height = stana::Points{hostSide}}
	};
	host.mount(stana::ui::VStack{.children = stana::ui::children(label, label)});
	const auto nodes = host.nodes();
	std::vector<stana::document::NodeSnapshot> shared;
	for (const auto &node : nodes) {
		if (node.text == "Shared") {
			shared.push_back(node);
		}
	}
	require(shared.size() == 2 && shared[0].id != shared[1].id, "two occurrences");
	host.pointerMove(centerOf(shared[0].bounds));
	require(findText(host, "Shared").id == shared[0].id, "first occurrence is the hit");
	bool secondHovered = false;
	for (const auto &node : host.nodes()) {
		if (node.id == shared[1].id) {
			secondHovered = node.isHovered;
			require(
				textColor(host, node.bounds) != source(hover),
				"sibling keeps its base foreground"
			);
		}
	}
	require(!secondHovered, "the other mount is not hovered");

	stana::reactive::Signal items{std::vector<int>{1, 2}};
	host.mount(
		stana::ui::VStack{
			.children = stana::ui::children(
				stana::ui::ForEach{
					.items = items,
					.key = [](int value) -> int {
						return value;
					},
					.content = [hover](int value) -> stana::ui::TextLabel {
						return {
							.text = std::to_string(value),
							.layout = {.frame = fixed(side, 28)},
							.states = {.hovered = {.textStyle = {.foreground = hover}}}
						};
					}
				}
			)
		}
	);
	const auto first = findText(host, "1");
	const auto second = findText(host, "2");
	host.pointerMove(centerOf(first.bounds));
	items.set({2, 1});
	require(
		host.handle(first.id).isAlive() && host.handle(second.id).isAlive(),
		"keys keep identity"
	);
	const auto moved = findText(host, "2");
	require(
		moved.id == second.id && moved.isHovered,
		"hover follows the occurrence now under the pointer"
	);
	require(!findText(host, "1").isHovered, "the moved-away row leaves");

	const auto hoveredId = findText(host, "2").id;
	host.mount(stana::ui::Empty{});
	require(!host.handle(hoveredId).isAlive(), "unmount clears the hovered occurrence");
	host.pointerLeave();
	host.mount(stana::ui::TextLabel{.text = "Fresh", .layout = {.frame = fixed(side, 24)}});
	require(!hovered(host, "Fresh"), "a new occurrence does not inherit hover");
}

/// @brief Losing the window drops latched press and hover without activating.
void releaseDropsLatches() {
	int clicks = 0;
	stana::testing::TestHost host{
		{.width = stana::Points{hostSide}, .height = stana::Points{hostSide}}
	};
	host.mount(
		stana::ui::Button{
			.text = "Hold",
			.layout = {.frame = fixed(side, 32)},
			.states = {.pressed = {.appearance = {.background = rgb(1, 0, 0)}}},
			.events = {stana::ui::on(stana::ui::Event::click, [&] -> void {
				++clicks;
			})}
		}
	);
	require(host.handle(findText(host, "Hold").id).requestFocus().has_value(), "focus hold");
	host.pointerMove(centerOf(findText(host, "Hold").bounds));
	host.keyDown(stana::document::Key::space);
	require(findText(host, "Hold").isPressed && hovered(host, "Hold"), "latched");
	const auto clicksBefore = clicks;
	host.releaseInput();
	require(!findText(host, "Hold").isPressed && !hovered(host, "Hold"), "release clears visuals");
	require(clicks == clicksBefore, "release does not activate");
	host.pointerMove(centerOf(findText(host, "Hold").bounds));
	host.pointerDown(centerOf(findText(host, "Hold").bounds));
	host.setHostVisible(false);
	host.flush();
	require(!findText(host, "Hold").isPressed, "hiding clears the pointer press");
}
} // namespace

auto main() -> int {
	try {
		stateMembersAreRestricted();
		omissionStaysCompatible();
		statesAttachToListedComponents();
		hoverRouteAndUnknownPointer();
		stationaryPointerFollowsGeometry();
		presentationGatesHover();
		hoverReentrancyRetargets();
		pointerPressVisuals();
		removalDuringPress();
		keyboardPressVisuals();
		focusOutlineComposes();
		disabledStyle();
		fieldPrecedenceAndMasking();
		occurrencesAreIndependent();
		releaseDropsLatches();
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
