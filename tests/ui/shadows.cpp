#include <algorithm>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
import stana;

namespace {
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
		{.offset = {.x = 8, .y = 6}, .blurRadius = 12},
		{.offset = {.x = -2, .y = 1}, .color = Color::accent(), .isInset = true}
	}};
	const reactive::Signal foreground{Color::rgb(1, 0, 0)};
	const reactive::Signal radii{CornerRadii{.topLeading = {.width = 20, .height = 10}}};
	const reactive::Signal widths{BorderWidths{.leading = 4, .trailing = 8}};
	const reactive::Signal opacity{1.0};
	const reactive::Signal visibility{Visibility::visible};
	int clicks = 0;
	testing::TestHost host{{.width = 100, .height = 100}};
	host.mount(
		ui::Button{
			.text = "shadow",
			.onClick = [&] -> void {
				++clicks;
			},
			.layout = {.frame = {.width = Length::points(60), .height = Length::points(30)}},
			.textStyle = {.foreground = foreground},
			.appearance = {
				.opacity = opacity,
				.borderWidth = 99,
				.cornerRadius = 99,
				.cornerRadii = radii,
				.borderWidths = widths,
				.boxShadows = shadows,
				.visibility = visibility,
				.clip = true
			}
		}
	);
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
			background->boxGeometry->outer.radii.front() == Size{.width = 20, .height = 10} &&
			background->boxGeometry->padding.bounds.size.width == 48,
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
	opacity.set(0.5);
	require(
		shadow(shadowCommands(host).front()).color.alpha <= 0.5,
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
	host.setEnvironment({.theme = Theme::dark, .direction = Direction::rightToLeft});
	require(
		shadow(shadowCommands(host).front()).clip.radii.at(1) == Size{.width = 20, .height = 10},
		"direction resolves rich corners"
	);
	host.click({.x = 70, .y = 15});
	require(clicks == 0, "overflow shadow does not expand hit area");
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
	testing::TestHost host{{.width = 50, .height = 50}};
	host.mount(
		ui::Box{
			.layout = {.frame = {.width = Length::points(20), .height = Length::points(20)}},
			.appearance = {.boxShadows = std::vector<rendering::BoxShadow>{{}}}
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
