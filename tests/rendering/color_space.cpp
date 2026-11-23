#include "../../src/gpu/color.hpp"
#include <algorithm>
#include <cmath>
#include <compare>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

import stana;

#include "../../src/rendering/color_space.hpp"

namespace {
constexpr double epsilon = 1e-4;
constexpr double highChroma = 0.4;

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] auto near(double left, double right) -> bool {
	return std::abs(left - right) <= epsilon;
}

void neutralsAndAlpha() {
	using namespace stana::literals;
	using stana::rendering::detail::prepareEncoded;
	using stana::rendering::detail::prepareLinear;
	const auto white = stana::Color::oklch(1.0_okl, 0.0_okc, 0_deg);
	const auto linearWhite = prepareLinear(white);
	require(
		near(linearWhite.red, 1) && near(linearWhite.green, 1) && near(linearWhite.blue, 1),
		"neutral white OKLCH is linear sRGB white"
	);
	const auto black = stana::Color::oklch(0.0_okl, 0.0_okc, 40_deg);
	const auto linearBlack = prepareLinear(black);
	require(
		near(linearBlack.red, 0) && near(linearBlack.green, 0) && near(linearBlack.blue, 0),
		"neutral black OKLCH is linear sRGB black"
	);
	const auto faded = white.withAlpha(stana::Opacity{0.5});
	require(near(prepareEncoded(faded).alpha, 0.5), "alpha is preserved through conversion");
	require(faded.asOKLCH() != nullptr, "conversion does not rewrite authored color");
}

void hueWrapAndGamut() {
	using namespace stana::literals;
	using stana::rendering::detail::prepareLinear;
	const auto wrapped = stana::Color::oklch(0.65_okl, 0.12_okc, 370_deg);
	const auto canonical = stana::Color::oklch(0.65_okl, 0.12_okc, 10_deg);
	require(!(wrapped == canonical), "authored hues stay distinct");
	const auto a = prepareLinear(wrapped);
	const auto b = prepareLinear(canonical);
	require(
		near(a.red, b.red) && near(a.green, b.green) && near(a.blue, b.blue),
		"hue wrapping is trigonometric at conversion"
	);
	const auto hot = stana::Color::oklch(0.6_okl, stana::OklchChroma{highChroma}, 250_deg);
	const auto encoded = stana::rendering::detail::prepareEncoded(hot);
	require(
		encoded.red >= 0 && encoded.red <= 1 && encoded.green >= 0 && encoded.green <= 1 &&
			encoded.blue >= 0 && encoded.blue <= 1,
		"SDR encoded destination clips out-of-gamut channels"
	);
	require(
		std::is_eq(hot.asOKLCH()->chroma <=> stana::OklchChroma{highChroma}),
		"authored chroma is not clipped"
	);
}

void encodedRgbaPassThrough() {
	const auto red = stana::Color::rgb(1, 0, 0, 0.25);
	const auto encoded = stana::rendering::detail::prepareEncoded(red);
	require(
		near(encoded.red, 1) && near(encoded.green, 0) && near(encoded.blue, 0) &&
			near(encoded.alpha, 0.25),
		"encoded RGBA is not re-encoded"
	);
	bool rejected = false;
	try {
		static_cast<void>(stana::Color::oklch(
			stana::OklchLightness{std::numeric_limits<double>::infinity()},
			stana::OklchChroma{0},
			stana::Angle{}
		));
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	require(rejected, "nonfinite lightness is diagnosed");
}
} // namespace

auto main() -> int {
	try {
		neutralsAndAlpha();
		hueWrapAndGamut();
		encodedRgbaPassThrough();
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
