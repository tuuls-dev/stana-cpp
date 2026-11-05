#ifndef STANA_GRAPHICS_COLOR_HPP
#define STANA_GRAPHICS_COLOR_HPP
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

namespace stana::graphics_detail {
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
} // namespace stana::graphics_detail
#endif
