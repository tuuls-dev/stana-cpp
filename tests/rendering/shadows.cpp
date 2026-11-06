#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>
import stana;

namespace {
void require(bool value, const std::string &message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

void near(double actual, double expected) {
	require(std::abs(actual - expected) < 1e-9, "shadow numeric contract");
}

void values() {
	using namespace stana;
	using rendering::BoxShadow;
	using rendering::interpolateBoxShadows;
	const BoxShadow zero;
	require(
		zero.offset == Point{} && zero.blurRadius <= 0 && zero.spreadRadius <= 0 && !zero.color &&
			!zero.isInset && zero == BoxShadow{},
		"shadow defaults/comparison"
	);
	const std::array start{BoxShadow{
		.offset = {.x = -8, .y = 4},
		.blurRadius = 6,
		.spreadRadius = -2,
		.color = Color::rgb(1, 0, 0)
	}};
	const std::array end{BoxShadow{
		.offset = {.x = 8, .y = -4},
		.blurRadius = 2,
		.spreadRadius = 2,
		.color = Color::rgb(0, 0, 1, 0)
	}};
	const auto middle = interpolateBoxShadows(start, end, 0.5);
	near(middle.front().offset.x, 0);
	near(middle.front().blurRadius, 4);
	near(middle.front().color.value_or(Color::clear()).red, 1);
	near(middle.front().color.value_or(Color::clear()).blue, 0);
	near(middle.front().color.value_or(Color::clear()).alpha, 0.5);
	require(interpolateBoxShadows({}, {}, 0.5).empty(), "empty interpolation");
	const auto padded = interpolateBoxShadows({}, start, 0.5);
	near(padded.front().offset.x, -4);
	near(padded.front().color.value_or(Color::clear()).alpha, 0.5);
	const std::array inset{BoxShadow{.color = Color::surface(), .isInset = true}};
	require(
		!interpolateBoxShadows(start, inset, 0.49).front().isInset &&
			interpolateBoxShadows(start, inset, 0.5).front().isInset,
		"whole-list discrete inset switch"
	);
	const std::array omitted{BoxShadow{}};
	const auto dark =
		interpolateBoxShadows(omitted, omitted, 0.5, Color::text(), {.theme = Theme::dark});
	require(dark.front().color == Color::white(), "omitted themed foreground");
	near(interpolateBoxShadows(start, start, 0).front().offset.x, -8);
	near(interpolateBoxShadows(start, start, 1).front().offset.x, -8);
	for (const double progress :
		{-1.0,
			2.0,
			std::numeric_limits<double>::infinity(),
			std::numeric_limits<double>::quiet_NaN()}) {
		bool threw = false;
		try {
			static_cast<void>(interpolateBoxShadows(start, end, progress));
		} catch (const std::invalid_argument &) {
			threw = true;
		}
		require(threw, "invalid progress rejected");
	}
	const Rect bounds{.size = {.width = 100, .height = 60}};
	const auto geometry = rendering::detail::boxGeometry(
		bounds,
		{.topLeading = {.width = 80, .height = 40}, .topTrailing = {.width = 80, .height = 40}},
		{.top = 3, .leading = 5, .bottom = 7, .trailing = 9}
	);
	near(geometry.outer.radii.front().width, 50);
	near(geometry.outer.radii.front().height, 25);
	near(geometry.padding.bounds.origin.x, 5);
	near(geometry.padding.bounds.size.width, 86);
	const auto rtl = rendering::detail::boxGeometry(
		bounds,
		{.topLeading = {.width = 20, .height = 10}},
		{.leading = 5, .trailing = 9},
		Direction::rightToLeft
	);
	near(rtl.outer.radii.at(1).width, 20);
	near(rtl.padding.bounds.origin.x, 9);
	const std::array spread{BoxShadow{.spreadRadius = 20}};
	const auto paints = rendering::detail::prepareBoxShadows(bounds, spread, CornerRadii::all(10));
	near(paints.front().caster.radii.front().width, 27.5);
	const std::array collapsed{BoxShadow{.spreadRadius = -60}};
	require(
		rendering::detail::prepareBoxShadows(bounds, collapsed).empty(),
		"collapsed outer is empty"
	);
	const std::array collapsedInset{BoxShadow{.spreadRadius = 60, .isInset = true}};
	require(
		!rendering::detail::prepareBoxShadows(bounds, collapsedInset).empty(),
		"collapsed inset fills padding"
	);
	require(rendering::detail::prepareBoxShadows({}, omitted).empty(), "zero-size has no shadow");
	const std::array blurred{
		BoxShadow{.offset = {.x = -8, .y = 4}, .blurRadius = 10, .spreadRadius = 2}
	};
	const auto ink = rendering::detail::prepareBoxShadows(bounds, blurred).front().ink;
	near(ink.origin.x, -25);
	near(ink.origin.y, -13);
	near(ink.size.width, 134);
	rendering::GPUDrawing drawing;
	std::vector authored{BoxShadow{.blurRadius = 4}};
	drawing.drawBoxShadows(bounds, authored);
	authored.clear();
	const auto commands = rendering::detail::GPUAccess::commands(drawing);
	require(
		std::get<rendering::detail::GPUBoxShadows>(commands.front()).shadows.size() == 1,
		"GPU commands own caller data"
	);
	const auto before = drawing;
	const std::array invalid{BoxShadow{.blurRadius = -1}};
	bool threw = false;
	try {
		drawing.drawBoxShadows(bounds, invalid);
	} catch (const std::invalid_argument &) {
		threw = true;
	}
	require(threw && drawing == before, "invalid append preserves drawing");
}
} // namespace

auto main() -> int {
	try {
		values();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
