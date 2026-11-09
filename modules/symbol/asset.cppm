module;

#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module stana:symbol_asset;
export import :symbol_units;
import :primitives;

export namespace stana::symbol {
namespace detail {
inline constexpr double numberLimit = 1e9;
inline constexpr int nestingLimit = 64;
inline constexpr int elementLimit = 4096;
inline constexpr int commandLimit = 65536;
inline constexpr std::uint32_t bridgeVersion = 1;
inline constexpr double fullTurn = 2 * std::numbers::pi;
inline constexpr int storageBaseBytes = 128;
inline constexpr int storageElementBytes = 256;
inline constexpr int storageCommandBytes = 128;
inline constexpr double minimumMiterLimit = 1;

[[nodiscard]] constexpr auto normalized(double value) noexcept -> double {
	return std::is_eq(value <=> 0.0) ? 0.0 : value;
}

/// std::abs(double) is not a constant expression on the macOS SDK libc++.
[[nodiscard]] constexpr auto magnitude(double value) noexcept -> double {
	return value < 0 ? -value : value;
}

[[nodiscard]] constexpr auto finiteLimited(double value) noexcept -> bool {
	return std::isfinite(value) && magnitude(value) <= numberLimit;
}
} // namespace detail

/// @brief Solid paint, template tint, or no paint. Literal colors are not theme-dependent.
struct NoPaint {
	auto operator<=>(const NoPaint &) const = default;
	[[nodiscard]] auto operator==(const NoPaint &) const -> bool = default;
};

struct CurrentColor {
	auto operator<=>(const CurrentColor &) const = default;
	[[nodiscard]] auto operator==(const CurrentColor &) const -> bool = default;
};

struct RgbaColor {
	SdrChannel red{};
	SdrChannel green{};
	SdrChannel blue{};
	Opacity alpha{1};
	auto operator<=>(const RgbaColor &) const = default;

	[[nodiscard]] auto operator==(const RgbaColor &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

using Paint = std::variant<NoPaint, CurrentColor, RgbaColor>;

/// @brief How a filled path covers its interior.
enum class FillRule : std::uint8_t {
	nonzero, ///< Non-zero winding, the SVG default.
	evenodd	 ///< Even-odd winding.
};

/// @brief Stroke cap at an open path end.
enum class LineCap : std::uint8_t { butt, round, square };

/// @brief Stroke join between path segments.
enum class LineJoin : std::uint8_t { miter, round, bevel };

/// @brief One normalized path verb in viewBox / asset coordinates.
struct MoveTo {
	AssetUnits x{};
	AssetUnits y{};
	auto operator<=>(const MoveTo &) const = default;

	[[nodiscard]] auto operator==(const MoveTo &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

struct LineTo {
	AssetUnits x{};
	AssetUnits y{};
	auto operator<=>(const LineTo &) const = default;

	[[nodiscard]] auto operator==(const LineTo &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

struct QuadraticTo {
	AssetUnits controlX{};
	AssetUnits controlY{};
	AssetUnits x{};
	AssetUnits y{};
	auto operator<=>(const QuadraticTo &) const = default;

	[[nodiscard]] auto operator==(const QuadraticTo &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

struct CubicTo {
	AssetUnits control1X{};
	AssetUnits control1Y{};
	AssetUnits control2X{};
	AssetUnits control2Y{};
	AssetUnits x{};
	AssetUnits y{};
	auto operator<=>(const CubicTo &) const = default;

	[[nodiscard]] auto operator==(const CubicTo &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

struct ArcTo {
	AssetUnits centerX{};
	AssetUnits centerY{};
	AssetUnits radiusX{};
	AssetUnits radiusY{};
	Angle rotation{};
	Angle startAngle{};
	Angle sweepAngle{};
	auto operator<=>(const ArcTo &) const = default;

	[[nodiscard]] auto operator==(const ArcTo &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

struct ClosePath {
	auto operator<=>(const ClosePath &) const = default;
	[[nodiscard]] auto operator==(const ClosePath &) const -> bool = default;
};

using PathCommand = std::variant<MoveTo, LineTo, QuadraticTo, CubicTo, ArcTo, ClosePath>;

/// @brief One retained vector element. Groups nest; paths own their commands.
///
/// The value is a variant so compiled artwork stays constexpr-friendly. Equality is
/// structural. Comments, identifiers, and source text are not part of the value.
struct Element {
	/// @brief A transformed, opacity-composited child list.
	struct Group {
		AssetTransform transform{};
		Opacity opacity{1};
		std::vector<Element> children;

		constexpr Group();
		constexpr Group(const Group &);
		constexpr Group(Group &&) noexcept;
		constexpr auto operator=(const Group &) -> Group &;
		constexpr auto operator=(Group &&) noexcept -> Group &;
		constexpr ~Group();
		[[nodiscard]] auto operator==(const Group &) const -> bool;
	};

	/// @brief One filled and/or stroked path in asset/viewBox coordinates.
	struct Path {
		std::vector<PathCommand> commands;
		AssetTransform transform{};
		Paint fill = RgbaColor{};
		FillRule fillRule = FillRule::nonzero;
		Opacity fillOpacity{1};
		Paint stroke = NoPaint{};
		Opacity strokeOpacity{1};
		AssetUnits strokeWidth{1};
		LineCap lineCap = LineCap::butt;
		LineJoin lineJoin = LineJoin::miter;
		Ratio miterLimit{4};
		Opacity opacity{1};
		auto operator<=>(const Path &) const = default;

		[[nodiscard]] auto operator==(const Path &other) const -> bool {
			return std::is_eq(*this <=> other);
		}
	};

	std::variant<Group, Path> value;
	[[nodiscard]] auto operator==(const Element &) const -> bool;
};

constexpr Element::Group::Group() = default;
constexpr Element::Group::Group(const Group &) = default;
constexpr Element::Group::Group(Group &&) noexcept = default;
constexpr auto Element::Group::operator=(const Group &) -> Group & = default;
constexpr auto Element::Group::operator=(Group &&) noexcept -> Group & = default;
constexpr Element::Group::~Group() = default;

auto Element::Group::operator==(const Group &other) const -> bool {
	return transform == other.transform && std::is_eq(opacity <=> other.opacity) &&
		children == other.children;
}

auto Element::operator==(const Element &other) const -> bool {
	return value == other.value;
}

class Asset;

namespace detail {
/// @brief Scalars for an asset whose geometry is loaded from static storage.
struct ViewHeader {
	AssetRect viewBox{};
	std::size_t estimatedBytes = 0;
	bool mirrorsInRTL = false;
	bool hasStroke = false;
};

[[nodiscard]] constexpr auto adoptView(ViewHeader header, const std::vector<Element> &(*geometry)())
	-> Asset;
} // namespace detail

/// @brief Immutable compiled vector artwork. Produce it with compile() or compiled().
///
/// There is no runtime SVG, file, or URL constructor. Equality covers normalized
/// geometry, paints, the viewBox, and RTL mirroring. A default asset is empty and
/// rejected by mounting until a validated value replaces it.
class Asset {
  public:
	/// @brief Accepts bridge version 1 and enforces the compiled-artwork budgets.
	/// @throws std::invalid_argument when the version, viewBox, nesting, counts, or numbers
	/// fail. Negative zero is normalized to zero.
	[[nodiscard]] static constexpr auto compiled(
		std::uint32_t version,
		AssetRect viewBox,
		bool mirrorsInRTL,
		std::vector<Element> elements
	) -> Asset;

	[[nodiscard]] constexpr auto viewBox() const noexcept -> AssetRect { return viewBox_; }

	[[nodiscard]] constexpr auto mirrorsInRTL() const noexcept -> bool { return mirrorsInRTL_; }

	/// @brief Geometry in document order. compile() loads it from static storage on first use.
	[[nodiscard]] constexpr auto elements() const -> const std::vector<Element> & {
		if (!std::is_constant_evaluated() && geometry_ != nullptr) {
			return geometry_();
		}
		return elements_;
	}

	[[nodiscard]] constexpr auto hasStroke() const noexcept -> bool { return hasStroke_; }

	[[nodiscard]] constexpr auto estimatedBytes() const noexcept -> std::size_t {
		return estimatedBytes_;
	}

	/// @brief True after compiled() or compile() accepts the value. A default asset is not valid.
	[[nodiscard]] constexpr auto valid() const noexcept -> bool { return valid_; }

	[[nodiscard]] auto operator==(const Asset &other) const -> bool {
		return valid_ == other.valid_ && viewBox_ == other.viewBox_ &&
			mirrorsInRTL_ == other.mirrorsInRTL_ && elements() == other.elements();
	}

  private:
	friend constexpr auto
	detail::adoptView(detail::ViewHeader header, const std::vector<Element> &(*geometry)())
		-> Asset;

	AssetRect viewBox_{};
	bool mirrorsInRTL_ = false;
	std::vector<Element> elements_{};
	const std::vector<Element> &(*geometry_)() = nullptr;
	std::size_t estimatedBytes_ = 0;
	bool hasStroke_ = false;
	bool valid_ = false;
};

namespace detail {
constexpr auto adoptView(ViewHeader header, const std::vector<Element> &(*geometry)()) -> Asset {
	Asset asset;
	asset.viewBox_ = header.viewBox;
	asset.mirrorsInRTL_ = header.mirrorsInRTL;
	asset.hasStroke_ = header.hasStroke;
	asset.estimatedBytes_ = header.estimatedBytes;
	asset.geometry_ = geometry;
	asset.valid_ = true;
	return asset;
}

[[noreturn]] inline constexpr void reject(const char *message) {
	throw std::invalid_argument(message);
}

inline constexpr void requireNumber(double value) {
	if (!finiteLimited(value)) {
		reject("Stana: invalid compiled symbol number");
	}
}

inline constexpr void requireNumber(AssetUnits value) {
	requireNumber(value.value());
}

inline constexpr void requireNumber(ScaleFactor value) {
	requireNumber(value.value());
}

inline constexpr void requireNumber(Angle value) {
	requireNumber(value.radians());
}

inline constexpr void requireNumber(Ratio value) {
	requireNumber(value.value());
}

inline constexpr void requireOpacity(Opacity value) {
	if (!std::isfinite(value.value()) || value.value() < 0 || value.value() > 1) {
		reject("Stana: invalid compiled symbol opacity");
	}
}

inline constexpr void requirePaint(const Paint &paint) {
	if (const auto *color = std::get_if<RgbaColor>(&paint)) {
		const bool channels = finiteLimited(color->red.value()) && color->red.value() >= 0 &&
			color->red.value() <= 1 && finiteLimited(color->green.value()) &&
			color->green.value() >= 0 && color->green.value() <= 1 &&
			finiteLimited(color->blue.value()) && color->blue.value() >= 0 &&
			color->blue.value() <= 1 && finiteLimited(color->alpha.value()) &&
			color->alpha.value() >= 0 && color->alpha.value() <= 1;
		if (!channels) {
			reject("Stana: invalid compiled symbol color");
		}
	}
}

inline constexpr auto normalizedUnits(AssetUnits value) -> AssetUnits {
	return AssetUnits{normalized(value.value())};
}

inline constexpr auto normalizedOpacity(Opacity value) -> Opacity {
	return Opacity{normalized(value.value())};
}

inline constexpr auto normalizedChannel(SdrChannel value) -> SdrChannel {
	return SdrChannel{normalized(value.value())};
}

inline constexpr auto normalizedRatio(Ratio value) -> Ratio {
	return Ratio{normalized(value.value())};
}

inline constexpr auto normalizedAngle(Angle value) -> Angle {
	return Angle{normalized(value.radians())};
}

inline constexpr auto normalizedTransform(AssetTransform transform) -> AssetTransform {
	transform.a = ScaleFactor{normalized(transform.a.value())};
	transform.b = ScaleFactor{normalized(transform.b.value())};
	transform.c = ScaleFactor{normalized(transform.c.value())};
	transform.d = ScaleFactor{normalized(transform.d.value())};
	transform.tx = normalizedUnits(transform.tx);
	transform.ty = normalizedUnits(transform.ty);
	return transform;
}

inline constexpr void requireTransform(const AssetTransform &transform) {
	requireNumber(transform.a);
	requireNumber(transform.b);
	requireNumber(transform.c);
	requireNumber(transform.d);
	requireNumber(transform.tx);
	requireNumber(transform.ty);
}

inline constexpr auto normalizedCommand(PathCommand command) -> PathCommand {
	auto zeroPoint = [](AssetUnits &x, AssetUnits &y) -> void {
		x = normalizedUnits(x);
		y = normalizedUnits(y);
	};
	if (auto *move = std::get_if<MoveTo>(&command)) {
		zeroPoint(move->x, move->y);
	} else if (auto *line = std::get_if<LineTo>(&command)) {
		zeroPoint(line->x, line->y);
	} else if (auto *quadratic = std::get_if<QuadraticTo>(&command)) {
		zeroPoint(quadratic->controlX, quadratic->controlY);
		zeroPoint(quadratic->x, quadratic->y);
	} else if (auto *cubic = std::get_if<CubicTo>(&command)) {
		zeroPoint(cubic->control1X, cubic->control1Y);
		zeroPoint(cubic->control2X, cubic->control2Y);
		zeroPoint(cubic->x, cubic->y);
	} else if (auto *arc = std::get_if<ArcTo>(&command)) {
		arc->centerX = normalizedUnits(arc->centerX);
		arc->centerY = normalizedUnits(arc->centerY);
		arc->radiusX = normalizedUnits(arc->radiusX);
		arc->radiusY = normalizedUnits(arc->radiusY);
		arc->rotation = normalizedAngle(arc->rotation);
		arc->startAngle = normalizedAngle(arc->startAngle);
		arc->sweepAngle = normalizedAngle(arc->sweepAngle);
	}
	return command;
}

struct Budget {
	int elements = 0;
	int commands = 0;
};

inline constexpr void
validateElements(std::vector<Element> &elements, int depth, AssetTransform parent, Budget &budget);

inline constexpr void
validateElements(std::vector<Element> &elements, int depth, AssetTransform parent, Budget &budget) {
	if (elements.empty()) {
		return;
	}
	if (depth > nestingLimit) {
		reject("Stana: compiled symbol nesting limit exceeded");
	}
	for (Element &element : elements) {
		++budget.elements;
		if (budget.elements > elementLimit) {
			reject("Stana: compiled symbol element limit exceeded");
		}
		if (auto *group = std::get_if<Element::Group>(&element.value)) {
			requireTransform(group->transform);
			const AssetTransform cumulative = parent.concatenating(group->transform);
			requireTransform(cumulative);
			requireOpacity(group->opacity);
			group->transform = normalizedTransform(group->transform);
			group->opacity = normalizedOpacity(group->opacity);
			validateElements(group->children, depth + 1, cumulative, budget);
			requireTransform(group->transform);
		} else {
			auto &path = std::get<Element::Path>(element.value);
			requireTransform(path.transform);
			const AssetTransform cumulative = parent.concatenating(path.transform);
			requireTransform(cumulative);
			requirePaint(path.fill);
			requirePaint(path.stroke);
			requireOpacity(path.fillOpacity);
			requireOpacity(path.strokeOpacity);
			requireOpacity(path.opacity);
			requireNumber(path.strokeWidth);
			requireNumber(path.miterLimit);
			if (path.strokeWidth.value() < 0 || path.miterLimit.value() < minimumMiterLimit) {
				reject("Stana: invalid compiled symbol stroke");
			}
			budget.commands += static_cast<int>(path.commands.size());
			if (budget.commands > commandLimit) {
				reject("Stana: compiled symbol command limit exceeded");
			}
			for (PathCommand &command : path.commands) {
				command = normalizedCommand(command);
				auto checkPoint = [&](AssetUnits x, AssetUnits y) -> void {
					requireNumber(x);
					requireNumber(y);
					const AssetPoint mapped = cumulative.apply({.x = x, .y = y});
					requireNumber(mapped.x);
					requireNumber(mapped.y);
				};
				if (const auto *move = std::get_if<MoveTo>(&command)) {
					checkPoint(move->x, move->y);
				} else if (const auto *line = std::get_if<LineTo>(&command)) {
					checkPoint(line->x, line->y);
				} else if (const auto *quadratic = std::get_if<QuadraticTo>(&command)) {
					checkPoint(quadratic->controlX, quadratic->controlY);
					checkPoint(quadratic->x, quadratic->y);
				} else if (const auto *cubic = std::get_if<CubicTo>(&command)) {
					checkPoint(cubic->control1X, cubic->control1Y);
					checkPoint(cubic->control2X, cubic->control2Y);
					checkPoint(cubic->x, cubic->y);
				} else if (const auto *arc = std::get_if<ArcTo>(&command)) {
					requireNumber(arc->centerX);
					requireNumber(arc->centerY);
					requireNumber(arc->radiusX);
					requireNumber(arc->radiusY);
					requireNumber(arc->rotation);
					requireNumber(arc->startAngle);
					requireNumber(arc->sweepAngle);
					if (!(arc->radiusX.value() > 0) || !(arc->radiusY.value() > 0) ||
						magnitude(arc->sweepAngle.radians()) > fullTurn) {
						reject("Stana: invalid compiled symbol arc");
					}
				}
			}
			if (!path.commands.empty() && !std::holds_alternative<MoveTo>(path.commands.at(0))) {
				reject("Stana: compiled symbol path must start with move");
			}
			path.transform = normalizedTransform(path.transform);
			path.fillOpacity = normalizedOpacity(path.fillOpacity);
			path.strokeOpacity = normalizedOpacity(path.strokeOpacity);
			path.strokeWidth = normalizedUnits(path.strokeWidth);
			path.miterLimit = normalizedRatio(path.miterLimit);
			path.opacity = normalizedOpacity(path.opacity);
			if (auto *color = std::get_if<RgbaColor>(&path.fill)) {
				*color = RgbaColor{
					.red = normalizedChannel(color->red),
					.green = normalizedChannel(color->green),
					.blue = normalizedChannel(color->blue),
					.alpha = normalizedOpacity(color->alpha)
				};
			}
			if (auto *color = std::get_if<RgbaColor>(&path.stroke)) {
				*color = RgbaColor{
					.red = normalizedChannel(color->red),
					.green = normalizedChannel(color->green),
					.blue = normalizedChannel(color->blue),
					.alpha = normalizedOpacity(color->alpha)
				};
			}
		}
	}
}

[[nodiscard]] inline constexpr auto stroked(const std::vector<Element> &elements) -> bool {
	for (const Element &element : elements) {
		if (const auto *group = std::get_if<Element::Group>(&element.value)) {
			if (stroked(group->children)) {
				return true;
			}
		} else if (!std::holds_alternative<NoPaint>(
					   std::get<Element::Path>(element.value).stroke
				   )) {
			return true;
		}
	}
	return false;
}
} // namespace detail

constexpr auto Asset::compiled(
	std::uint32_t version,
	AssetRect viewBox,
	bool mirrorsInRTL,
	std::vector<Element> elements
) -> Asset {
	if (version != detail::bridgeVersion) {
		detail::reject(
			"Stana: symbol compiler/runtime package mismatch (expected bridge version 1)"
		);
	}
	viewBox = {
		.origin =
			{.x = detail::normalizedUnits(viewBox.origin.x),
				.y = detail::normalizedUnits(viewBox.origin.y)},
		.size = {
			.width = detail::normalizedUnits(viewBox.size.width),
			.height = detail::normalizedUnits(viewBox.size.height)
		}
	};
	if (!(viewBox.size.width.value() > 0) || !(viewBox.size.height.value() > 0)) {
		detail::reject("Stana: invalid compiled symbol viewBox");
	}
	detail::requireNumber(viewBox.origin.x);
	detail::requireNumber(viewBox.origin.y);
	detail::requireNumber(viewBox.size.width);
	detail::requireNumber(viewBox.size.height);
	detail::Budget budget;
	detail::validateElements(elements, 1, AssetTransform::identity(), budget);
	Asset asset;
	asset.viewBox_ = viewBox;
	asset.mirrorsInRTL_ = mirrorsInRTL;
	asset.hasStroke_ = detail::stroked(elements);
	asset.estimatedBytes_ = static_cast<std::size_t>(detail::storageBaseBytes) +
		static_cast<std::size_t>(budget.elements) *
			static_cast<std::size_t>(detail::storageElementBytes) +
		static_cast<std::size_t>(budget.commands) *
			static_cast<std::size_t>(detail::storageCommandBytes);
	asset.elements_ = std::move(elements);
	asset.valid_ = true;
	return asset;
}
} // namespace stana::symbol
