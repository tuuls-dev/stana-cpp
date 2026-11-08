module;

#include <cmath>
#include <compare>
#include <optional>

export module stana:symbol_units;
import :primitives;

export namespace stana::symbol {

/// @brief Scalar distance or coordinate in symbol viewBox / asset space.
/// Distinct from logical Points; conversion is explicit at presentation boundaries.
struct AssetUnits {
	double value_ = 0;

	constexpr AssetUnits() noexcept = default;

	explicit constexpr AssetUnits(double value) noexcept : value_(value) {}

	[[nodiscard]] constexpr auto value() const noexcept -> double { return value_; }

	auto operator<=>(const AssetUnits &) const = default;

	[[nodiscard]] constexpr auto operator==(const AssetUnits &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

[[nodiscard]] constexpr auto operator+(AssetUnits left, AssetUnits right) noexcept -> AssetUnits {
	return AssetUnits{left.value() + right.value()};
}

[[nodiscard]] constexpr auto operator-(AssetUnits left, AssetUnits right) noexcept -> AssetUnits {
	return AssetUnits{left.value() - right.value()};
}

[[nodiscard]] constexpr auto operator-(AssetUnits value) noexcept -> AssetUnits {
	return AssetUnits{-value.value()};
}

[[nodiscard]] constexpr auto operator*(AssetUnits left, double right) noexcept -> AssetUnits {
	return AssetUnits{left.value() * right};
}

[[nodiscard]] constexpr auto operator*(double left, AssetUnits right) noexcept -> AssetUnits {
	return AssetUnits{left * right.value()};
}

[[nodiscard]] constexpr auto operator/(AssetUnits left, double right) noexcept -> AssetUnits {
	return AssetUnits{left.value() / right};
}

[[nodiscard]] constexpr auto operator/(AssetUnits left, AssetUnits right) noexcept -> Ratio {
	return Ratio{left.value() / right.value()};
}

constexpr auto operator+=(AssetUnits &left, AssetUnits right) noexcept -> AssetUnits & {
	left = left + right;
	return left;
}

constexpr auto operator-=(AssetUnits &left, AssetUnits right) noexcept -> AssetUnits & {
	left = left - right;
	return left;
}

/// @brief Location in asset/viewBox coordinates (top-left, y-down within the asset).
struct AssetPoint {
	AssetUnits x{};
	AssetUnits y{};
	auto operator<=>(const AssetPoint &) const = default;

	[[nodiscard]] constexpr auto operator==(const AssetPoint &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Extent in asset/viewBox coordinates.
struct AssetSize {
	AssetUnits width{};
	AssetUnits height{};
	auto operator<=>(const AssetSize &) const = default;

	[[nodiscard]] constexpr auto operator==(const AssetSize &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Origin and extent in asset/viewBox coordinates.
struct AssetRect {
	AssetPoint origin{};
	AssetSize size{};
	auto operator<=>(const AssetRect &) const = default;

	[[nodiscard]] constexpr auto operator==(const AssetRect &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Affine mapping in asset space. Linear coeffs are unitless ScaleFactor values;
/// translations are AssetUnits. Does not accept logical Points or AffineTransform.
struct AssetTransform {
	ScaleFactor a{1};
	ScaleFactor b{0};
	ScaleFactor c{0};
	ScaleFactor d{1};
	AssetUnits tx{};
	AssetUnits ty{};

	[[nodiscard]] static constexpr auto identity() noexcept -> AssetTransform { return {}; }

	[[nodiscard]] static constexpr auto translation(AssetUnits x, AssetUnits y) noexcept
		-> AssetTransform {
		return {.tx = x, .ty = y};
	}

	[[nodiscard]] static constexpr auto scale(ScaleFactor x, ScaleFactor y) noexcept
		-> AssetTransform {
		return {.a = x, .d = y};
	}

	[[nodiscard]] static auto rotation(Angle angle) noexcept -> AssetTransform {
		const double radians = angle.radians();
		return {
			.a = ScaleFactor{std::cos(radians)},
			.b = ScaleFactor{std::sin(radians)},
			.c = ScaleFactor{-std::sin(radians)},
			.d = ScaleFactor{std::cos(radians)}
		};
	}

	[[nodiscard]] constexpr auto concatenating(const AssetTransform &other) const noexcept
		-> AssetTransform {
		return {
			.a = ScaleFactor{a.value() * other.a.value() + c.value() * other.b.value()},
			.b = ScaleFactor{b.value() * other.a.value() + d.value() * other.b.value()},
			.c = ScaleFactor{a.value() * other.c.value() + c.value() * other.d.value()},
			.d = ScaleFactor{b.value() * other.c.value() + d.value() * other.d.value()},
			.tx =
				AssetUnits{
					a.value() * other.tx.value() + c.value() * other.ty.value() + tx.value()
				},
			.ty =
				AssetUnits{b.value() * other.tx.value() + d.value() * other.ty.value() + ty.value()}
		};
	}

	[[nodiscard]] constexpr auto apply(AssetPoint point) const noexcept -> AssetPoint {
		return {
			.x = AssetUnits{a.value() * point.x.value() + c.value() * point.y.value() + tx.value()},
			.y = AssetUnits{b.value() * point.x.value() + d.value() * point.y.value() + ty.value()}
		};
	}

	[[nodiscard]] auto maximumScale() const noexcept -> ScaleFactor {
		const AffineTransform logical{
			.a = a,
			.b = b,
			.c = c,
			.d = d,
			.tx = Points{tx.value()},
			.ty = Points{ty.value()}
		};
		return logical.maximumScale();
	}

	[[nodiscard]] auto inverse() const noexcept -> std::optional<AssetTransform> {
		const AffineTransform logical{
			.a = a,
			.b = b,
			.c = c,
			.d = d,
			.tx = Points{tx.value()},
			.ty = Points{ty.value()}
		};
		const auto inverted = logical.inverse();
		if (!inverted) {
			return std::nullopt;
		}
		return AssetTransform{
			.a = inverted->a,
			.b = inverted->b,
			.c = inverted->c,
			.d = inverted->d,
			.tx = AssetUnits{inverted->tx.value()},
			.ty = AssetUnits{inverted->ty.value()}
		};
	}

	auto operator<=>(const AssetTransform &) const = default;

	[[nodiscard]] constexpr auto operator==(const AssetTransform &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Explicit conversion from asset units into logical points using a uniform scale.
[[nodiscard]] constexpr auto toLogical(AssetUnits value, ScaleFactor scale) noexcept -> Points {
	return Points{value.value() * scale.value()};
}

[[nodiscard]] constexpr auto toLogical(AssetPoint point, ScaleFactor scale) noexcept -> Point {
	return {.x = toLogical(point.x, scale), .y = toLogical(point.y, scale)};
}

[[nodiscard]] constexpr auto toLogical(AssetSize size, ScaleFactor scale) noexcept -> Size {
	return {.width = toLogical(size.width, scale), .height = toLogical(size.height, scale)};
}

[[nodiscard]] constexpr auto toLogical(AssetRect rect, ScaleFactor scale) noexcept -> Rect {
	return {.origin = toLogical(rect.origin, scale), .size = toLogical(rect.size, scale)};
}

[[nodiscard]] constexpr auto toLogical(const AssetTransform &transform, ScaleFactor scale) noexcept
	-> AffineTransform {
	return {
		.a = transform.a,
		.b = transform.b,
		.c = transform.c,
		.d = transform.d,
		.tx = toLogical(transform.tx, scale),
		.ty = toLogical(transform.ty, scale)
	};
}

/// @brief Explicit conversion from logical points into asset units using a uniform scale.
[[nodiscard]] constexpr auto toAsset(Points value, ScaleFactor scale) noexcept -> AssetUnits {
	return AssetUnits{value.value() / scale.value()};
}

[[nodiscard]] constexpr auto toAsset(Point point, ScaleFactor scale) noexcept -> AssetPoint {
	return {.x = toAsset(point.x, scale), .y = toAsset(point.y, scale)};
}

} // namespace stana::symbol
