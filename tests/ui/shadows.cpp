#include <algorithm>
#include <compare>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
import stana;

namespace {
constexpr double hostExtent = 100;
constexpr double smallHostExtent = 50;
constexpr double controlWidth = 60;
constexpr double controlHeight = 30;
constexpr double scalarOverride = 99;
constexpr double horizontalRadius = 20;
constexpr double verticalRadius = 10;
constexpr double paddingWidth = 48;
constexpr double halfOpacity = 0.5;
constexpr double outsideX = 70;
constexpr double outsideY = 15;

void require(bool value, const std::string &message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] auto shadowCommands(const stana::testing::TestHost &host)
	-> std::vector<stana::rendering::PaintCommand> {
	std::vector<stana::rendering::PaintCommand> result;
	for (const auto &command : host.scene()->commands) {
		if (command.kind == stana::rendering::PaintKind::boxShadow) {
			result.push_back(command);
		}
	}
	return result;
}

[[nodiscard]] auto shadow(const stana::rendering::PaintCommand &command)
	-> const stana::rendering::detail::ShadowPaint & {
	if (!command.shadow) {
		throw std::runtime_error("missing shadow payload");
	}
	return *command.shadow;
}

void retained() {
	using namespace stana;
	using Shadows = std::vector<rendering::BoxShadow>;
	const reactive::Signal shadows{Shadows{
		{.offset = {.x = Points{8}, .y = Points{6}}, .blurRadius = Points{12}},
		{.offset = {.x = Points{-2}, .y = Points{1}}, .color = Color::accent(), .isInset = true}
	}};
	const auto computedShadows = shadows.map([](const Shadows &value) -> Shadows {
		return value;
	});
	const reactive::Signal foreground{Color::rgb(1, 0, 0)};
	const reactive::Signal radii{CornerRadii{
		.topLeading = {.width = Points{horizontalRadius}, .height = Points{verticalRadius}}
	}};
	const reactive::Signal widths{BorderWidths{.leading = Points{4}, .trailing = Points{8}}};
	const reactive::Signal opacity{Opacity{1}};
	const reactive::Signal visibility{Visibility::visible};
	int clicks = 0;
	testing::TestHost host{{.width = Points{hostExtent}, .height = Points{hostExtent}}};
	const auto button = ui::Button{
		.text = "shadow",
		.layout =
			{.frame =
					stana::Frame{
						.width = Length::points(Points{controlWidth}),
						.height = Length::points(Points{controlHeight})
					}},
		.textStyle = {.foreground = foreground},
		.appearance =
			{.opacity = opacity,
				.borderWidth = Points{scalarOverride},
				.cornerRadius = Points{scalarOverride},
				.cornerRadii = radii,
				.borderWidths = widths,
				.boxShadows = computedShadows,
				.visibility = visibility,
				.clip = true},
		.events = {ui::on(ui::Event::click, [&] -> void {
			++clicks;
		})}
	};
	host.mount(ui::VStack{.children = ui::children(button)});
	const auto nodes = host.nodes();
	const auto before = host.metrics();
	const auto initial = host.scene();
	const auto commands = shadowCommands(host);
	require(
		commands.size() == 2 && !shadow(commands.front()).isInset &&
			shadow(commands.back()).isInset,
		"outer/inset projection"
	);
	require(shadow(commands.front()).color == Color::rgb(1, 0, 0), "omitted foreground");
	require(
		initial->commands.front().kind == rendering::PaintKind::boxShadow,
		"outer shadow precedes own clip and fill"
	);
	const auto background =
		std::ranges::find_if(initial->commands, [](const auto &command) -> bool {
			return command.kind == rendering::PaintKind::fillRect;
		});
	require(
		background != initial->commands.end() && background->boxGeometry &&
			background->boxGeometry->outer.radii.front() ==
				Size{.width = Points{horizontalRadius}, .height = Points{verticalRadius}} &&
			std::is_eq(background->boxGeometry->padding.bounds.size.width <=> Points{paddingWidth}),
		"rich geometry takes precedence"
	);
	shadows.set(shadows.get());
	require(
		host.metrics().sceneRevisions == before.sceneRevisions,
		"unchanged list reuses projection"
	);
	foreground.set(Color::rgb(0, 1, 0));
	require(
		shadow(shadowCommands(host).front()).color == Color::rgb(0, 1, 0),
		"foreground invalidates shadows"
	);
	opacity.set(Opacity{halfOpacity});
	require(
		shadow(shadowCommands(host).front()).color.alpha <= Opacity{halfOpacity},
		"opacity multiplies shadow alpha"
	);
	const auto current = host.metrics();
	require(
		current.mounts == before.mounts && current.measurements == before.measurements &&
			current.arrangements == before.arrangements &&
			host.nodes().back().id == nodes.back().id &&
			host.nodes().back().bounds == nodes.back().bounds,
		"shadow/foreground/opacity updates do not remount or relayout"
	);
	host.click({.x = Points{outsideX}, .y = Points{outsideY}});
	require(clicks == 0, "overflow shadow does not expand hit area");
	host.setEnvironment({.theme = Theme::dark, .direction = Direction::rightToLeft});
	require(
		shadow(shadowCommands(host).front()).clip.radii.at(1) ==
			Size{.width = Points{horizontalRadius}, .height = Points{verticalRadius}},
		"direction resolves rich corners"
	);
	visibility.set(Visibility::hidden);
	require(shadowCommands(host).empty(), "hidden ancestor/node does not paint shadows");
	visibility.set(Visibility::visible);
	shadows.set({});
	require(
		shadowCommands(host).empty() && host.nodes().back().id == nodes.back().id,
		"empty list removes shadows only"
	);
	require(
		shadow(initial->commands.front()).color == Color::rgb(1, 0, 0),
		"old projection remains immutable"
	);
}

void theme() {
	using namespace stana;
	testing::TestHost host{{.width = Points{smallHostExtent}, .height = Points{smallHostExtent}}};
	host.mount(
		ui::Box{
			.layout =
				{.frame =
						stana::Frame{
							.width = Length::points(Points{horizontalRadius}),
							.height = Length::points(Points{horizontalRadius})
						}},
			.appearance = {.boxShadows = std::vector<rendering::BoxShadow>{{}}},
			.children = ui::children()
		}
	);
	require(
		shadow(shadowCommands(host).front()).color == Color::black(),
		"default light foreground"
	);
	host.setEnvironment({.theme = Theme::dark});
	require(
		shadow(shadowCommands(host).front()).color == Color::white(),
		"default foreground follows theme"
	);
}
} // namespace

auto main() -> int {
	try {
		retained();
		theme();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
