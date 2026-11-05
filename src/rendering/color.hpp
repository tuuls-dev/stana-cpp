#ifndef STANA_GRAPHICS_COLOR_HPP
#define STANA_GRAPHICS_COLOR_HPP
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>

namespace stana::rendering::detail {
inline constexpr unsigned halfFractionBits = 10;
inline constexpr unsigned halfExponentMask = 31;
inline constexpr unsigned halfSignShift = 15;
inline constexpr unsigned byteBits = 8;
inline constexpr unsigned halfBias = 15;
inline constexpr double halfFractionScale = 1024;
inline constexpr double decodeThreshold = 0.04045;
inline constexpr double linearThreshold = 0.0031308;
inline constexpr double transferSlope = 12.92;
inline constexpr double transferOffset = 0.055;
inline constexpr double transferScale = 1.055;
inline constexpr double transferPower = 2.4;
inline constexpr double mappingKnee = 0.75;

[[nodiscard]] inline auto decodeSRGB(double value) -> double {
	const double magnitude = std::abs(value);
	return std::copysign(
		magnitude <= decodeThreshold
			? magnitude / transferSlope
			: std::pow((magnitude + transferOffset) / transferScale, transferPower),
		value
	);
}

[[nodiscard]] inline auto encodeSRGB(double value) -> double {
	const double magnitude = std::abs(value);
	return std::copysign(
		magnitude <= linearThreshold
			? magnitude * transferSlope
			: transferScale * std::pow(magnitude, 1 / transferPower) - transferOffset,
		value
	);
}

[[nodiscard]] inline auto byteAt(std::span<const std::byte> bytes, std::size_t offset)
	-> std::byte {
	if (offset >= bytes.size()) {
		throw std::out_of_range("Color payload offset");
	}
	return bytes.subspan(offset, 1).front();
}

/// @brief Binary16 payloads have an explicitly little-endian byte contract on all hosts.
[[nodiscard]] inline auto halfAt(std::span<const std::byte> bytes, std::size_t offset) -> double {
	const auto bits = static_cast<std::uint16_t>(
		std::to_integer<unsigned>(byteAt(bytes, offset)) |
		(std::to_integer<unsigned>(byteAt(bytes, offset + 1)) << byteBits)
	);
	const unsigned exponent = (bits >> halfFractionBits) & halfExponentMask;
	const unsigned fraction = bits & ((1U << halfFractionBits) - 1U);
	const double sign = (bits >> halfSignShift) != 0 ? -1.0 : 1.0;
	if (exponent == halfExponentMask) {
		return std::numeric_limits<double>::infinity() * sign;
	}
	if (exponent == 0) {
		return sign *
			std::ldexp(
				static_cast<double>(fraction) / halfFractionScale,
				1 - static_cast<int>(halfBias)
			);
	}
	return sign *
		std::ldexp(
			1 + static_cast<double>(fraction) / halfFractionScale,
			static_cast<int>(exponent) - static_cast<int>(halfBias)
		);
}

/// @brief IEEE binary16 nearest-even conversion, independent of the host rounding mode.
[[nodiscard]] inline auto toHalf(double value) -> std::optional<std::uint16_t> {
	constexpr double maximum = 65504;
	constexpr int minimumExponent = -14;
	constexpr int maximumExponent = 15;
	constexpr int subnormalShift = 24;
	const auto sign = static_cast<std::uint16_t>(std::signbit(value) ? (1U << halfSignShift) : 0U);
	const double magnitude = std::abs(value);
	if (!std::isfinite(value) || magnitude > maximum) {
		return {};
	}
	if (std::is_eq(magnitude <=> 0.0)) {
		return sign;
	}
	const auto nearestEven = [](double scaled) -> std::uint32_t {
		const auto low = static_cast<std::uint32_t>(std::floor(scaled));
		const double fraction = scaled - low;
		constexpr double midpoint = 0.5;
		return low +
			static_cast<std::uint32_t>(
				fraction > midpoint || (std::is_eq(fraction <=> midpoint) && low % 2 != 0)
			);
	};
	if (magnitude < std::ldexp(1.0, minimumExponent)) {
		return static_cast<std::uint16_t>(
			sign | nearestEven(std::ldexp(magnitude, subnormalShift))
		);
	}
	int exponent = 0;
	const double fraction = std::frexp(magnitude, &exponent);
	auto mantissa = nearestEven((fraction * 2 - 1) * halfFractionScale);
	--exponent;
	if (mantissa == static_cast<std::uint32_t>(halfFractionScale)) {
		mantissa = 0;
		++exponent;
	}
	if (exponent > maximumExponent) {
		return {};
	}
	const auto encodedExponent = static_cast<std::uint32_t>(exponent + static_cast<int>(halfBias));
	return static_cast<std::uint16_t>(sign | (encodedExponent << halfFractionBits) | mantissa);
}

[[nodiscard]] inline auto displayMap(std::array<double, 4> color, double headroom)
	-> std::array<double, 4> {
	const double alpha = color.at(3);
	if (alpha <= 0) {
		return {};
	}
	const double level =
		std::max({0.0, color.at(0) / alpha, color.at(1) / alpha, color.at(2) / alpha});
	const double high = std::isfinite(headroom) ? std::max(1.0, headroom) : 1.0;
	const double knee = std::min(1.0, mappingKnee * high);
	if (level > knee) {
		const double factor =
			(knee + (high - knee) * (level - knee) / ((level - knee) + (high - knee))) / level;
		for (std::size_t channel = 0; channel < 3; ++channel) {
			color.at(channel) *= factor;
		}
	}
	return color;
}
} // namespace stana::rendering::detail
#endif
