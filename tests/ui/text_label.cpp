#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
import stana;

namespace {
constexpr stana::Points siblingWidth{40};
constexpr stana::Points initialFillWidth{80};
constexpr stana::Points naturalLineHeight{20};
constexpr stana::Points expandedRowWidth{200};
constexpr stana::Points expandedFillWidth{160};
constexpr stana::Points smallFontSize{10};
constexpr stana::Points normalFontSize{14};
constexpr stana::Points explicitLineHeight{30};
constexpr stana::Points twoLineHeight{60};

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
					.layout = {.frame = stana::Frame{.width = stana::Length::points(siblingWidth)}}
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
		first.kind == "textLabel" && first.bounds.size.width == initialFillWidth &&
			first.bounds.size.height > naturalLineHeight,
		"fill label wraps in remaining sibling space"
	);
	const auto mounts = host.metrics().mounts;
	width.set(stana::Frame{.width = stana::Length::points(expandedRowWidth)});
	const auto wider = named(host, text.get());
	require(
		wider.id == first.id && host.metrics().mounts == mounts &&
			wider.bounds.size.width == expandedFillWidth &&
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
				.font = stana::Font::system(smallFontSize),
				.fontSize = normalFontSize,
				.fontStyle = italic,
				.lineHeight = explicitLineHeight,
				.maxLines = std::size_t{2},
				.isStrikethrough = strike
			}
		}
	);
	require(
		named(host, "first\nsecond\nthird").bounds.size.height == twoLineHeight,
		"line count clips at explicit line height"
	);
	const auto scene = host.scene();
	std::size_t textCommands = 0, strikes = 0;
	for (const auto &command : scene->commands) {
		if (command.kind == stana::rendering::PaintKind::text) {
			++textCommands;
			require(
				command.font.size == normalFontSize &&
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
