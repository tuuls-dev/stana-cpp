#include <cmath>
#include <compare>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

import stana;

namespace {
constexpr double accentRed = 0.12;
constexpr double accentGreen = 0.38;
constexpr double accentBlue = 0.92;
constexpr double half = 0.5;

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void tokenPalette() {
	using stana::theming::ColorToken;
	using stana::theming::resolveColor;
	const auto light = resolveColor(ColorToken::accent(), {});
	const auto *rgba = light.asRGBA();
	require(
		rgba != nullptr && std::is_eq(rgba->red <=> stana::SdrChannel{accentRed}) &&
			std::is_eq(rgba->green <=> stana::SdrChannel{accentGreen}) &&
			std::is_eq(rgba->blue <=> stana::SdrChannel{accentBlue}),
		"accent palette is encoded sRGB"
	);
	require(resolveColor(ColorToken::text(), {}) == stana::Color::black(), "light text is black");
	stana::Environment dark{.theme = stana::Theme::dark};
	require(resolveColor(ColorToken::text(), dark) == stana::Color::white(), "dark text is white");
	const auto faded = resolveColor(ColorToken::accent(stana::Opacity{half}), {});
	require(std::is_eq(faded.alpha() <=> stana::Opacity{half}), "token alpha is absolute");
	stana::Environment contrast{.highContrast = true};
	const auto border = resolveColor(ColorToken::border(), contrast);
	const auto *borderRgba = border.asRGBA();
	require(
		borderRgba != nullptr && std::is_eq(borderRgba->red <=> stana::SdrChannel{0.2}),
		"high-contrast light border"
	);
}

void concreteIdentity() {
	using namespace stana::literals;
	const auto authored = stana::Color::oklch(0.65_okl, 0.18_okc, 250_deg);
	const stana::theming::ColorSource source{authored};
	const auto resolved = stana::theming::resolveColor(source, {.theme = stana::Theme::dark});
	require(resolved == authored, "resolveColor keeps OKLCH coordinates");
	require(resolved.asOKLCH() != nullptr, "resolveColor does not convert OKLCH");
	require(source.asToken() == nullptr, "concrete source is not a token");
}
} // namespace

auto main() -> int {
	try {
		tokenPalette();
		concreteIdentity();
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
