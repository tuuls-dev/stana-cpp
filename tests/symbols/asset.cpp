#include <cmath>
#include <compare>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

import stana;

namespace {
constexpr double viewOriginX = -12;
constexpr double viewOriginY = -4;
constexpr double viewWidth = 24;
constexpr double viewHeight = 8;
constexpr double lineEndX = 4;
constexpr double lineEndY = 5;
constexpr double strokeWidth = 2;
constexpr double contentWidth = 80;
constexpr double contentHeight = 40;
constexpr double fittedSide = 24;
constexpr double fittedOriginX = 28;
constexpr double fittedOriginY = 8;
constexpr double firstAdvance = 0;
constexpr double secondAdvance = 0.25;
constexpr double translationX = 2;
constexpr double translationY = 3;
constexpr double scaleX = 2;
constexpr double scaleY = 4;
constexpr double sampleX = 1;
constexpr double sampleY = 1;
constexpr double mappedX = 4;
constexpr double mappedY = 7;
constexpr double dominantScale = 3;
constexpr std::uint32_t bridgeVersion = 1;
constexpr std::uint32_t rejectedBridgeVersion = 2;

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] auto same(double left, double right) -> bool {
	return std::is_eq(left <=> right);
}

[[nodiscard]] auto icon() -> stana::symbol::Asset {
	using stana::symbol::Element;
	using stana::symbol::LineTo;
	using stana::symbol::MoveTo;
	Element::Path path;
	path.commands = {MoveTo{.x = 0, .y = 0}, LineTo{.x = lineEndX, .y = lineEndY}};
	path.fill = stana::symbol::NoPaint{};
	path.stroke = stana::symbol::CurrentColor{};
	path.strokeWidth = strokeWidth;
	return stana::symbol::Asset::compiled(
		bridgeVersion,
		{.origin = {.x = viewOriginX, .y = viewOriginY},
			.size = {.width = viewWidth, .height = viewHeight}},
		false,
		{Element{.value = path}}
	);
}

void compiledArtworkNormalizesAndRejectsBudgets() {
	const auto asset = icon();
	require(asset.valid() && asset.hasStroke(), "stroke is retained");
	require(same(asset.viewBox().size.width, viewWidth), "viewBox width");
	const auto again = icon();
	require(asset == again, "semantic equality");
	bool rejected = false;
	try {
		static_cast<void>(stana::symbol::Asset::compiled(
			rejectedBridgeVersion,
			{.size = {.width = 1, .height = 1}},
			false,
			{}
		));
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	require(rejected, "bridge version");
}

void squareAndPlaybackMatchTheSwiftContract() {
	const stana::Rect content{.size = {.width = contentWidth, .height = contentHeight}};
	const auto fitted = stana::symbol::square(fittedSide, content);
	require(
		same(fitted.size.width, fittedSide) && same(fitted.origin.x, fittedOriginX) &&
			same(fitted.origin.y, fittedOriginY),
		"square"
	);
	stana::symbol::PlaybackState state;
	state.reconcile(
		false,
		stana::symbol::Playback::loop(stana::symbol::Effect::rotate),
		false,
		true
	);
	require(state.needsFrame() && state.resetClock(), "loop demands a frame at rest");
	state.advance(firstAdvance);
	require(same(state.sample().rotation, 0.0), "the reset frame does not catch up");
	state.advance(secondAdvance);
	const auto sample = state.sample();
	require(
		sample.rotation > 0.0 && same(sample.scale, 1.0),
		"rotate advances after the reset frame"
	);
	const auto transform = stana::AffineTransform::translation(translationX, translationY)
							   .concatenating(stana::AffineTransform::scale(scaleX, scaleY));
	const auto point = transform.apply({.x = sampleX, .y = sampleY});
	require(same(point.x, mappedX) && same(point.y, mappedY), "concatenating applies scale first");
	require(
		stana::AffineTransform::scale(dominantScale, scaleY).maximumScale() > dominantScale,
		"maximum scale"
	);
}
} // namespace

auto main() -> int {
	try {
		compiledArtworkNormalizesAndRejectsBudgets();
		squareAndPlaybackMatchTheSwiftContract();
		const auto bounds = stana::symbol::inkBounds(icon(), std::nullopt);
		require(bounds.has_value(), "ink bounds");
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
