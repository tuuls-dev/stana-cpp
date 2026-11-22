#ifndef STANA_RENDERING_COLOR_SPACE_HPP
#define STANA_RENDERING_COLOR_SPACE_HPP

// Included after the named-module declaration so Color is visible. Standard
// headers and src/gpu/color.hpp belong in the includer's global module fragment.

namespace stana::rendering::detail {

/// @brief Destination working space for paint-preparation conversion.
/// Encoded sRGB is the CPU/native SceneColor contract. Linear sRGB is used by
/// GPU paths that already carry a linear flag, avoiding an encoded intermediate.
enum class WorkingColorSpace : std::uint8_t { encodedSRGB, linearSRGB };

struct PreparedColor {
	double red = 0;
	double green = 0;
	double blue = 0;
	double alpha = 1;
};

[[nodiscard]] inline auto clipUnit(double value) -> double {
	if (!std::isfinite(value)) {
		throw std::invalid_argument("Color conversion requires finite channels");
	}
	return std::clamp(value, 0.0, 1.0);
}

/// @brief CSS Color 4 / Ottosson OKLCH → linear sRGB. Alpha is unchanged.
/// Destination clipping is applied by the caller for the chosen working space.
/// Authored OKLCH coordinates are not mutated.
[[nodiscard]] inline auto oklchToLinearSRGB(const ColorOKLCH &oklch) -> PreparedColor {
	const double lightness = oklch.lightness.value();
	const double chroma = oklch.chroma.value();
	if (!std::isfinite(lightness) || !std::isfinite(chroma) ||
		!std::isfinite(oklch.hue.radians()) || !std::isfinite(oklch.alpha.value())) {
		throw std::invalid_argument("Color conversion requires finite OKLCH coordinates");
	}
	const double hue = oklch.hue.radians();
	const double a = chroma * std::cos(hue);
	const double b = chroma * std::sin(hue);
	constexpr double lmsLFromA = 0.3963377774;
	constexpr double lmsLFromB = 0.2158037573;
	constexpr double lmsMFromA = -0.1055613458;
	constexpr double lmsMFromB = -0.0638541728;
	constexpr double lmsSFromA = -0.0894841775;
	constexpr double lmsSFromB = -1.2914855480;
	const double l_ = lightness + lmsLFromA * a + lmsLFromB * b;
	const double m_ = lightness + lmsMFromA * a + lmsMFromB * b;
	const double s_ = lightness + lmsSFromA * a + lmsSFromB * b;
	const double l = l_ * l_ * l_;
	const double m = m_ * m_ * m_;
	const double s = s_ * s_ * s_;
	constexpr double redFromL = 4.0767416621;
	constexpr double redFromM = -3.3077115913;
	constexpr double redFromS = 0.2309699292;
	constexpr double greenFromL = -1.2684380046;
	constexpr double greenFromM = 2.6097574011;
	constexpr double greenFromS = -0.3413193965;
	constexpr double blueFromL = -0.0041960863;
	constexpr double blueFromM = -0.7034186147;
	constexpr double blueFromS = 1.7076147010;
	return {
		.red = redFromL * l + redFromM * m + redFromS * s,
		.green = greenFromL * l + greenFromM * m + greenFromS * s,
		.blue = blueFromL * l + blueFromM * m + blueFromS * s,
		.alpha = oklch.alpha.value()
	};
}

/// @brief Converts a concrete Color into the destination working space.
/// RGBA stays encoded sRGB until a linear destination requests decode.
/// OKLCH goes OKLCH → OKLab → linear sRGB, then encodes only when required.
/// SDR destinations clip each RGB channel to [0, 1]; authored coordinates are
/// not clipped. Alpha passes through unchanged.
[[nodiscard]] inline auto prepareColor(const Color &color, WorkingColorSpace space)
	-> PreparedColor {
	if (const auto *rgba = color.asRGBA()) {
		PreparedColor prepared{
			.red = rgba->red.value(),
			.green = rgba->green.value(),
			.blue = rgba->blue.value(),
			.alpha = rgba->alpha.value()
		};
		if (space == WorkingColorSpace::linearSRGB) {
			prepared.red = stana::gpu::detail::decodeSRGB(prepared.red);
			prepared.green = stana::gpu::detail::decodeSRGB(prepared.green);
			prepared.blue = stana::gpu::detail::decodeSRGB(prepared.blue);
		}
		prepared.red = clipUnit(prepared.red);
		prepared.green = clipUnit(prepared.green);
		prepared.blue = clipUnit(prepared.blue);
		prepared.alpha = clipUnit(prepared.alpha);
		return prepared;
	}
	const auto *oklch = color.asOKLCH();
	if (oklch == nullptr) {
		throw std::invalid_argument("Color has no retained representation");
	}
	auto prepared = oklchToLinearSRGB(*oklch);
	prepared.red = clipUnit(prepared.red);
	prepared.green = clipUnit(prepared.green);
	prepared.blue = clipUnit(prepared.blue);
	prepared.alpha = clipUnit(prepared.alpha);
	if (space == WorkingColorSpace::encodedSRGB) {
		prepared.red = stana::gpu::detail::encodeSRGB(prepared.red);
		prepared.green = stana::gpu::detail::encodeSRGB(prepared.green);
		prepared.blue = stana::gpu::detail::encodeSRGB(prepared.blue);
	}
	return prepared;
}

[[nodiscard]] inline auto prepareEncoded(const Color &color) -> PreparedColor {
	return prepareColor(color, WorkingColorSpace::encodedSRGB);
}

[[nodiscard]] inline auto prepareLinear(const Color &color) -> PreparedColor {
	return prepareColor(color, WorkingColorSpace::linearSRGB);
}

} // namespace stana::rendering::detail

#endif
