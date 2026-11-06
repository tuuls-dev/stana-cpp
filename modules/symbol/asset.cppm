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
	double red = 0;
	double green = 0;
	double blue = 0;
	double alpha = 1;
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

/// @brief One normalized path verb in viewBox coordinates.
struct MoveTo {
	double x = 0;
	double y = 0;
	auto operator<=>(const MoveTo &) const = default;

	[[nodiscard]] auto operator==(const MoveTo &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

struct LineTo {
	double x = 0;
	double y = 0;
	auto operator<=>(const LineTo &) const = default;

	[[nodiscard]] auto operator==(const LineTo &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

struct QuadraticTo {
	double controlX = 0;
	double controlY = 0;
	double x = 0;
	double y = 0;
	auto operator<=>(const QuadraticTo &) const = default;

	[[nodiscard]] auto operator==(const QuadraticTo &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

struct CubicTo {
	double control1X = 0;
	double control1Y = 0;
	double control2X = 0;
	double control2Y = 0;
	double x = 0;
	double y = 0;
	auto operator<=>(const CubicTo &) const = default;

	[[nodiscard]] auto operator==(const CubicTo &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

struct ArcTo {
	double centerX = 0;
	double centerY = 0;
	double radiusX = 0;
	double radiusY = 0;
	double rotation = 0;
	double startAngle = 0;
	double sweepAngle = 0;
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
		AffineTransform transform{};
		double opacity = 1;
		std::vector<Element> children;

		constexpr Group();
		constexpr Group(const Group &);
		constexpr Group(Group &&) noexcept;
		constexpr auto operator=(const Group &) -> Group &;
		constexpr auto operator=(Group &&) noexcept -> Group &;
		constexpr ~Group();
		[[nodiscard]] auto operator==(const Group &) const -> bool;
	};

	/// @brief One filled and/or stroked path in local coordinates.
	struct Path {
		std::vector<PathCommand> commands;
		AffineTransform transform{};
		Paint fill = RgbaColor{};
		FillRule fillRule = FillRule::nonzero;
		double fillOpacity = 1;
		Paint stroke = NoPaint{};
		double strokeOpacity = 1;
		double strokeWidth = 1;
		LineCap lineCap = LineCap::butt;
		LineJoin lineJoin = LineJoin::miter;
		double miterLimit = 4;
		double opacity = 1;
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
	Rect viewBox{};
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
	[[nodiscard]] static constexpr auto
	compiled(std::uint32_t version, Rect viewBox, bool mirrorsInRTL, std::vector<Element> elements)
		-> Asset;

	[[nodiscard]] constexpr auto viewBox() const noexcept -> Rect { return viewBox_; }

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

	Rect viewBox_{};
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

inline constexpr void requireOpacity(double value) {
	if (!std::isfinite(value) || value < 0 || value > 1) {
		reject("Stana: invalid compiled symbol opacity");
	}
}

inline constexpr void requirePaint(const Paint &paint) {
	if (const auto *color = std::get_if<RgbaColor>(&paint)) {
		const bool channels = finiteLimited(color->red) && color->red >= 0 && color->red <= 1 &&
			finiteLimited(color->green) && color->green >= 0 && color->green <= 1 &&
			finiteLimited(color->blue) && color->blue >= 0 && color->blue <= 1 &&
			finiteLimited(color->alpha) && color->alpha >= 0 && color->alpha <= 1;
		if (!channels) {
			reject("Stana: invalid compiled symbol color");
		}
	}
}

inline constexpr auto normalizedTransform(AffineTransform transform) -> AffineTransform {
	transform.a = normalized(transform.a);
	transform.b = normalized(transform.b);
	transform.c = normalized(transform.c);
	transform.d = normalized(transform.d);
	transform.tx = normalized(transform.tx);
	transform.ty = normalized(transform.ty);
	return transform;
}

inline constexpr void requireTransform(const AffineTransform &transform) {
	requireNumber(transform.a);
	requireNumber(transform.b);
	requireNumber(transform.c);
	requireNumber(transform.d);
	requireNumber(transform.tx);
	requireNumber(transform.ty);
}

inline constexpr auto normalizedCommand(PathCommand command) -> PathCommand {
	auto zeroPoint = [](double &x, double &y) -> void {
		x = normalized(x);
		y = normalized(y);
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
		arc->centerX = normalized(arc->centerX);
		arc->centerY = normalized(arc->centerY);
		arc->radiusX = normalized(arc->radiusX);
		arc->radiusY = normalized(arc->radiusY);
		arc->rotation = normalized(arc->rotation);
		arc->startAngle = normalized(arc->startAngle);
		arc->sweepAngle = normalized(arc->sweepAngle);
	}
	return command;
}

struct Budget {
	int elements = 0;
	int commands = 0;
};

inline constexpr void
validateElements(std::vector<Element> &elements, int depth, AffineTransform parent, Budget &budget);

inline constexpr void validateElements(
	std::vector<Element> &elements,
	int depth,
	AffineTransform parent,
	Budget &budget
) {
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
			const AffineTransform cumulative = parent.concatenating(group->transform);
			requireTransform(cumulative);
			requireOpacity(group->opacity);
			group->transform = normalizedTransform(group->transform);
			group->opacity = normalized(group->opacity);
			validateElements(group->children, depth + 1, cumulative, budget);
			requireTransform(group->transform);
		} else {
			auto &path = std::get<Element::Path>(element.value);
			requireTransform(path.transform);
			const AffineTransform cumulative = parent.concatenating(path.transform);
			requireTransform(cumulative);
			requirePaint(path.fill);
			requirePaint(path.stroke);
			requireOpacity(path.fillOpacity);
			requireOpacity(path.strokeOpacity);
			requireOpacity(path.opacity);
			requireNumber(path.strokeWidth);
			requireNumber(path.miterLimit);
			if (path.strokeWidth < 0 || path.miterLimit < minimumMiterLimit) {
				reject("Stana: invalid compiled symbol stroke");
			}
			budget.commands += static_cast<int>(path.commands.size());
			if (budget.commands > commandLimit) {
				reject("Stana: compiled symbol command limit exceeded");
			}
			for (PathCommand &command : path.commands) {
				command = normalizedCommand(command);
				auto checkPoint = [&](double x, double y) -> void {
					requireNumber(x);
					requireNumber(y);
					const Point mapped = cumulative.apply({.x = x, .y = y});
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
					if (!(arc->radiusX > 0) || !(arc->radiusY > 0) ||
						magnitude(arc->sweepAngle) > fullTurn) {
						reject("Stana: invalid compiled symbol arc");
					}
				}
			}
			if (!path.commands.empty() && !std::holds_alternative<MoveTo>(path.commands.at(0))) {
				reject("Stana: compiled symbol path must start with move");
			}
			path.transform = normalizedTransform(path.transform);
			path.fillOpacity = normalized(path.fillOpacity);
			path.strokeOpacity = normalized(path.strokeOpacity);
			path.strokeWidth = normalized(path.strokeWidth);
			path.miterLimit = normalized(path.miterLimit);
			path.opacity = normalized(path.opacity);
			if (auto *color = std::get_if<RgbaColor>(&path.fill)) {
				*color = RgbaColor{
					.red = normalized(color->red),
					.green = normalized(color->green),
					.blue = normalized(color->blue),
					.alpha = normalized(color->alpha)
				};
			}
			if (auto *color = std::get_if<RgbaColor>(&path.stroke)) {
				*color = RgbaColor{
					.red = normalized(color->red),
					.green = normalized(color->green),
					.blue = normalized(color->blue),
					.alpha = normalized(color->alpha)
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
	Rect viewBox,
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
			{.x = detail::normalized(viewBox.origin.x), .y = detail::normalized(viewBox.origin.y)},
		.size = {
			.width = detail::normalized(viewBox.size.width),
			.height = detail::normalized(viewBox.size.height)
		}
	};
	if (!(viewBox.size.width > 0) || !(viewBox.size.height > 0)) {
		detail::reject("Stana: invalid compiled symbol viewBox");
	}
	detail::requireNumber(viewBox.origin.x);
	detail::requireNumber(viewBox.origin.y);
	detail::requireNumber(viewBox.size.width);
	detail::requireNumber(viewBox.size.height);
	detail::Budget budget;
	detail::validateElements(elements, 1, AffineTransform::identity(), budget);
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
