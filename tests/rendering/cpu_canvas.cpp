#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
import stana;

namespace {
constexpr double smallExtent = 8;
constexpr double extent = 10;
constexpr double doubleExtent = 20;
constexpr double height = 30;
constexpr double width = 40;
constexpr double trailing = 7;

void require(bool value, const std::string &message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

template <typename F> void rejects(F operation) {
	bool rejected = false;
	try {
		operation();
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	require(rejected, "invalid CPU drawing accepted");
}

void values() {

	stana::rendering::Drawing drawing;
	const stana::Rect rect{.size = {.width = doubleExtent, .height = extent}};
	drawing.fillRect(rect, stana::Color::accent(), doubleExtent);
	require(
		std::is_eq(drawing.commands.front().cornerRadius <=> doubleExtent),
		"authored radius preserved for structural value equality"
	);
	stana::rendering::Drawing copy = drawing;
	copy.commands.front().text = "ignored";
	copy.commands.front().font.size = height;
	require(copy == drawing, "equality compares only opcode payload");
	rejects([&] -> void {
		drawing.strokeRect(rect, stana::Color::black(), -1);
	});
	require(drawing == copy, "failed append is atomic");
	rejects([&] -> void {
		drawing.fillRect(
			{.origin = {.x = std::numeric_limits<double>::max()},
				.size = {.width = std::numeric_limits<double>::max()}},
			stana::Color::black()
		);
	});
	stana::rendering::Drawing line;
	line.commands.push_back(
		{.kind = stana::rendering::PaintKind::line,
			.bounds = {
				.origin = {.x = doubleExtent, .y = doubleExtent},
				.size = {.width = -extent, .height = -extent}
			}}
	);
	line.validate();
	line.commands.push_back({.kind = stana::rendering::PaintKind::popClip});
	rejects([&] -> void {
		line.validate();
	});
	line.commands.back().kind = stana::rendering::PaintKind::pushClip;
	rejects([&] -> void {
		line.validate();
	});
}

void retained() {

	stana::rendering::Drawing initial;
	initial.fillRect(
		{.size = {.width = smallExtent, .height = smallExtent}},
		stana::Color::border()
	);
	const stana::reactive::Signal drawing{initial};
	const stana::reactive::Signal present{true};
	unsigned presses = 0;
	stana::testing::TestHost host;
	host.mount(
		stana::ui::If{
			.condition = present,
			.thenContent = stana::ui::CPUCanvas{
				.drawing = drawing,
				.layout =
					{.padding = stana::Insets{.top = 2, .leading = 3, .trailing = trailing},
						.frame =
							stana::Frame{
								.width = stana::Length::points(width),
								.height = stana::Length::points(height)
							}},
				.interaction = {.onPointerDown = [&](stana::document::PointerEvent &) -> void {
					++presses;
				}},
				.accessibility = {.label = "chart"}
			}
		}
	);
	const auto before = host.metrics();
	const auto old = host.scene();
	const auto identity = host.nodes().back().id;
	require(
		old->commands.front().kind == stana::rendering::PaintKind::pushClip &&
			old->commands.back().kind == stana::rendering::PaintKind::popClip,
		"default clipping"
	);
	require(old->commands.at(1).bounds.origin == stana::Point{.x = 3, .y = 2}, "padded origin");
	initial.fillRect({.size = {.width = 1, .height = 1}}, stana::Color::black());
	require(host.scene() == old, "builder independent from mounted copy");
	drawing.set(drawing.get());
	require(
		host.metrics().sceneRevisions == before.sceneRevisions,
		"equal assignment no publication"
	);
	stana::rendering::Drawing next;
	next.fillRect({.size = {.width = extent, .height = extent}}, stana::Color::text());
	drawing.set(next);
	require(
		host.metrics().mounts == before.mounts &&
			host.metrics().measurements == before.measurements &&
			host.metrics().arrangements == before.arrangements,
		"drawing update is paint-only"
	);
	require(host.nodes().back().id == identity, "occurrence stable");
	stana::rendering::Drawing bad = next;
	bad.commands.push_back({.kind = stana::rendering::PaintKind::popClip});
	const auto valid = host.scene();
	rejects([&] -> void {
		drawing.set(bad);
	});
	require(host.scene() == valid, "failed replacement retains previous scene");
	drawing.set(next);
	host.setEnvironment({.theme = stana::Theme::dark, .direction = stana::Direction::rightToLeft});
	require(
		host.scene()->commands.at(1).bounds.origin ==
			stana::Point{
				.x = host.nodes().back().bounds.origin.x + trailing,
				.y = host.nodes().back().bounds.origin.y + 2
			},
		"RTL padded origin"
	);
	require(
		host.scene()->commands.at(1).color == stana::Color::white(),
		"semantic theme resolution"
	);
	host.click(
		{.x = host.nodes().back().bounds.origin.x + extent,
			.y = host.nodes().back().bounds.origin.y + extent}
	);
	require(presses == 1, "canvas pointer interaction");
	present.set(false);
	const auto removed = host.metrics();
	drawing.set(initial);
	require(
		host.metrics().sceneRevisions == removed.sceneRevisions,
		"removed bindings disconnected"
	);
	host.mount(
		stana::ui::CPUCanvas{
			.drawing = stana::reactive::computed([drawing] -> stana::rendering::Drawing {
				return drawing.get();
			}),
			.appearance = {.clip = false}
		}
	);
	require(
		std::ranges::none_of(
			host.scene()->commands,
			[](const auto &command) -> bool {
				return command.kind == stana::rendering::PaintKind::pushClip;
			}
		),
		"explicit unclipping"
	);
	host.close();
	drawing.set(next);
}
} // namespace

auto main() -> int {
	try {
		values();
		retained();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
