#include <cmath>
#include <compare>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>

import stana;

namespace {
constexpr double epsilon = 1e-12;
constexpr double channelMax = 255;
constexpr double redByte = 0x11;
constexpr double greenByte = 0x22;
constexpr double blueByte = 0x33;
constexpr double alphaByte = 0x7F;
constexpr double twelve = 12;
constexpr double fifty = 50;
constexpr double ninety = 90;
constexpr double oneEighty = 180;
constexpr double ten = 10;
constexpr double two = 2;
constexpr double eight = 8;
constexpr double twenty = 20;
constexpr double five = 5;
constexpr double twentyFour = 24;
constexpr double fourFifths = 0.8;
constexpr double oneFifty = 150;
constexpr double oneTwenty = 120;
constexpr double eighty = 80;
constexpr double half = 0.5;
constexpr double quarter = 0.25;
constexpr double oneAndHalf = 1.5;
constexpr double negativeOne = -1;
constexpr std::uint32_t eightCount = 8;
constexpr double halfPiRadians = 1.5707963267948966;

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] auto near(double left, double right) -> bool {
	return std::abs(left - right) <= epsilon;
}

void literalsAndArithmetic() {
	using stana::literals::operator""_pt;
	using stana::literals::operator""_pct;
	using stana::literals::operator""_deg;
	using stana::literals::operator""_rad;
	using stana::literals::operator""_px;
	using stana::literals::operator""_color;
	const auto gap = operator""_pt(static_cast<long double>(twelve));
	const auto halfPercent = operator""_pct(static_cast<long double>(fifty));
	const auto turn = operator""_deg(static_cast<long double>(oneEighty));
	const auto halfPi = operator""_rad(static_cast<long double>(halfPiRadians));
	require(std::is_eq(gap <=> stana::Points{twelve}), "_pt builds Points");
	require(std::is_eq(halfPercent <=> stana::Percentage{fifty}), "_pct builds Percentage");
	require(near(turn.radians(), std::numbers::pi_v<double>), "_deg stores radians");
	require(near(halfPi.degrees(), ninety), "_rad converts to degrees");
	const auto left = stana::Points{ten};
	const auto step = stana::Points{two};
	require(std::is_eq((left + step) <=> stana::Points{twelve}), "Points addition");
	require(std::is_eq((left - step) <=> stana::Points{eight}), "Points subtraction");
	require(std::is_eq((left * two) <=> stana::Points{twenty}), "Points scale");
	require(std::is_eq((left / step) <=> stana::Ratio{five}), "Points ratio");
	const auto historical = operator""_px(static_cast<long double>(twentyFour));
	require(
		historical.kind == stana::Length::Kind::points &&
			std::is_eq(historical.absolute <=> stana::Points{twentyFour}),
		"_px remains Length::points"
	);
	constexpr auto opaque = operator""_color("#112233", 7);
	require(
		near(opaque.red.value(), redByte / channelMax) &&
			near(opaque.green.value(), greenByte / channelMax) &&
			near(opaque.blue.value(), blueByte / channelMax) &&
			std::is_eq(opaque.alpha <=> stana::Opacity{1}),
		"RRGGBB color literal"
	);
	constexpr auto translucent = operator""_color("#1122337F", 9);
	require(
		near(translucent.alpha.value(), alphaByte / channelMax),
		"RRGGBBAA color literal uses trailing alpha"
	);
}

void opacityAndLengthFactories() {
	using stana::literals::operator""_pct;
	using stana::literals::operator""_pt;
	const auto faded =
		stana::Opacity::percent(operator""_pct(static_cast<long double>(eighty)));
	require(near(faded.value(), fourFifths), "Opacity::percent maps percent of opaque");
	bool rejected = false;
	try {
		static_cast<void>(
			stana::Opacity::percent(operator""_pct(static_cast<long double>(oneFifty)))
		);
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	require(rejected, "Opacity::percent rejects >100%");
	rejected = false;
	try {
		static_cast<void>(stana::Opacity::percent(stana::Percentage{negativeOne}));
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	require(rejected, "Opacity::percent rejects negative");
	const auto absolute =
		stana::Length::points(operator""_pt(static_cast<long double>(oneTwenty)));
	const auto relative =
		stana::Length::percent(operator""_pct(static_cast<long double>(fifty)));
	const auto fill = stana::Length::fill();
	require(
		absolute.kind == stana::Length::Kind::points &&
			std::is_eq(absolute.absolute <=> stana::Points{oneTwenty}),
		"Length::points"
	);
	require(
		relative.kind == stana::Length::Kind::percent &&
			std::is_eq(relative.relative <=> stana::Percentage{fifty}),
		"Length::percent"
	);
	require(fill.kind == stana::Length::Kind::fill, "Length::fill");
}

void namedQuantities() {
	require(
		std::is_eq(stana::FontWeight::regular() <=> stana::FontWeight{}),
		"FontWeight::regular"
	);
	require(
		std::is_eq(stana::Progress{half} <=> stana::Progress{half}),
		"Progress equality"
	);
	require(
		std::is_eq(stana::ScaleFactor{two} <=> stana::ScaleFactor{two}),
		"ScaleFactor equality"
	);
	require(std::is_eq(stana::Ratio{five} <=> stana::Ratio{five}), "Ratio equality");
	require(
		std::is_eq(stana::SdrChannel{quarter} <=> stana::SdrChannel{quarter}),
		"SdrChannel equality"
	);
	require(
		std::is_eq(stana::LinearChannel{oneAndHalf} <=> stana::LinearChannel{oneAndHalf}),
		"LinearChannel equality"
	);
	require(
		std::is_eq(stana::PixelDistance{oneAndHalf} <=> stana::PixelDistance{oneAndHalf}),
		"PixelDistance equality"
	);
	require(
		std::is_eq(stana::PixelCount{eightCount} <=> stana::PixelCount{eightCount}),
		"PixelCount equality"
	);
}
} // namespace

auto main() -> int {
	try {
		literalsAndArithmetic();
		opacityAndLengthFactories();
		namedQuantities();
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
