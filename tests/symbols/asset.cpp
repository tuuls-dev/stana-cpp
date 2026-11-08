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
	path.commands = {
		MoveTo{.x = stana::symbol::AssetUnits{0}, .y = stana::symbol::AssetUnits{0}},
		LineTo{.x = stana::symbol::AssetUnits{lineEndX}, .y = stana::symbol::AssetUnits{lineEndY}}
	};
	path.fill = stana::symbol::NoPaint{};
	path.stroke = stana::symbol::CurrentColor{};
	path.strokeWidth = stana::symbol::AssetUnits{strokeWidth};
	const stana::symbol::AssetRect viewBox{
		.origin =
			stana::symbol::AssetPoint{
				.x = stana::symbol::AssetUnits{viewOriginX},
				.y = stana::symbol::AssetUnits{viewOriginY}},
		.size = stana::symbol::AssetSize{
			.width = stana::symbol::AssetUnits{viewWidth},
			.height = stana::symbol::AssetUnits{viewHeight}}};
	return stana::symbol::Asset::compiled(bridgeVersion, viewBox, false, {Element{.value = path}});
}

void compiledArtworkNormalizesAndRejectsBudgets() {
	const auto asset = icon();
	require(asset.valid() && asset.hasStroke(), "stroke is retained");
	require(same(asset.viewBox().size.width.value(), viewWidth), "viewBox width");
	const auto again = icon();
	require(asset == again, "semantic equality");
	bool rejected = false;
	try {
		const stana::symbol::AssetRect tiny{
			.size = stana::symbol::AssetSize{
				.width = stana::symbol::AssetUnits{1}, .height = stana::symbol::AssetUnits{1}}};
		static_cast<void>(stana::symbol::Asset::compiled(rejectedBridgeVersion, tiny, false, {}));
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	require(rejected, "bridge version");
}

void squareAndPlaybackMatchTheSwiftContract() {
	const stana::Rect content{
		.size = stana::Size{
			.width = stana::Points{contentWidth}, .height = stana::Points{contentHeight}}};
	const auto fitted = stana::symbol::square(stana::Points{fittedSide}, content);
	require(
		same(fitted.size.width.value(), fittedSide) &&
			same(fitted.origin.x.value(), fittedOriginX) &&
			same(fitted.origin.y.value(), fittedOriginY),
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
	state.advance(stana::FrameDuration{firstAdvance});
	require(same(state.sample().rotation.radians(), 0.0), "the reset frame does not catch up");
	state.advance(stana::FrameDuration{secondAdvance});
	const auto sample = state.sample();
	require(
		sample.rotation.radians() > 0.0 && same(sample.scale.value(), 1.0),
		"rotate advances after the reset frame"
	);
	const auto transform =
		stana::AffineTransform::translation(stana::Points{translationX}, stana::Points{translationY})
			.concatenating(stana::AffineTransform::scale(
				stana::ScaleFactor{scaleX},
				stana::ScaleFactor{scaleY}
			));
	const auto point =
		transform.apply({.x = stana::Points{sampleX}, .y = stana::Points{sampleY}});
	require(
		same(point.x.value(), mappedX) && same(point.y.value(), mappedY),
		"concatenating applies scale first"
	);
	require(
		stana::AffineTransform::scale(stana::ScaleFactor{dominantScale}, stana::ScaleFactor{scaleY})
				.maximumScale()
				.value() > dominantScale,
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
