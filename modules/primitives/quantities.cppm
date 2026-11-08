module;

#include <chrono>
#include <cmath>
#include <compare>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>

export module stana:primitives_quantities;

export namespace stana {

/// @brief Logical distance or coordinate in top-left, y-down layout space.
/// Not a physical device pixel. Signed so coordinates and shadow offsets may be
/// negative. Extract the scalar with value(); construct explicitly from runtime
/// numbers. Literals use _pt. Historical _px still builds Length::points and must
/// not be confused with PixelDistance.
struct Points {
	double value_ = 0;

	constexpr Points() noexcept = default;
	explicit constexpr Points(double value) noexcept : value_(value) {}

	[[nodiscard]] constexpr auto value() const noexcept -> double {
		return value_;
	}

	auto operator<=>(const Points &) const = default;
	[[nodiscard]] constexpr auto operator==(const Points &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

[[nodiscard]] constexpr auto operator+(Points left, Points right) noexcept -> Points {
	return Points{left.value() + right.value()};
}
[[nodiscard]] constexpr auto operator-(Points left, Points right) noexcept -> Points {
	return Points{left.value() - right.value()};
}
[[nodiscard]] constexpr auto operator-(Points value) noexcept -> Points {
	return Points{-value.value()};
}
[[nodiscard]] constexpr auto operator*(Points left, double right) noexcept -> Points {
	return Points{left.value() * right};
}
[[nodiscard]] constexpr auto operator*(double left, Points right) noexcept -> Points {
	return Points{left * right.value()};
}
[[nodiscard]] constexpr auto operator/(Points left, double right) noexcept -> Points {
	return Points{left.value() / right};
}
constexpr auto operator+=(Points &left, Points right) noexcept -> Points & {
	left = left + right;
	return left;
}
constexpr auto operator-=(Points &left, Points right) noexcept -> Points & {
	left = left - right;
	return left;
}
constexpr auto operator*=(Points &left, double right) noexcept -> Points & {
	left = left * right;
	return left;
}
constexpr auto operator/=(Points &left, double right) noexcept -> Points & {
	left = left / right;
	return left;
}

/// @brief Unitless percentage payload (100 means full relative extent).
/// Does not interpret opacity or layout by itself; callers convert with
/// Length::percent or Opacity::percent.
struct Percentage {
	double value_ = 0;

	constexpr Percentage() noexcept = default;
	explicit constexpr Percentage(double value) noexcept : value_(value) {}

	[[nodiscard]] constexpr auto value() const noexcept -> double {
		return value_;
	}

	auto operator<=>(const Percentage &) const = default;
	[[nodiscard]] constexpr auto operator==(const Percentage &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Fractional opacity in [0, 1]; zero is transparent and one is opaque.
/// Construction does not clamp. Runtime validation rejects values outside the
/// domain. Opacity::percent converts Percentage and diagnoses out-of-range input.
struct Opacity {
	double value_ = 1;

	constexpr Opacity() noexcept = default;
	explicit constexpr Opacity(double value) noexcept : value_(value) {}

	[[nodiscard]] constexpr auto value() const noexcept -> double {
		return value_;
	}

	/// @brief Builds opacity from a percentage of full opacity.
	/// @param value Percentage where 100_pct is opaque. Values outside [0, 100]
	/// throw std::invalid_argument; there is no silent clamp.
	[[nodiscard]] static auto percent(Percentage value) -> Opacity {
		constexpr double full = 100;
		if (!std::isfinite(value.value()) || value.value() < 0 || value.value() > full) {
			throw std::invalid_argument("Opacity percentage out of range");
		}
		return Opacity{value.value() / full};
	}

	auto operator<=>(const Opacity &) const = default;
	[[nodiscard]] constexpr auto operator==(const Opacity &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Angle stored as radians; construct with _rad or _deg literals.
/// AffineTransform::rotation preserves clockwise/y-down behavior.
struct Angle {
	double radians_ = 0;

	constexpr Angle() noexcept = default;
	explicit constexpr Angle(double radians) noexcept : radians_(radians) {}

	[[nodiscard]] constexpr auto radians() const noexcept -> double {
		return radians_;
	}

	[[nodiscard]] constexpr auto degrees() const noexcept -> double {
		constexpr double halfTurnDegrees = 180;
		return radians_ * halfTurnDegrees / std::numbers::pi_v<double>;
	}

	[[nodiscard]] static constexpr auto fromRadians(double value) noexcept -> Angle {
		return Angle{value};
	}

	[[nodiscard]] static constexpr auto fromDegrees(double value) noexcept -> Angle {
		constexpr double halfTurnDegrees = 180;
		return Angle{value * std::numbers::pi_v<double> / halfTurnDegrees};
	}

	auto operator<=>(const Angle &) const = default;
	[[nodiscard]] constexpr auto operator==(const Angle &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

[[nodiscard]] constexpr auto operator+(Angle left, Angle right) noexcept -> Angle {
	return Angle{left.radians() + right.radians()};
}
[[nodiscard]] constexpr auto operator-(Angle left, Angle right) noexcept -> Angle {
	return Angle{left.radians() - right.radians()};
}
[[nodiscard]] constexpr auto operator-(Angle value) noexcept -> Angle {
	return Angle{-value.radians()};
}
[[nodiscard]] constexpr auto operator*(Angle left, double right) noexcept -> Angle {
	return Angle{left.radians() * right};
}
[[nodiscard]] constexpr auto operator*(double left, Angle right) noexcept -> Angle {
	return Angle{left * right.radians()};
}

/// @brief System-font weight on the native weight scale used by Font.
/// Zero is regular. Named factories preserve that scale; numeric construction is
/// explicit and does not remap CSS-style 100..900 weights.
struct FontWeight {
	double value_ = 0;

	constexpr FontWeight() noexcept = default;
	explicit constexpr FontWeight(double value) noexcept : value_(value) {}

	[[nodiscard]] constexpr auto value() const noexcept -> double {
		return value_;
	}

	[[nodiscard]] static constexpr auto thin() noexcept -> FontWeight {
		constexpr double thinWeight = -0.8;
		return FontWeight{thinWeight};
	}
	[[nodiscard]] static constexpr auto ultraLight() noexcept -> FontWeight {
		constexpr double ultraLightWeight = -0.6;
		return FontWeight{ultraLightWeight};
	}
	[[nodiscard]] static constexpr auto light() noexcept -> FontWeight {
		constexpr double lightWeight = -0.4;
		return FontWeight{lightWeight};
	}
	[[nodiscard]] static constexpr auto regular() noexcept -> FontWeight {
		return FontWeight{0};
	}
	[[nodiscard]] static constexpr auto medium() noexcept -> FontWeight {
		constexpr double mediumWeight = 0.23;
		return FontWeight{mediumWeight};
	}
	[[nodiscard]] static constexpr auto semibold() noexcept -> FontWeight {
		constexpr double semiboldWeight = 0.3;
		return FontWeight{semiboldWeight};
	}
	[[nodiscard]] static constexpr auto bold() noexcept -> FontWeight {
		constexpr double boldWeight = 0.4;
		return FontWeight{boldWeight};
	}
	[[nodiscard]] static constexpr auto heavy() noexcept -> FontWeight {
		constexpr double heavyWeight = 0.56;
		return FontWeight{heavyWeight};
	}
	[[nodiscard]] static constexpr auto black() noexcept -> FontWeight {
		constexpr double blackWeight = 0.62;
		return FontWeight{blackWeight};
	}

	auto operator<=>(const FontWeight &) const = default;
	[[nodiscard]] constexpr auto operator==(const FontWeight &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Signed dimensionless ratio of two quantities with the same unit.
struct Ratio {
	double value_ = 0;

	constexpr Ratio() noexcept = default;
	explicit constexpr Ratio(double value) noexcept : value_(value) {}

	[[nodiscard]] constexpr auto value() const noexcept -> double {
		return value_;
	}

	auto operator<=>(const Ratio &) const = default;
	[[nodiscard]] constexpr auto operator==(const Ratio &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

[[nodiscard]] constexpr auto operator/(Points left, Points right) noexcept -> Ratio {
	return Ratio{left.value() / right.value()};
}

/// @brief Multiplicative scale factor applied to geometry or symbol effects.
struct ScaleFactor {
	double value_ = 1;

	constexpr ScaleFactor() noexcept = default;
	explicit constexpr ScaleFactor(double value) noexcept : value_(value) {}

	[[nodiscard]] constexpr auto value() const noexcept -> double {
		return value_;
	}

	auto operator<=>(const ScaleFactor &) const = default;
	[[nodiscard]] constexpr auto operator==(const ScaleFactor &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

[[nodiscard]] constexpr auto operator*(Points left, ScaleFactor right) noexcept -> Points {
	return Points{left.value() * right.value()};
}
[[nodiscard]] constexpr auto operator*(ScaleFactor left, Points right) noexcept -> Points {
	return Points{left.value() * right.value()};
}
constexpr auto operator*=(Points &left, ScaleFactor right) noexcept -> Points & {
	left = left * right;
	return left;
}
[[nodiscard]] constexpr auto operator*(ScaleFactor left, ScaleFactor right) noexcept -> ScaleFactor {
	return ScaleFactor{left.value() * right.value()};
}
constexpr auto operator*=(ScaleFactor &left, ScaleFactor right) noexcept -> ScaleFactor & {
	left = left * right;
	return left;
}

/// @brief Interpolation or animation progress in [0, 1] unless a caller documents
/// otherwise. Distinct from Ratio and Opacity; validation is caller-owned.
struct Progress {
	double value_ = 0;

	constexpr Progress() noexcept = default;
	explicit constexpr Progress(double value) noexcept : value_(value) {}

	[[nodiscard]] constexpr auto value() const noexcept -> double {
		return value_;
	}

	auto operator<=>(const Progress &) const = default;
	[[nodiscard]] constexpr auto operator==(const Progress &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Encoded SDR color channel in [0, 1]; not scene-linear and not
/// premultiplied. Runtime validators reject values outside the domain.
struct SdrChannel {
	double value_ = 0;

	constexpr SdrChannel() noexcept = default;
	explicit constexpr SdrChannel(double value) noexcept : value_(value) {}

	[[nodiscard]] constexpr auto value() const noexcept -> double {
		return value_;
	}

	auto operator<=>(const SdrChannel &) const = default;
	[[nodiscard]] constexpr auto operator==(const SdrChannel &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Extended scene-linear color channel; may exceed [0, 1].
struct LinearChannel {
	double value_ = 0;

	constexpr LinearChannel() noexcept = default;
	explicit constexpr LinearChannel(double value) noexcept : value_(value) {}

	[[nodiscard]] constexpr auto value() const noexcept -> double {
		return value_;
	}

	auto operator<=>(const LinearChannel &) const = default;
	[[nodiscard]] constexpr auto operator==(const LinearChannel &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Fractional physical-pixel distance after backing-scale conversion.
/// Distinct from logical Points and from historical _px literals.
struct PixelDistance {
	double value_ = 0;

	constexpr PixelDistance() noexcept = default;
	explicit constexpr PixelDistance(double value) noexcept : value_(value) {}

	[[nodiscard]] constexpr auto value() const noexcept -> double {
		return value_;
	}

	auto operator<=>(const PixelDistance &) const = default;
	[[nodiscard]] constexpr auto operator==(const PixelDistance &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Integer physical-pixel count for GPU textures and similar resources.
struct PixelCount {
	std::uint32_t value_ = 0;

	constexpr PixelCount() noexcept = default;
	explicit constexpr PixelCount(std::uint32_t value) noexcept : value_(value) {}

	[[nodiscard]] constexpr auto value() const noexcept -> std::uint32_t {
		return value_;
	}

	auto operator<=>(const PixelCount &) const = default;
	[[nodiscard]] constexpr auto operator==(const PixelCount &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief GPU texture/program source peak in the extended-linear domain.
struct SourcePeak {
	static constexpr double defaultPeak = 16;
	double value_ = defaultPeak;

	constexpr SourcePeak() noexcept = default;
	explicit constexpr SourcePeak(double value) noexcept : value_(value) {}

	[[nodiscard]] constexpr auto value() const noexcept -> double {
		return value_;
	}

	auto operator<=>(const SourcePeak &) const = default;
	[[nodiscard]] constexpr auto operator==(const SourcePeak &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief GPU sampling radius in logical points for program descriptors.
struct SamplingRadius {
	double value_ = 0;

	constexpr SamplingRadius() noexcept = default;
	explicit constexpr SamplingRadius(double value) noexcept : value_(value) {}

	[[nodiscard]] constexpr auto value() const noexcept -> double {
		return value_;
	}

	auto operator<=>(const SamplingRadius &) const = default;
	[[nodiscard]] constexpr auto operator==(const SamplingRadius &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Strong integer identifier for a GPU binding slot.
struct BindingIndex {
	std::uint32_t value_ = 0;

	constexpr BindingIndex() noexcept = default;
	explicit constexpr BindingIndex(std::uint32_t value) noexcept : value_(value) {}

	[[nodiscard]] constexpr auto value() const noexcept -> std::uint32_t {
		return value_;
	}

	auto operator<=>(const BindingIndex &) const = default;
	[[nodiscard]] constexpr auto operator==(const BindingIndex &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Display-link / frame callback clock. Not interchangeable with
/// PresentationClock or wall-clock system_clock.
struct FrameClock {
	using rep = double;
	using period = std::ratio<1>;
	using duration = std::chrono::duration<rep, period>;
	using time_point = std::chrono::time_point<FrameClock, duration>;
	static constexpr bool is_steady = true;

	[[nodiscard]] static auto now() noexcept -> time_point {
		return time_point{};
	}
};

/// @brief Host presentation / compositor evidence clock. Distinct from FrameClock.
struct PresentationClock {
	using rep = double;
	using period = std::ratio<1>;
	using duration = std::chrono::duration<rep, period>;
	using time_point = std::chrono::time_point<PresentationClock, duration>;
	static constexpr bool is_steady = true;

	[[nodiscard]] static auto now() noexcept -> time_point {
		return time_point{};
	}
};

using FrameTimePoint = FrameClock::time_point;
using FrameDuration = FrameClock::duration;
using PresentationTimePoint = PresentationClock::time_point;
using PresentationDuration = PresentationClock::duration;

inline namespace literals {

[[nodiscard]] constexpr auto operator""_pt(long double value) noexcept -> Points {
	return Points{static_cast<double>(value)};
}
[[nodiscard]] constexpr auto operator""_pt(unsigned long long value) noexcept -> Points {
	return Points{static_cast<double>(value)};
}

[[nodiscard]] constexpr auto operator""_pct(long double value) noexcept -> Percentage {
	return Percentage{static_cast<double>(value)};
}
[[nodiscard]] constexpr auto operator""_pct(unsigned long long value) noexcept -> Percentage {
	return Percentage{static_cast<double>(value)};
}

[[nodiscard]] constexpr auto operator""_rad(long double value) noexcept -> Angle {
	return Angle::fromRadians(static_cast<double>(value));
}
[[nodiscard]] constexpr auto operator""_rad(unsigned long long value) noexcept -> Angle {
	return Angle::fromRadians(static_cast<double>(value));
}

[[nodiscard]] constexpr auto operator""_deg(long double value) noexcept -> Angle {
	return Angle::fromDegrees(static_cast<double>(value));
}
[[nodiscard]] constexpr auto operator""_deg(unsigned long long value) noexcept -> Angle {
	return Angle::fromDegrees(static_cast<double>(value));
}

} // namespace literals

} // namespace stana

namespace stana::primitives::detail {

[[nodiscard]] consteval auto hexNibble(char digit) -> unsigned {
	constexpr unsigned hexBase = 10;
	if (digit >= '0' && digit <= '9') {
		return static_cast<unsigned>(digit - '0');
	}
	if (digit >= 'a' && digit <= 'f') {
		return static_cast<unsigned>(digit - 'a') + hexBase;
	}
	if (digit >= 'A' && digit <= 'F') {
		return static_cast<unsigned>(digit - 'A') + hexBase;
	}
	throw std::invalid_argument("Malformed color literal");
}

[[nodiscard]] consteval auto hexByte(char high, char low) -> double {
	constexpr double scale = 255;
	return static_cast<double>((hexNibble(high) << 4U) | hexNibble(low)) / scale;
}

[[nodiscard]] consteval auto parseHexColor(std::string_view text)
	-> std::tuple<double, double, double, double> {
	constexpr std::size_t rgbLength = 7;
	constexpr std::size_t rgbaLength = 9;
	if (text.size() != rgbLength && text.size() != rgbaLength) {
		throw std::invalid_argument("Malformed color literal");
	}
	if (text.front() != '#') {
		throw std::invalid_argument("Malformed color literal");
	}
	const double red = hexByte(text.at(1), text.at(2));
	const double green = hexByte(text.at(3), text.at(4));
	const double blue = hexByte(text.at(5), text.at(6));
	const double alpha = text.size() == rgbaLength ? hexByte(text.at(7), text.at(8)) : 1.0;
	return {red, green, blue, alpha};
}

} // namespace stana::primitives::detail
