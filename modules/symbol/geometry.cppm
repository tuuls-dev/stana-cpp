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
export import :symbol_units;
import :primitives;

export namespace stana::symbol {
namespace detail {
[[nodiscard]] constexpr auto toRect(AssetRect rect) noexcept -> Rect {
	return {
		.origin =
			{.x = Points{rect.origin.x.value()}, .y = Points{rect.origin.y.value()}},
		.size =
			{.width = Points{rect.size.width.value()},
				.height = Points{rect.size.height.value()}}
	};
}

[[nodiscard]] inline auto emptyRect(AssetRect rect) noexcept -> bool {
	return std::is_eq(rect.size.width <=> AssetUnits{}) ||
		std::is_eq(rect.size.height <=> AssetUnits{});
}

[[nodiscard]] inline auto unite(AssetRect left, AssetRect right) noexcept -> AssetRect {
	if (emptyRect(left)) {
		return right;
	}
	if (emptyRect(right)) {
		return left;
	}
	const AssetUnits x{std::min(left.origin.x.value(), right.origin.x.value())};
	const AssetUnits y{std::min(left.origin.y.value(), right.origin.y.value())};
	return {
		.origin = {.x = x, .y = y},
		.size = {
			.width = AssetUnits{
				std::max(
					left.origin.x.value() + left.size.width.value(),
					right.origin.x.value() + right.size.width.value()
				) -
					x.value()
			},
			.height = AssetUnits{
				std::max(
					left.origin.y.value() + left.size.height.value(),
					right.origin.y.value() + right.size.height.value()
				) -
					y.value()
			}
		}
	};
}
} // namespace detail

/// @brief Centered square of side min(size, content width, content height).
/// A zero side still reports the center of the content box. textScale is not an input.
[[nodiscard]] inline auto square(Points size, Rect content) noexcept -> Rect {
	const double side = std::min(
		{size.value(), content.size.width.value(), content.size.height.value()}
	);
	return {
		.origin =
			{.x = Points{
				 content.origin.x.value() + (content.size.width.value() - side) / 2
			 },
				.y = Points{
				 content.origin.y.value() + (content.size.height.value() - side) / 2
			 }},
		.size = {.width = Points{side}, .height = Points{side}}
	};
}

/// @brief Axis-aligned hull of a transformed logical rectangle.
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
	double minX = corners.front().x.value();
	double minY = corners.front().y.value();
	double maxX = corners.front().x.value();
	double maxY = corners.front().y.value();
	for (const Point corner : corners) {
		minX = std::min(minX, corner.x.value());
		minY = std::min(minY, corner.y.value());
		maxX = std::max(maxX, corner.x.value());
		maxY = std::max(maxY, corner.y.value());
	}
	const double width = maxX - minX;
	const double height = maxY - minY;
	if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX) ||
		!std::isfinite(maxY) || !std::isfinite(width) || !std::isfinite(height)) {
		return std::nullopt;
	}
	return Rect{
		.origin = {.x = Points{minX}, .y = Points{minY}},
		.size = {.width = Points{width}, .height = Points{height}}
	};
}

/// @brief Axis-aligned hull of a transformed rectangle in asset space.
/// A singular transform returns a zero rectangle. Nonfinite corners return no value so the
/// caller can report a resource diagnostic instead of painting an overflow.
[[nodiscard]] inline auto enclosing(const AssetTransform &transform, AssetRect rect)
	-> std::optional<AssetRect> {
	if (!transform.inverse()) {
		return AssetRect{};
	}
	const std::array<AssetPoint, 4> corners = {
		transform.apply({.x = rect.origin.x, .y = rect.origin.y}),
		transform.apply(
			{.x = rect.origin.x + rect.size.width, .y = rect.origin.y}
		),
		transform.apply(
			{.x = rect.origin.x, .y = rect.origin.y + rect.size.height}
		),
		transform.apply(
			{.x = rect.origin.x + rect.size.width,
				.y = rect.origin.y + rect.size.height}
		)
	};
	double minX = corners.front().x.value();
	double minY = corners.front().y.value();
	double maxX = corners.front().x.value();
	double maxY = corners.front().y.value();
	for (const AssetPoint corner : corners) {
		minX = std::min(minX, corner.x.value());
		minY = std::min(minY, corner.y.value());
		maxX = std::max(maxX, corner.x.value());
		maxY = std::max(maxY, corner.y.value());
	}
	const double width = maxX - minX;
	const double height = maxY - minY;
	if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX) ||
		!std::isfinite(maxY) || !std::isfinite(width) || !std::isfinite(height)) {
		return std::nullopt;
	}
	return AssetRect{
		.origin = {.x = AssetUnits{minX}, .y = AssetUnits{minY}},
		.size = {.width = AssetUnits{width}, .height = AssetUnits{height}}
	};
}

/// @brief Conservative local ink hull, including stroke caps, joins, and control points.
/// No value means a nonfinite bound. Empty or fully transparent artwork returns a zero rect.
[[nodiscard]] inline auto inkBounds(const Asset &asset, std::optional<Points> strokeOverride)
	-> std::optional<Rect> {
	constexpr double squareCapFactor = std::numbers::sqrt2;
	const auto visit = [&](const auto &self,
						   const std::vector<Element> &elements,
						   AssetTransform parent) -> std::optional<AssetRect> {
		AssetRect result;
		for (const Element &element : elements) {
			if (const auto *group = std::get_if<Element::Group>(&element.value)) {
				if (group->opacity.value() > 0) {
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
			if (!(path.opacity.value() > 0)) {
				continue;
			}
			const bool hasFill =
				!std::holds_alternative<NoPaint>(path.fill) && path.fillOpacity.value() > 0;
			const AssetUnits width = strokeOverride
				? AssetUnits{strokeOverride->value()}
				: path.strokeWidth;
			const bool hasStroke = !std::holds_alternative<NoPaint>(path.stroke) &&
				path.strokeOpacity.value() > 0 && width.value() > 0;
			if (!hasFill && !hasStroke) {
				continue;
			}
			bool any = false;
			double minX = 0;
			double minY = 0;
			double maxX = 0;
			double maxY = 0;
			const auto include = [&](AssetUnits x, AssetUnits y) -> void {
				const double px = x.value();
				const double py = y.value();
				if (!any) {
					minX = maxX = px;
					minY = maxY = py;
					any = true;
					return;
				}
				minX = std::min(minX, px);
				minY = std::min(minY, py);
				maxX = std::max(maxX, px);
				maxY = std::max(maxY, py);
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
					const double rotation = arc->rotation.radians();
					const double dx = std::hypot(
						arc->radiusX.value() * std::cos(rotation),
						arc->radiusY.value() * std::sin(rotation)
					);
					const double dy = std::hypot(
						arc->radiusX.value() * std::sin(rotation),
						arc->radiusY.value() * std::cos(rotation)
					);
					include(
						AssetUnits{arc->centerX.value() - dx},
						AssetUnits{arc->centerY.value() - dy}
					);
					include(
						AssetUnits{arc->centerX.value() + dx},
						AssetUnits{arc->centerY.value() + dy}
					);
				}
			}
			if (!any) {
				continue;
			}
			const double joinFactor =
				path.lineJoin == LineJoin::miter ? path.miterLimit.value() : 1;
			const double capFactor = path.lineCap == LineCap::square ? squareCapFactor : 1;
			const double margin =
				hasStroke ? width.value() / 2 * std::max(joinFactor, capFactor) : 0;
			const AssetUnits left{minX - margin};
			const AssetUnits top{minY - margin};
			const AssetUnits right{maxX + margin};
			const AssetUnits bottom{maxY + margin};
			if (!std::isfinite(left.value()) || !std::isfinite(top.value()) ||
				!std::isfinite(right.value()) || !std::isfinite(bottom.value()) ||
				!std::isfinite(right.value() - left.value()) ||
				!std::isfinite(bottom.value() - top.value())) {
				return std::nullopt;
			}
			const auto bounds = enclosing(
				parent.concatenating(path.transform),
				AssetRect{
					.origin = {.x = left, .y = top},
					.size =
						{.width = AssetUnits{right.value() - left.value()},
							.height = AssetUnits{bottom.value() - top.value()}}
				}
			);
			if (!bounds) {
				return std::nullopt;
			}
			result = detail::unite(result, *bounds);
		}
		return result;
	};
	const auto assetBounds = visit(visit, asset.elements(), AssetTransform::identity());
	if (!assetBounds) {
		return std::nullopt;
	}
	return detail::toRect(*assetBounds);
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
	std::optional<Points> strokeWidth{};
	std::uint64_t generation = 0;
	bool preparationLimited = false;
	ScaleFactor preparationScale{1};

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
	Points size,
	Rect content,
	Direction direction,
	const EffectSample &effect,
	Color color,
	std::optional<Points> strokeWidth,
	std::uint64_t generation,
	bool available,
	const std::optional<Rect> &ink
) -> std::optional<Presentation> {
	if (!available) {
		return std::nullopt;
	}
	const Rect fitted = square(size, content);
	if (!(fitted.size.width.value() > 0)) {
		return std::nullopt;
	}
	const AssetRect box = asset.viewBox();
	const double boxLongest =
		std::max(box.size.width.value(), box.size.height.value());
	const ScaleFactor fitScale{boxLongest > 0
			? fitted.size.width.value() / boxLongest
			: 0};
	const Points centerX =
		fitted.origin.x + Points{fitted.size.width.value() / 2};
	const Points centerY =
		fitted.origin.y + Points{fitted.size.height.value() / 2};
	const AssetUnits boxCenterX =
		AssetUnits{box.origin.x.value() + box.size.width.value() / 2};
	const AssetUnits boxCenterY =
		AssetUnits{box.origin.y.value() + box.size.height.value() / 2};
	const double mirror = asset.mirrorsInRTL() && direction == Direction::rightToLeft ? -1.0 : 1.0;
	const double cosine =
		std::cos(effect.rotation.radians()) * effect.scale.value() * fitScale.value();
	const double sine =
		std::sin(effect.rotation.radians()) * effect.scale.value() * fitScale.value();
	const ScaleFactor horizontal{cosine * mirror};
	const ScaleFactor vertical{sine * mirror};
	const ScaleFactor skew{-sine};
	const ScaleFactor verticalScale{cosine};
	const Points translateX{
		centerX.value() - cosine * mirror * boxCenterX.value() + sine * boxCenterY.value()
	};
	const Points translateY{
		centerY.value() + effect.verticalFraction.value() * fitted.size.height.value() -
			sine * mirror * boxCenterX.value() - cosine * boxCenterY.value()
	};
	const bool finite = std::isfinite(horizontal.value()) && std::isfinite(vertical.value()) &&
		std::isfinite(skew.value()) && std::isfinite(verticalScale.value()) &&
		std::isfinite(translateX.value()) && std::isfinite(translateY.value());
	const bool limited = !ink || !finite;
	Presentation result{
		.asset = asset,
		.color = color,
		.strokeWidth = strokeWidth,
		.generation = generation,
		.preparationLimited = limited,
		.preparationScale = limited ? ScaleFactor{1} : ScaleFactor{fitScale.value() * effect.scale.value()}
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
