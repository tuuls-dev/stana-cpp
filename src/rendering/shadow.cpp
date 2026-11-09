module;
#include "../gpu/color.hpp"
#include "../gpu/limits.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <vector>
module stana;
import :rendering_shadow;
import :primitives;

namespace stana::rendering::detail {
namespace {
void validateColor(Color color) {
	for (const double value :
		{color.red.value(), color.green.value(), color.blue.value(), color.alpha.value()}) {
		stana::detail::requireNonnegative(value);
		if (value > 1) {
			throw std::invalid_argument("Shadow SDR color exceeds one");
		}
	}
}

void validateRect(Rect bounds) {
	stana::detail::requireNonnegative(bounds.size.width);
	stana::detail::requireNonnegative(bounds.size.height);
	stana::detail::requireFinite(bounds.origin.x.value());
	stana::detail::requireFinite(bounds.origin.y.value());
	stana::detail::requireFinite(bounds.origin.x.value() + bounds.size.width.value());
	stana::detail::requireFinite(bounds.origin.y.value() + bounds.size.height.value());
}

[[nodiscard]] auto normalize(BoxContour contour) -> BoxContour {
	const auto &r = contour.radii;
	// Dividing before adding avoids overflow even for finite radii near DBL_MAX.
	double factor = 1;
	const auto limit = [&](double extent, Points first, Points second) -> void {
		const double largest = std::max(first.value(), second.value());
		if (largest > 0) {
			factor = std::min(
				factor,
				(extent / largest) / (first.value() / largest + second.value() / largest)
			);
		}
	};
	limit(contour.bounds.size.width.value(), r.at(0).width, r.at(1).width);
	limit(contour.bounds.size.width.value(), r.at(3).width, r.at(2).width);
	limit(contour.bounds.size.height.value(), r.at(0).height, r.at(3).height);
	limit(contour.bounds.size.height.value(), r.at(1).height, r.at(2).height);
	for (auto &radius : contour.radii) {
		radius.width = Points{radius.width.value() * factor};
		radius.height = Points{radius.height.value() * factor};
		if (radius.width.value() <= 0 || radius.height.value() <= 0) {
			radius = {};
		}
	}
	return contour;
}

[[nodiscard]] auto spreadRadius(double radius, double spread) -> double {
	// CSS Backgrounds 3 §6.1.1: preserve sharp corners when a growing spread exceeds radius.
	if (spread > 0 && radius < spread) {
		const double ratio = radius / spread - 1;
		spread *= 1 + ratio * ratio * ratio;
	}
	const double result = std::max(0.0, radius + spread);
	stana::detail::requireFinite(result);
	return result;
}

[[nodiscard]] auto spreadContour(BoxContour contour, Points spread) -> BoxContour {
	const double spreadValue = spread.value();
	contour.bounds.origin.x -= Points{spreadValue};
	contour.bounds.origin.y -= Points{spreadValue};
	contour.bounds.size.width =
		Points{std::max(0.0, contour.bounds.size.width.value() + 2 * spreadValue)};
	contour.bounds.size.height =
		Points{std::max(0.0, contour.bounds.size.height.value() + 2 * spreadValue)};
	validateRect(contour.bounds);
	for (auto &radius : contour.radii) {
		radius.width = Points{spreadRadius(radius.width.value(), spreadValue)};
		radius.height = Points{spreadRadius(radius.height.value(), spreadValue)};
	}
	return normalize(contour);
}
} // namespace

void validateCornerRadii(CornerRadii radii) {
	for (const auto radius :
		{radii.topLeading, radii.topTrailing, radii.bottomTrailing, radii.bottomLeading}) {
		stana::detail::requireNonnegative(radius.width);
		stana::detail::requireNonnegative(radius.height);
	}
}

void validateBorderWidths(BorderWidths widths) {
	for (const Points width : {widths.top, widths.leading, widths.bottom, widths.trailing}) {
		stana::detail::requireNonnegative(width);
	}
}

void validateBoxShadows(std::span<const BoxShadow> shadows) {
	if (shadows.size() > stana::gpu::detail::commandLimit) {
		throw std::invalid_argument("Box shadow list exceeds command budget");
	}
	for (const auto &shadow : shadows) {
		stana::detail::requireFinite(shadow.offset.x.value());
		stana::detail::requireFinite(shadow.offset.y.value());
		stana::detail::requireFinite(shadow.spreadRadius.value());
		stana::detail::requireNonnegative(shadow.blurRadius);
		if (shadow.color) {
			validateColor(*shadow.color);
		}
	}
}

auto boxGeometry(Rect bounds, CornerRadii radii, BorderWidths widths, Direction direction)
	-> BoxGeometry {
	validateRect(bounds);
	validateCornerRadii(radii);
	validateBorderWidths(widths);
	const bool rtl = direction == Direction::rightToLeft;
	BoxContour outer{
		.bounds = bounds,
		.radii = {
			rtl ? radii.topTrailing : radii.topLeading,
			rtl ? radii.topLeading : radii.topTrailing,
			rtl ? radii.bottomLeading : radii.bottomTrailing,
			rtl ? radii.bottomTrailing : radii.bottomLeading
		}
	};
	outer = normalize(outer);
	const double left =
		std::min(bounds.size.width.value(), (rtl ? widths.trailing : widths.leading).value());
	const double right = std::min(
		bounds.size.width.value() - left,
		(rtl ? widths.leading : widths.trailing).value()
	);
	const double top = std::min(bounds.size.height.value(), widths.top.value());
	const double bottom = std::min(bounds.size.height.value() - top, widths.bottom.value());
	BoxContour padding{
		.bounds =
			{.origin = {.x = bounds.origin.x + Points{left}, .y = bounds.origin.y + Points{top}},
				.size =
					{.width = Points{bounds.size.width.value() - left - right},
						.height = Points{bounds.size.height.value() - top - bottom}}},
		.radii = outer.radii
	};
	for (std::size_t index = 0; index < padding.radii.size(); ++index) {
		auto &radius = padding.radii.at(index);
		radius.width = Points{
			std::max(0.0, radius.width.value() - ((index == 0 || index == 3) ? left : right))
		};
		radius.height = Points{std::max(0.0, radius.height.value() - (index < 2 ? top : bottom))};
	}
	return {.outer = outer, .padding = normalize(padding)};
}

auto prepareBoxShadows(
	Rect bounds,
	std::span<const BoxShadow> shadows,
	CornerRadii radii,
	BorderWidths widths,
	Color foreground,
	Environment environment
) -> std::vector<ShadowPaint> {
	validateBoxShadows(shadows);
	validateColor(foreground);
	const auto geometry = boxGeometry(bounds, radii, widths, environment.direction);
	std::vector<ShadowPaint> result;
	result.reserve(shadows.size());
	// Reverse within the authored list, including mixed groups in an explicit GPU command.
	for (const auto &shadow : std::views::reverse(shadows)) {
		const auto color =
			shadow.color.value_or(foreground).resolved(environment.theme, environment.highContrast);
		if (color.alpha.value() <= 0) {
			continue;
		}
		const auto clip = shadow.isInset ? geometry.padding : geometry.outer;
		const auto caster = spreadContour(
			clip,
			shadow.isInset ? Points{-shadow.spreadRadius.value()} : shadow.spreadRadius
		);
		if ((shadow.isInset &&
				(clip.bounds.size.width.value() <= 0 || clip.bounds.size.height.value() <= 0)) ||
			(!shadow.isInset &&
				(caster.bounds.size.width.value() <= 0 ||
					caster.bounds.size.height.value() <= 0))) {
			continue;
		}
		Rect ink = clip.bounds;
		if (!shadow.isInset) {
			const double support = shadow.blurRadius.value() * (shadowSigmaSupport / 2);
			ink = caster.bounds;
			ink.origin.x += shadow.offset.x - Points{support};
			ink.origin.y += shadow.offset.y - Points{support};
			ink.size.width += Points{2 * support};
			ink.size.height += Points{2 * support};
		}
		validateRect(ink);
		stana::detail::requireFinite(caster.bounds.origin.x.value() + shadow.offset.x.value());
		stana::detail::requireFinite(caster.bounds.origin.y.value() + shadow.offset.y.value());
		result.push_back(
			{.clip = clip,
				.caster = caster,
				.offset = shadow.offset,
				.color = color,
				.blurRadius = shadow.blurRadius,
				.isInset = shadow.isInset,
				.ink = ink}
		);
	}
	return result;
}
} // namespace stana::rendering::detail

namespace stana::rendering {
auto interpolateBoxShadows(
	std::span<const BoxShadow> from,
	std::span<const BoxShadow> to,
	Progress progress,
	Color foreground,
	Environment environment
) -> std::vector<BoxShadow> {
	stana::detail::requireNonnegative(progress.value());
	if (progress.value() > 1) {
		throw std::invalid_argument("Shadow interpolation progress exceeds one");
	}
	const double progressValue = progress.value();
	detail::validateBoxShadows(from);
	detail::validateBoxShadows(to);
	const std::array foregroundShadow{BoxShadow{.color = foreground}};
	detail::validateBoxShadows(foregroundShadow);
	const auto resolve = [&](BoxShadow shadow) -> BoxShadow {
		shadow.color =
			shadow.color.value_or(foreground).resolved(environment.theme, environment.highContrast);
		return shadow;
	};
	for (std::size_t index = 0; index < std::min(from.size(), to.size()); ++index) {
		if (from.subspan(index, 1).front().isInset != to.subspan(index, 1).front().isInset) {
			const auto selected = progressValue < 0.5 ? from : to;
			std::vector<BoxShadow> result;
			result.reserve(selected.size());
			for (auto shadow : selected) {
				result.push_back(resolve(shadow));
			}
			return result;
		}
	}
	std::vector<BoxShadow> result;
	result.reserve(std::max(from.size(), to.size()));
	for (std::size_t index = 0; index < std::max(from.size(), to.size()); ++index) {
		const bool inset = index < from.size() ? from.subspan(index, 1).front().isInset
											   : to.subspan(index, 1).front().isInset;
		const BoxShadow zero{.color = Color::clear(), .isInset = inset};
		const auto start = resolve(index < from.size() ? from.subspan(index, 1).front() : zero);
		const auto end = resolve(index < to.size() ? to.subspan(index, 1).front() : zero);
		const auto a = start.color.value_or(Color::clear());
		const auto b = end.color.value_or(Color::clear());
		const double alpha = std::lerp(a.alpha.value(), b.alpha.value(), progressValue);
		const auto channel = [&](double first, double second) -> SdrChannel {
			if (alpha <= 0) {
				return SdrChannel{0};
			}
			if (std::is_eq(first <=> second) || std::is_eq(progressValue <=> 0.0)) {
				return SdrChannel{first};
			}
			if (std::is_eq(progressValue <=> 1.0)) {
				return SdrChannel{second};
			}
			return SdrChannel{std::clamp(
				stana::gpu::detail::encodeSRGB(
					std::lerp(
						stana::gpu::detail::decodeSRGB(first) * a.alpha.value(),
						stana::gpu::detail::decodeSRGB(second) * b.alpha.value(),
						progressValue
					) /
					alpha
				),
				0.0,
				1.0
			)};
		};
		result.push_back(
			{.offset =
					{.x = Points{std::lerp(
						 start.offset.x.value(),
						 end.offset.x.value(),
						 progressValue
					 )},
						.y = Points{std::lerp(
							start.offset.y.value(),
							end.offset.y.value(),
							progressValue
						)}},
				.blurRadius =
					Points{
						std::lerp(start.blurRadius.value(), end.blurRadius.value(), progressValue)
					},
				.spreadRadius = Points{std::lerp(
					start.spreadRadius.value(),
					end.spreadRadius.value(),
					progressValue
				)},
				.color = Color::rgb(
					channel(a.red.value(), b.red.value()),
					channel(a.green.value(), b.green.value()),
					channel(a.blue.value(), b.blue.value()),
					Opacity{alpha}
				),
				.isInset = inset}
		);
	}
	return result;
}
} // namespace stana::rendering
