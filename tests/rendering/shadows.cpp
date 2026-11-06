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
constexpr double numericTolerance = 1e-9;
constexpr double midpoint = 0.5;
constexpr double beforeMidpoint = 0.49;
constexpr double offsetMagnitude = 8;
constexpr double normalizedHorizontal = 50;
constexpr double normalizedVertical = 25;
constexpr double leadingWidth = 5;
constexpr double paddingWidth = 86;
constexpr double directionRadius = 20;
constexpr double trailingWidth = 9;
constexpr double spreadAdjustedRadius = 27.5;
constexpr double inkTopMagnitude = 13;
constexpr double inkWidth = 134;
constexpr double collapsedWidth = 40;

void require(bool value, const std::string &message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

void near(double actual, double expected) {
	require(std::abs(actual - expected) < numericTolerance, "shadow numeric contract");
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
		.offset = {.x = -offsetMagnitude, .y = 4},
		.blurRadius = 6,
		.spreadRadius = -2,
		.color = Color::rgb(1, 0, 0)
	}};
	const std::array end{BoxShadow{
		.offset = {.x = offsetMagnitude, .y = -4},
		.blurRadius = 2,
		.spreadRadius = 2,
		.color = Color::rgb(0, 0, 1, 0)
	}};
	const auto middle = interpolateBoxShadows(start, end, midpoint);
	near(middle.front().offset.x, 0);
	near(middle.front().blurRadius, 4);
	near(middle.front().color.value_or(Color::clear()).red, 1);
	near(middle.front().color.value_or(Color::clear()).blue, 0);
	near(middle.front().color.value_or(Color::clear()).alpha, midpoint);
	require(interpolateBoxShadows({}, {}, midpoint).empty(), "empty interpolation");
	const auto padded = interpolateBoxShadows({}, start, midpoint);
	near(padded.front().offset.x, -4);
	near(padded.front().color.value_or(Color::clear()).alpha, midpoint);
	const std::array inset{BoxShadow{.color = Color::surface(), .isInset = true}};
	require(
		!interpolateBoxShadows(start, inset, beforeMidpoint).front().isInset &&
			interpolateBoxShadows(start, inset, midpoint).front().isInset,
		"whole-list discrete inset switch"
	);
	const std::array omitted{BoxShadow{}};
	const auto dark =
		interpolateBoxShadows(omitted, omitted, midpoint, Color::text(), {.theme = Theme::dark});
	require(dark.front().color == Color::white(), "omitted themed foreground");
	near(interpolateBoxShadows(start, start, 0).front().offset.x, -offsetMagnitude);
	near(interpolateBoxShadows(start, start, 1).front().offset.x, -offsetMagnitude);
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
		{.topLeading = {.width = 80, .height = collapsedWidth},
			.topTrailing = {.width = 80, .height = collapsedWidth}},
		{.top = 3, .leading = leadingWidth, .bottom = 7, .trailing = trailingWidth}
	);
	near(geometry.outer.radii.front().width, normalizedHorizontal);
	near(geometry.outer.radii.front().height, normalizedVertical);
	near(geometry.padding.bounds.origin.x, leadingWidth);
	near(geometry.padding.bounds.size.width, paddingWidth);
	const auto rtl = rendering::detail::boxGeometry(
		bounds,
		{.topLeading = {.width = directionRadius, .height = 10}},
		{.leading = leadingWidth, .trailing = trailingWidth},
		Direction::rightToLeft
	);
	near(rtl.outer.radii.at(1).width, directionRadius);
	near(rtl.padding.bounds.origin.x, trailingWidth);
	const std::array spread{BoxShadow{.spreadRadius = directionRadius}};
	const auto paints = rendering::detail::prepareBoxShadows(bounds, spread, CornerRadii::all(10));
	near(paints.front().caster.radii.front().width, spreadAdjustedRadius);
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
		BoxShadow{.offset = {.x = -offsetMagnitude, .y = 4}, .blurRadius = 10, .spreadRadius = 2}
	};
	const auto ink = rendering::detail::prepareBoxShadows(bounds, blurred).front().ink;
	near(ink.origin.x, -normalizedVertical);
	near(ink.origin.y, -inkTopMagnitude);
	near(ink.size.width, inkWidth);
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

void geometryAndValidation() {
	using namespace stana;
	using rendering::BoxShadow;
	const Rect bounds{.size = {.width = collapsedWidth, .height = directionRadius}};
	const std::array layers{
		BoxShadow{.offset = {.x = 1}},
		BoxShadow{.offset = {.x = 2}, .isInset = true},
		BoxShadow{.offset = {.x = 3}}
	};
	const auto prepared = rendering::detail::prepareBoxShadows(bounds, layers);
	require(
		prepared.size() == 3 && prepared.front().offset.x > prepared.back().offset.x &&
			prepared.at(1).isInset,
		"first authored entry is frontmost in painter order"
	);
	const std::array transparent{BoxShadow{.color = Color::clear()}};
	require(
		rendering::detail::prepareBoxShadows(bounds, transparent).empty(),
		"transparent shadows paint nothing"
	);
	const std::array expanded{BoxShadow{.spreadRadius = 4}};
	require(
		!rendering::detail::prepareBoxShadows({}, expanded).empty(),
		"positive spread can expand zero-size caster"
	);
	const std::array inset{BoxShadow{.isInset = true}};
	require(
		rendering::detail::prepareBoxShadows(bounds, inset, {}, BorderWidths::all(collapsedWidth))
			.empty(),
		"collapsed padding paints no inset shadow"
	);
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double infinity = std::numeric_limits<double>::infinity();
	const std::array invalid{
		BoxShadow{.offset = {.x = nan}},
		BoxShadow{.offset = {.y = infinity}},
		BoxShadow{.blurRadius = -1},
		BoxShadow{.blurRadius = nan},
		BoxShadow{.spreadRadius = infinity},
		BoxShadow{.color = Color::rgb(1, 0, 0, -1)}
	};
	for (const auto &shadow : invalid) {
		const std::array single{shadow};
		bool rejected = false;
		try {
			static_cast<void>(rendering::detail::prepareBoxShadows(bounds, single));
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		require(rejected, "nonfinite geometry/invalid color rejected");
	}
	for (const CornerRadii corners : {CornerRadii::all(-1), CornerRadii::all(nan)}) {
		bool rejected = false;
		try {
			static_cast<void>(rendering::detail::boxGeometry(bounds, corners));
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		require(rejected, "invalid elliptical corner rejected");
	}
	bool rejected = false;
	try {
		static_cast<void>(rendering::detail::boxGeometry(bounds, {}, {.trailing = infinity}));
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	require(rejected, "invalid edge width rejected");
	const auto half = rendering::interpolateBoxShadows({}, inset, midpoint);
	require(
		half.front().isInset && half.front().color.value_or(Color::black()).alpha < 1,
		"unequal inset list pads matching inset transparent layers"
	);
}

} // namespace

auto main() -> int {
	try {
		values();
		geometryAndValidation();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
