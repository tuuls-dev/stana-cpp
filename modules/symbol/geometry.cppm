module;

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <numbers>
#include <optional>
#include <variant>
#include <vector>

export module stana:symbol_geometry;
export import :symbol_asset;
export import :symbol_playback;
import :primitives;

export namespace stana::symbol {
namespace detail {
[[nodiscard]] inline auto emptyRect(Rect rect) noexcept -> bool {
	return std::is_eq(rect.size.width <=> 0.0) || std::is_eq(rect.size.height <=> 0.0);
}

[[nodiscard]] inline auto unite(Rect left, Rect right) noexcept -> Rect {
	if (emptyRect(left)) {
		return right;
	}
	if (emptyRect(right)) {
		return left;
	}
	const double x = std::min(left.origin.x, right.origin.x);
	const double y = std::min(left.origin.y, right.origin.y);
	return {
		.origin = {.x = x, .y = y},
		.size = {
			.width =
				std::max(left.origin.x + left.size.width, right.origin.x + right.size.width) - x,
			.height =
				std::max(left.origin.y + left.size.height, right.origin.y + right.size.height) - y
		}
	};
}
} // namespace detail

/// @brief Centered square of side min(size, content width, content height).
/// A zero side still reports the center of the content box. textScale is not an input.
[[nodiscard]] inline auto square(double size, Rect content) noexcept -> Rect {
	const double side = std::min({size, content.size.width, content.size.height});
	return {
		.origin =
			{.x = content.origin.x + (content.size.width - side) / 2,
				.y = content.origin.y + (content.size.height - side) / 2},
		.size = {.width = side, .height = side}
	};
}

/// @brief Axis-aligned hull of a transformed rectangle.
/// A singular transform returns a zero rectangle. Nonfinite corners return no value so the
/// caller can report a resource diagnostic instead of painting an overflow.
[[nodiscard]] inline auto enclosing(const AffineTransform &transform, Rect rect)
	-> std::optional<Rect> {
	if (!transform.inverse()) {
		return Rect{};
	}
	const std::array<Point, 4> corners = {
		transform.apply({.x = rect.origin.x, .y = rect.origin.y}),
		transform.apply({.x = rect.origin.x + rect.size.width, .y = rect.origin.y}),
		transform.apply({.x = rect.origin.x, .y = rect.origin.y + rect.size.height}),
		transform.apply(
			{.x = rect.origin.x + rect.size.width, .y = rect.origin.y + rect.size.height}
		)
	};
	double minX = corners.front().x;
	double minY = corners.front().y;
	double maxX = corners.front().x;
	double maxY = corners.front().y;
	for (const Point corner : corners) {
		minX = std::min(minX, corner.x);
		minY = std::min(minY, corner.y);
		maxX = std::max(maxX, corner.x);
		maxY = std::max(maxY, corner.y);
	}
	const double width = maxX - minX;
	const double height = maxY - minY;
	if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX) ||
		!std::isfinite(maxY) || !std::isfinite(width) || !std::isfinite(height)) {
		return std::nullopt;
	}
	return Rect{.origin = {.x = minX, .y = minY}, .size = {.width = width, .height = height}};
}

/// @brief Conservative local ink hull, including stroke caps, joins, and control points.
/// No value means a nonfinite bound. Empty or fully transparent artwork returns a zero rect.
[[nodiscard]] inline auto inkBounds(const Asset &asset, std::optional<double> strokeOverride)
	-> std::optional<Rect> {
	constexpr double squareCapFactor = std::numbers::sqrt2;
	const auto visit = [&](const auto &self,
						   const std::vector<Element> &elements,
						   AffineTransform parent) -> std::optional<Rect> {
		Rect result;
		for (const Element &element : elements) {
			if (const auto *group = std::get_if<Element::Group>(&element.value)) {
				if (group->opacity > 0) {
					const auto bounds =
						self(self, group->children, parent.concatenating(group->transform));
					if (!bounds) {
						return std::nullopt;
					}
					result = detail::unite(result, *bounds);
				}
				continue;
			}
			const auto &path = std::get<Element::Path>(element.value);
			if (!(path.opacity > 0)) {
				continue;
			}
			const bool hasFill =
				!std::holds_alternative<NoPaint>(path.fill) && path.fillOpacity > 0;
			const double width = strokeOverride.value_or(path.strokeWidth);
			const bool hasStroke = !std::holds_alternative<NoPaint>(path.stroke) &&
				path.strokeOpacity > 0 && width > 0;
			if (!hasFill && !hasStroke) {
				continue;
			}
			bool any = false;
			double minX = 0;
			double minY = 0;
			double maxX = 0;
			double maxY = 0;
			const auto include = [&](double x, double y) -> void {
				if (!any) {
					minX = maxX = x;
					minY = maxY = y;
					any = true;
					return;
				}
				minX = std::min(minX, x);
				minY = std::min(minY, y);
				maxX = std::max(maxX, x);
				maxY = std::max(maxY, y);
			};
			for (const PathCommand &command : path.commands) {
				if (const auto *move = std::get_if<MoveTo>(&command)) {
					include(move->x, move->y);
				} else if (const auto *line = std::get_if<LineTo>(&command)) {
					include(line->x, line->y);
				} else if (const auto *quadratic = std::get_if<QuadraticTo>(&command)) {
					include(quadratic->controlX, quadratic->controlY);
					include(quadratic->x, quadratic->y);
				} else if (const auto *cubic = std::get_if<CubicTo>(&command)) {
					include(cubic->control1X, cubic->control1Y);
					include(cubic->control2X, cubic->control2Y);
					include(cubic->x, cubic->y);
				} else if (const auto *arc = std::get_if<ArcTo>(&command)) {
					const double dx = std::hypot(
						arc->radiusX * std::cos(arc->rotation),
						arc->radiusY * std::sin(arc->rotation)
					);
					const double dy = std::hypot(
						arc->radiusX * std::sin(arc->rotation),
						arc->radiusY * std::cos(arc->rotation)
					);
					include(arc->centerX - dx, arc->centerY - dy);
					include(arc->centerX + dx, arc->centerY + dy);
				}
			}
			if (!any) {
				continue;
			}
			const double joinFactor = path.lineJoin == LineJoin::miter ? path.miterLimit : 1;
			const double capFactor = path.lineCap == LineCap::square ? squareCapFactor : 1;
			const double margin = hasStroke ? width / 2 * std::max(joinFactor, capFactor) : 0;
			const double left = minX - margin;
			const double top = minY - margin;
			const double right = maxX + margin;
			const double bottom = maxY + margin;
			if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) ||
				!std::isfinite(bottom) || !std::isfinite(right - left) ||
				!std::isfinite(bottom - top)) {
				return std::nullopt;
			}
			const auto bounds = enclosing(
				parent.concatenating(path.transform),
				Rect{
					.origin = {.x = left, .y = top},
					.size = {.width = right - left, .height = bottom - top}
				}
			);
			if (!bounds) {
				return std::nullopt;
			}
			result = detail::unite(result, *bounds);
		}
		return result;
	};
	return visit(visit, asset.elements(), AffineTransform::identity());
}

/// @brief Paint-only placement of one symbol. Layout bounds stay on the node.
///
/// The transform aspect-fits the viewBox into the centered square, mirrors in RTL when
/// requested, then applies the effect around the square center. A zero square or an
/// unavailable symbol produces no presentation, so the renderer paints nothing.
struct Presentation {
	Asset asset{};
	AffineTransform transform{};
	Color color{};
	std::optional<double> strokeWidth{};
	std::uint64_t generation = 0;
	bool preparationLimited = false;
	double preparationScale = 1;

	[[nodiscard]] auto operator==(const Presentation &other) const -> bool {
		return asset == other.asset && transform == other.transform && color == other.color &&
			strokeWidth == other.strokeWidth && generation == other.generation &&
			preparationLimited == other.preparationLimited &&
			std::is_eq(preparationScale <=> other.preparationScale);
	}
};

/// @brief Builds the scene transform from the fitted square, RTL mirror, and effect sample.
/// The effect sample is paint-only. It does not change measurement. Zero content or an
/// unavailable symbol omits the leaf.
[[nodiscard]] inline auto presentationFor(
	const Asset &asset,
	double size,
	Rect content,
	Direction direction,
	const EffectSample &effect,
	Color color,
	std::optional<double> strokeWidth,
	std::uint64_t generation,
	bool available,
	const std::optional<Rect> &ink
) -> std::optional<Presentation> {
	if (!available) {
		return std::nullopt;
	}
	const Rect fitted = square(size, content);
	if (!(fitted.size.width > 0)) {
		return std::nullopt;
	}
	const Rect box = asset.viewBox();
	const double fitScale = fitted.size.width / std::max(box.size.width, box.size.height);
	const double centerX = fitted.origin.x + fitted.size.width / 2;
	const double centerY = fitted.origin.y + fitted.size.height / 2;
	const double boxCenterX = box.origin.x + box.size.width / 2;
	const double boxCenterY = box.origin.y + box.size.height / 2;
	const double mirror = asset.mirrorsInRTL() && direction == Direction::rightToLeft ? -1.0 : 1.0;
	const double cosine = std::cos(effect.rotation) * effect.scale * fitScale;
	const double sine = std::sin(effect.rotation) * effect.scale * fitScale;
	const double horizontal = cosine * mirror;
	const double vertical = sine * mirror;
	const double skew = -sine;
	const double verticalScale = cosine;
	const double translateX = centerX - cosine * mirror * boxCenterX + sine * boxCenterY;
	const double translateY = centerY + effect.verticalFraction * fitted.size.height -
		sine * mirror * boxCenterX - cosine * boxCenterY;
	const bool finite = std::isfinite(horizontal) && std::isfinite(vertical) &&
		std::isfinite(skew) && std::isfinite(verticalScale) && std::isfinite(translateX) &&
		std::isfinite(translateY);
	const bool limited = !ink || !finite;
	Presentation result{
		.asset = asset,
		.color = color,
		.strokeWidth = strokeWidth,
		.generation = generation,
		.preparationLimited = limited,
		.preparationScale = limited ? 1.0 : fitScale * effect.scale
	};
	if (!limited) {
		result.transform = {
			.a = horizontal,
			.b = vertical,
			.c = skew,
			.d = verticalScale,
			.tx = translateX,
			.ty = translateY
		};
	}
	return result;
}
} // namespace stana::symbol
