#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
import stana;

namespace {
void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] auto named(const stana::testing::TestHost &host, const std::string &value)
	-> stana::document::NodeSnapshot {
	for (const auto &node : host.nodes()) {
		if (node.text == value) {
			return node;
		}
	}
	throw std::runtime_error("Missing label");
}

void constrainedWrapping() {
	const stana::reactive::Signal text{std::string{"one two three four"}};
	const stana::reactive::Signal width{
		stana::Frame{.width = stana::Length::points(stana::Points{120})}
	};
	stana::testing::TestHost host;
	host.mount(
		stana::ui::HStack{
			.layout = {.frame = width},
			.children = stana::ui::children(
				stana::ui::TextLabel{
					.text = "X",
					.layout =
						{.frame = stana::Frame{.width = stana::Length::points(stana::Points{40})}}
				},
				stana::ui::TextLabel{
					.text = text,
					.layout = {.frame = stana::Frame{.width = stana::Length::fill()}}
				}
			)
		}
	);
	const auto first = named(host, text.get());
	require(
		first.kind == "textLabel" && first.bounds.size.width == stana::Points{80} &&
			first.bounds.size.height > stana::Points{20},
		"fill label wraps in remaining sibling space"
	);
	const auto mounts = host.metrics().mounts;
	width.set(stana::Frame{.width = stana::Length::points(stana::Points{200})});
	const auto wider = named(host, text.get());
	require(
		wider.id == first.id && host.metrics().mounts == mounts &&
			wider.bounds.size.width == stana::Points{160} &&
			wider.bounds.size.height < first.bounds.size.height,
		"width change reflows same occurrence"
	);
	const auto scene = host.scene()->commands;
	host.forceFullRecompute();
	require(host.scene()->commands == scene, "incremental and full wrapping equivalent");
}

void typographyAndLineLimits() {
	const stana::reactive::Signal italic{stana::FontStyle::italic};
	const stana::reactive::Signal strike{true};
	stana::testing::TestHost host;
	host.mount(
		stana::ui::TextLabel{
			.text = "first\nsecond\nthird",
			.textStyle = {
				.font = stana::Font::system(stana::Points{10}),
				.fontSize = stana::Points{14},
				.fontStyle = italic,
				.lineHeight = stana::Points{30},
				.maxLines = std::size_t{2},
				.isStrikethrough = strike
			}
		}
	);
	require(
		named(host, "first\nsecond\nthird").bounds.size.height == stana::Points{60},
		"line count clips at explicit line height"
	);
	const auto scene = host.scene();
	std::size_t textCommands = 0, strikes = 0;
	for (const auto &command : scene->commands) {
		if (command.kind == stana::rendering::PaintKind::text) {
			++textCommands;
			require(
				command.font.size == stana::Points{14} &&
					command.font.style == stana::FontStyle::italic,
				"individual font overrides whole font"
			);
		}
		if (command.kind == stana::rendering::PaintKind::line) {
			++strikes;
		}
	}
	require(textCommands == 2 && strikes == 2, "each visible line has glyphs and strike");
	const auto measurements = host.metrics().measurements;
	strike.set(false);
	require(host.metrics().measurements == measurements, "decoration is paint-only");
}
} // namespace

auto main() -> int {
	try {
		constrainedWrapping();
		typographyAndLineLimits();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
