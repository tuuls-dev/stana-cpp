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
		zero.offset == Point{} && zero.blurRadius <= Points{0} && zero.spreadRadius <= Points{0} &&
			!zero.color && !zero.isInset && zero == BoxShadow{},
		"shadow defaults/comparison"
	);
	const std::array start{BoxShadow{
		.offset = {.x = Points{-offsetMagnitude}, .y = Points{4}},
		.blurRadius = Points{6},
		.spreadRadius = Points{-2},
		.color = Color::rgb(1, 0, 0)
	}};
	const std::array end{BoxShadow{
		.offset = {.x = Points{offsetMagnitude}, .y = Points{-4}},
		.blurRadius = Points{2},
		.spreadRadius = Points{2},
		.color = Color::rgb(0, 0, 1, 0)
	}};
	const auto middle = interpolateBoxShadows(start, end, Progress{midpoint});
	near(middle.front().offset.x.value(), 0);
	near(middle.front().blurRadius.value(), 4);
	near(middle.front().color.value_or(Color::clear()).red.value(), 1);
	near(middle.front().color.value_or(Color::clear()).blue.value(), 0);
	near(middle.front().color.value_or(Color::clear()).alpha.value(), midpoint);
	require(interpolateBoxShadows({}, {}, Progress{midpoint}).empty(), "empty interpolation");
	const auto padded = interpolateBoxShadows({}, start, Progress{midpoint});
	near(padded.front().offset.x.value(), -4);
	near(padded.front().color.value_or(Color::clear()).alpha.value(), midpoint);
	const std::array inset{BoxShadow{.color = Color::surface(), .isInset = true}};
	require(
		!interpolateBoxShadows(start, inset, Progress{beforeMidpoint}).front().isInset &&
			interpolateBoxShadows(start, inset, Progress{midpoint}).front().isInset,
		"whole-list discrete inset switch"
	);
	const std::array omitted{BoxShadow{}};
	const auto dark = interpolateBoxShadows(
		omitted,
		omitted,
		Progress{midpoint},
		Color::text(),
		{.theme = Theme::dark}
	);
	require(dark.front().color == Color::white(), "omitted themed foreground");
	near(
		interpolateBoxShadows(start, start, Progress{0}).front().offset.x.value(),
		-offsetMagnitude
	);
	near(
		interpolateBoxShadows(start, start, Progress{1}).front().offset.x.value(),
		-offsetMagnitude
	);
	for (const double progress :
		{-1.0,
			2.0,
			std::numeric_limits<double>::infinity(),
			std::numeric_limits<double>::quiet_NaN()}) {
		bool threw = false;
		try {
			static_cast<void>(interpolateBoxShadows(start, end, Progress{progress}));
		} catch (const std::invalid_argument &) {
			threw = true;
		}
		require(threw, "invalid progress rejected");
	}
	const Rect bounds{.size = {.width = Points{100}, .height = Points{60}}};
	const auto geometry = rendering::detail::boxGeometry(
		bounds,
		{.topLeading = {.width = Points{80}, .height = Points{collapsedWidth}},
			.topTrailing = {.width = Points{80}, .height = Points{collapsedWidth}}},
		{.top = Points{3},
			.leading = Points{leadingWidth},
			.bottom = Points{7},
			.trailing = Points{trailingWidth}}
	);
	near(geometry.outer.radii.front().width.value(), normalizedHorizontal);
	near(geometry.outer.radii.front().height.value(), normalizedVertical);
	near(geometry.padding.bounds.origin.x.value(), leadingWidth);
	near(geometry.padding.bounds.size.width.value(), paddingWidth);
	const auto rtl = rendering::detail::boxGeometry(
		bounds,
		{.topLeading = {.width = Points{directionRadius}, .height = Points{10}}},
		{.leading = Points{leadingWidth}, .trailing = Points{trailingWidth}},
		Direction::rightToLeft
	);
	near(rtl.outer.radii.at(1).width.value(), directionRadius);
	near(rtl.padding.bounds.origin.x.value(), trailingWidth);
	const std::array spread{BoxShadow{.spreadRadius = Points{directionRadius}}};
	const auto paints =
		rendering::detail::prepareBoxShadows(bounds, spread, CornerRadii::all(Points{10}));
	near(paints.front().caster.radii.front().width.value(), spreadAdjustedRadius);
	const std::array collapsed{BoxShadow{.spreadRadius = Points{-60}}};
	require(
		rendering::detail::prepareBoxShadows(bounds, collapsed).empty(),
		"collapsed outer is empty"
	);
	const std::array collapsedInset{BoxShadow{.spreadRadius = Points{60}, .isInset = true}};
	require(
		!rendering::detail::prepareBoxShadows(bounds, collapsedInset).empty(),
		"collapsed inset fills padding"
	);
	require(rendering::detail::prepareBoxShadows({}, omitted).empty(), "zero-size has no shadow");
	const std::array blurred{BoxShadow{
		.offset = {.x = Points{-offsetMagnitude}, .y = Points{4}},
		.blurRadius = Points{10},
		.spreadRadius = Points{2}
	}};
	const auto ink = rendering::detail::prepareBoxShadows(bounds, blurred).front().ink;
	near(ink.origin.x.value(), -normalizedVertical);
	near(ink.origin.y.value(), -inkTopMagnitude);
	near(ink.size.width.value(), inkWidth);
	rendering::GPUDrawing drawing;
	std::vector authored{BoxShadow{.blurRadius = Points{4}}};
	drawing.drawBoxShadows(bounds, authored);
	authored.clear();
	const auto commands = rendering::detail::GPUAccess::commands(drawing);
	require(
		std::get<rendering::detail::GPUBoxShadows>(commands.front()).shadows.size() == 1,
		"GPU commands own caller data"
	);
	const auto before = drawing;
	const std::array invalid{BoxShadow{.blurRadius = Points{-1}}};
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
	const Rect bounds{.size = {.width = Points{collapsedWidth}, .height = Points{directionRadius}}};
	const std::array layers{
		BoxShadow{.offset = {.x = Points{1}}},
		BoxShadow{.offset = {.x = Points{2}}, .isInset = true},
		BoxShadow{.offset = {.x = Points{3}}}
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
	const std::array expanded{BoxShadow{.spreadRadius = Points{4}}};
	require(
		!rendering::detail::prepareBoxShadows({}, expanded).empty(),
		"positive spread can expand zero-size caster"
	);
	const std::array inset{BoxShadow{.isInset = true}};
	require(
		rendering::detail::prepareBoxShadows(
			bounds,
			inset,
			{},
			BorderWidths::all(Points{collapsedWidth})
		)
			.empty(),
		"collapsed padding paints no inset shadow"
	);
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double infinity = std::numeric_limits<double>::infinity();
	const std::array invalid{
		BoxShadow{.offset = {.x = Points{nan}}},
		BoxShadow{.offset = {.y = Points{infinity}}},
		BoxShadow{.blurRadius = Points{-1}},
		BoxShadow{.blurRadius = Points{nan}},
		BoxShadow{.spreadRadius = Points{infinity}},
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
	for (const CornerRadii corners :
		{CornerRadii::all(Points{-1}), CornerRadii::all(Points{nan})}) {
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
		static_cast<void>(
			rendering::detail::boxGeometry(bounds, {}, {.trailing = Points{infinity}})
		);
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	require(rejected, "invalid edge width rejected");
	const auto half = rendering::interpolateBoxShadows({}, inset, Progress{midpoint});
	require(
		half.front().isInset && half.front().color.value_or(Color::black()).alpha < Opacity{1},
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
