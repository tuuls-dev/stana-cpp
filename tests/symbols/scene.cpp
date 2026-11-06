#include <compare>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

import stana;

namespace {
constexpr std::uint32_t bridgeVersion = 1;
constexpr double viewOriginX = -12;
constexpr double viewOriginY = -6;
constexpr double viewWidth = 24;
constexpr double viewHeight = 12;
constexpr double strokeWidth = 2;
constexpr double symbolSize = 24;
constexpr double padding = 2;
constexpr double textScale = 3;
constexpr double measuredSide = 28;
constexpr double measuredBaseline = 26;
constexpr double zeroSide = 0;
constexpr double zeroMeasured = 4;
constexpr double zeroBaseline = 2;
constexpr double placedX = 50;
constexpr double placedY = 50;
constexpr double placedWidth = 80;
constexpr double placedHeight = 40;
constexpr double fittedTopLeftX = 78;
constexpr double fittedTopLeftY = 64;
constexpr double fittedBottomRightX = 102;
constexpr double fittedBottomRightY = 76;
constexpr double quarterTurn = 0.25;
constexpr double halfSecond = 0.5;
constexpr double threeQuarters = 0.75;
constexpr double oneSecond = 1;
constexpr double fiveQuarters = 1.25;
constexpr double shrunkSize = 10;
constexpr double shrunkMeasured = 14;
constexpr std::uint64_t firstRequest = 1;
constexpr std::uint64_t secondRequest = 2;

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] auto same(double left, double right) -> bool {
	return std::is_eq(left <=> right);
}

[[nodiscard]] auto icon(bool mirrorsInRTL) -> stana::symbol::Asset {
	using stana::symbol::Element;
	Element::Path path;
	path.commands = {
		stana::symbol::MoveTo{.x = viewOriginX, .y = viewOriginY},
		stana::symbol::LineTo{.x = viewWidth + viewOriginX, .y = viewHeight + viewOriginY}
	};
	path.fill = stana::symbol::NoPaint{};
	path.stroke = stana::symbol::CurrentColor{};
	path.strokeWidth = strokeWidth;
	return stana::symbol::Asset::compiled(
		bridgeVersion,
		{.origin = {.x = viewOriginX, .y = viewOriginY},
			.size = {.width = viewWidth, .height = viewHeight}},
		mirrorsInRTL,
		{Element{.value = path}}
	);
}

[[nodiscard]] auto findKind(const stana::testing::TestHost &host, const std::string &kind)
	-> stana::document::NodeSnapshot {
	const auto nodes = host.nodes();
	for (const auto &node : nodes) {
		if (node.kind == kind) {
			return node;
		}
	}
	throw std::runtime_error("Missing node: " + kind);
}

[[nodiscard]] auto painted(const stana::testing::TestHost &host)
	-> std::optional<stana::symbol::Presentation> {
	for (const auto &paint : host.mixedPaint()) {
		if (const auto *presentation = std::get_if<stana::symbol::Presentation>(&paint)) {
			return *presentation;
		}
	}
	return std::nullopt;
}

[[nodiscard]] auto requirePaint(const stana::testing::TestHost &host)
	-> stana::symbol::Presentation {
	const auto leaf = painted(host);
	if (!leaf.has_value()) {
		throw std::runtime_error("missing symbol presentation");
	}
	return leaf.value_or(stana::symbol::Presentation{});
}

void fittedSquareMatchesTheSwiftPlacement() {
	const auto asset = icon(false);
	const stana::Rect content{
		.origin = {.x = placedX, .y = placedY},
		.size = {.width = placedWidth, .height = placedHeight}
	};
	const auto ink = stana::symbol::inkBounds(asset, std::nullopt);
	const auto presentation = stana::symbol::presentationFor(
		asset,
		symbolSize,
		content,
		stana::Direction::leftToRight,
		{},
		stana::Color::black(),
		std::nullopt,
		bridgeVersion,
		true,
		ink
	);
	require(presentation.has_value(), "fitted artwork has a presentation");
	const auto placed = presentation.value_or(stana::symbol::Presentation{});
	const auto topLeft = placed.transform.apply({.x = viewOriginX, .y = viewOriginY});
	const auto bottomRight =
		placed.transform.apply({.x = viewWidth + viewOriginX, .y = viewHeight + viewOriginY});
	require(
		same(topLeft.x, fittedTopLeftX) && same(topLeft.y, fittedTopLeftY) &&
			same(bottomRight.x, fittedBottomRightX) && same(bottomRight.y, fittedBottomRightY),
		"viewBox aspect-fits into the centered square"
	);
}

void measurementIgnoresTextScale() {
	stana::Environment environment;
	environment.textScale = textScale;
	stana::testing::TestHost host{
		{.width = placedWidth * textScale, .height = placedHeight * textScale},
		environment
	};
	host.mount(
		stana::ui::VStack{
			.children = stana::ui::children(
				stana::symbol::Symbol{
					.asset = icon(false),
					.layout = {.padding = stana::Insets::all(padding)}
				}
			)
		}
	);
	const auto node = findKind(host, "symbol");
	const auto baseline = host.symbolBaseline(node.id);
	require(node.kind == "symbol", "zero and positive symbols share a kind");
	require(
		same(node.bounds.size.width, measuredSide) && same(node.bounds.size.height, measuredSide),
		"padding grows the intrinsic square"
	);
	require(
		baseline.has_value() && same(*baseline, measuredBaseline),
		"baseline includes top padding"
	);
	require(!node.accessibilityLabel.has_value(), "omitted label is decorative");
}

void zeroSizePaintsNothing() {
	stana::testing::TestHost host;
	host.mount(
		stana::ui::VStack{
			.children = stana::ui::children(
				stana::symbol::Symbol{
					.asset = icon(false),
					.size = zeroSide,
					.layout = {.padding = stana::Insets::all(padding)}
				}
			)
		}
	);
	const auto node = findKind(host, "symbol");
	const auto baseline = host.symbolBaseline(node.id);
	require(
		same(node.bounds.size.width, zeroMeasured) && same(node.bounds.size.height, zeroMeasured),
		"zero artwork keeps padding"
	);
	require(
		baseline.has_value() && same(*baseline, zeroBaseline),
		"zero baseline is the top padding"
	);
	require(!painted(host).has_value(), "zero square omits the leaf");
}

void sizeChangesMeasureWithoutRemounting() {
	const stana::reactive::Signal<double> size{symbolSize};
	stana::testing::TestHost host;
	host.mount(
		stana::ui::VStack{
			.children = stana::ui::children(
				stana::symbol::Symbol{
					.asset = icon(false),
					.size = size,
					.layout = {.padding = stana::Insets::all(padding)}
				}
			)
		}
	);
	const auto before = findKind(host, "symbol");
	size.set(shrunkSize);
	host.flush();
	const auto after = findKind(host, "symbol");
	require(after.id == before.id, "size updates the same occurrence");
	require(
		same(after.bounds.size.width, shrunkMeasured) &&
			same(after.bounds.size.height, shrunkMeasured),
		"size invalidates measurement"
	);
}

void mountedTransformFollowsLayout() {
	stana::testing::TestHost host;
	host.mount(
		stana::ui::VStack{
			.children = stana::ui::children(
				stana::symbol::Symbol{
					.asset = icon(false),
					.size = symbolSize,
					.layout = {
						.frame = stana::Frame{
							.width = stana::Length::points(placedWidth),
							.height = stana::Length::points(placedHeight)
						}
					}
				}
			)
		}
	);
	const auto node = findKind(host, "symbol");
	const auto leaf = requirePaint(host);
	const auto expected = stana::symbol::presentationFor(
		icon(false),
		symbolSize,
		node.bounds,
		stana::Direction::leftToRight,
		{},
		stana::Color::text().resolved(stana::Theme::light),
		std::nullopt,
		leaf.generation,
		true,
		stana::symbol::inkBounds(icon(false), std::nullopt)
	);
	const auto fitted = expected.value_or(stana::symbol::Presentation{});
	require(
		expected.has_value() && leaf.transform == fitted.transform,
		"scene uses the fitted transform"
	);
	require(leaf.color == stana::Color::black(), "light text token resolves to black");
}

void rightToLeftMirrorsWhenRequested() {
	stana::Environment environment;
	environment.direction = stana::Direction::rightToLeft;
	stana::testing::TestHost host{{.width = placedWidth, .height = placedHeight}, environment};
	host.mount(stana::symbol::Symbol{.asset = icon(true), .size = symbolSize});
	require(requirePaint(host).transform.a < 0.0, "RTL mirror negates the horizontal scale");
}

void loopAdvancesOnlyAfterTheResetFrame() {
	stana::testing::TestHost host;
	host.mount(
		stana::symbol::Symbol{
			.asset = icon(false),
			.animation = stana::symbol::Playback::loop(stana::symbol::Effect::rotate)
		}
	);
	require(host.frameDemand(), "a visible loop requests frames");
	host.tickFrames({.timestamp = 0, .targetTimestamp = quarterTurn});
	require(same(requirePaint(host).transform.b, 0.0), "the reset frame stays at rest");
	host.tickFrames({.timestamp = quarterTurn, .targetTimestamp = halfSecond});
	require(requirePaint(host).transform.b > 0.0, "the next frame rotates");
}

void reducedMotionReturnsToRest() {
	stana::testing::TestHost host;
	host.mount(
		stana::symbol::Symbol{
			.asset = icon(false),
			.animation = stana::symbol::Playback::loop(stana::symbol::Effect::rotate)
		}
	);
	host.tickFrames({.timestamp = 0, .targetTimestamp = quarterTurn});
	host.tickFrames({.timestamp = quarterTurn, .targetTimestamp = halfSecond});
	stana::Environment environment;
	environment.reducedMotion = true;
	host.setEnvironment(environment);
	require(!host.frameDemand(), "reduced motion releases the clock");
	require(same(requirePaint(host).transform.b, 0.0), "reduced motion paints the resting pose");
}

void hiddenAndCollapsedAncestorsPause() {
	const stana::reactive::Signal hidden{stana::Visibility::hidden};
	stana::testing::TestHost hiddenHost;
	hiddenHost.mount(
		stana::ui::Box{
			.appearance = {.visibility = hidden},
			.children = stana::ui::children(
				stana::symbol::Symbol{
					.asset = icon(false),
					.animation = stana::symbol::Playback::loop(stana::symbol::Effect::rotate)
				}
			)
		}
	);
	require(!hiddenHost.frameDemand(), "a hidden ancestor pauses playback");
	hidden.set(stana::Visibility::visible);
	hiddenHost.flush();
	require(hiddenHost.frameDemand(), "showing the ancestor resumes playback");

	stana::testing::TestHost collapsedHost;
	collapsedHost.mount(
		stana::ui::Box{
			.appearance = {.visibility = stana::Visibility::collapsed},
			.children = stana::ui::children(
				stana::symbol::Symbol{
					.asset = icon(false),
					.animation = stana::symbol::Playback::loop(stana::symbol::Effect::rotate)
				}
			)
		}
	);
	require(!collapsedHost.frameDemand(), "a collapsed ancestor pauses playback");
}

void equalOnceRequestsDoNotReplay() {
	const stana::reactive::Signal animation{
		stana::symbol::Playback::once(stana::symbol::Effect::rotate, firstRequest)
	};
	stana::testing::TestHost host;
	host.mount(stana::symbol::Symbol{.asset = icon(false), .animation = animation});
	constexpr double motionStep = 0.1;
	constexpr double secondStep = 0.2;
	constexpr double thirdStep = 0.3;
	constexpr double fourthStep = 0.4;
	host.tickFrames({.timestamp = 0, .targetTimestamp = motionStep});
	host.tickFrames({.timestamp = motionStep, .targetTimestamp = secondStep});
	const double advanced = requirePaint(host).transform.b;
	require(advanced > 0.0, "a new once-request leaves rest");
	animation.set(stana::symbol::Playback::once(stana::symbol::Effect::rotate, firstRequest));
	host.flush();
	host.tickFrames({.timestamp = secondStep, .targetTimestamp = thirdStep});
	const double continued = requirePaint(host).transform.b;
	require(continued > advanced, "the same once-request keeps playing");
	animation.set(stana::symbol::Playback::once(stana::symbol::Effect::rotate, secondRequest));
	host.flush();
	host.tickFrames({.timestamp = thirdStep, .targetTimestamp = fourthStep});
	require(same(requirePaint(host).transform.b, 0.0), "a different once-request restarts at rest");
}

void hiddenHostPausesPlayback() {
	stana::testing::TestHost host;
	host.mount(
		stana::symbol::Symbol{
			.asset = icon(false),
			.animation = stana::symbol::Playback::loop(stana::symbol::Effect::rotate)
		}
	);
	require(host.frameDemand(), "a visible host requests frames");
	host.tickFrames({.timestamp = 0, .targetTimestamp = quarterTurn});
	host.tickFrames({.timestamp = quarterTurn, .targetTimestamp = halfSecond});
	const auto paused = requirePaint(host);
	require(paused.transform.b > 0.0, "the host paints motion before hiding");
	host.setHostVisible(false);
	require(!host.frameDemand(), "a hidden host pauses playback");
	require(
		same(requirePaint(host).transform.a, paused.transform.a) &&
			same(requirePaint(host).transform.b, paused.transform.b),
		"hiding keeps the current pose"
	);
	host.tickFrames({.timestamp = halfSecond, .targetTimestamp = threeQuarters});
	require(
		same(requirePaint(host).transform.a, paused.transform.a) &&
			same(requirePaint(host).transform.b, paused.transform.b),
		"a hidden host does not advance"
	);
	host.setHostVisible(true);
	require(host.frameDemand(), "showing the host resumes playback");
	host.tickFrames({.timestamp = threeQuarters, .targetTimestamp = oneSecond});
	require(
		same(requirePaint(host).transform.a, paused.transform.a) &&
			same(requirePaint(host).transform.b, paused.transform.b),
		"resume drops the first elapsed sample"
	);
	host.tickFrames({.timestamp = oneSecond, .targetTimestamp = fiveQuarters});
	const auto resumed = requirePaint(host);
	require(
		!same(resumed.transform.a, paused.transform.a) ||
			!same(resumed.transform.b, paused.transform.b),
		"the following frame advances"
	);
}

void failureRetriesOnTheNextRendererEpoch() {
	stana::testing::TestHost host;
	int reports = 0;
	host.mount(
		stana::symbol::Symbol{
			.asset = icon(false),
			.animation = stana::symbol::Playback::loop(stana::symbol::Effect::rotate),
			.onError = [&](const stana::symbol::Diagnostic &) -> void {
				++reports;
			}
		}
	);
	host.tickFrames({.timestamp = 0, .targetTimestamp = quarterTurn});
	host.tickFrames({.timestamp = quarterTurn, .targetTimestamp = halfSecond});
	require(requirePaint(host).transform.b > 0.0, "the loop leaves rest before failure");
	const auto node = findKind(host, "symbol");
	const auto generation = host.symbolGeneration(node.id);
	if (!generation) {
		throw std::runtime_error("the mounted symbol has a generation");
	}
	const std::uint64_t generationValue = generation.value();
	const stana::symbol::Diagnostic limit{
		.code = stana::symbol::Diagnostic::Code::resourceLimitExceeded,
		.message = "limit"
	};
	host.injectSymbolDiagnostic(limit, node.id, generationValue);
	require(reports == 1, "the first failure is delivered once");
	require(!host.frameDemand(), "a failed symbol releases the clock");
	require(!painted(host).has_value(), "a failed symbol paints nothing");
	require(findKind(host, "symbol").id == node.id, "failure keeps the mounted symbol");
	host.injectSymbolDiagnostic(limit, node.id, generationValue);
	require(reports == 1, "the same failure is not delivered twice");
	host.replaceSurface();
	const auto recovered = host.symbolGeneration(node.id);
	const auto epoch = host.symbolEpoch(node.id);
	if (!recovered || !epoch) {
		throw std::runtime_error("the new surface adopts the symbol");
	}
	const std::uint64_t recoveredValue = recovered.value();
	const std::uint64_t epochValue = epoch.value();
	require(recoveredValue != generationValue, "a new surface advances the symbol generation");
	require(host.frameDemand(), "recovery restarts a loop");
	require(same(requirePaint(host).transform.b, 0.0), "a recovered loop restarts at rest");
	host.injectSymbolDiagnostic(
		{.code = stana::symbol::Diagnostic::Code::renderingFailed, .message = "stale"},
		node.id,
		generationValue,
		epochValue
	);
	require(reports == 1, "a stale generation is discarded");
	require(host.frameDemand(), "a stale failure does not stop the recovered loop");
	host.injectSymbolDiagnostic(
		{.code = stana::symbol::Diagnostic::Code::renderingFailed, .message = "raster"},
		node.id,
		recoveredValue,
		epochValue
	);
	require(reports == 2, "the new preparation reports its own failure");
	require(!painted(host).has_value(), "the new failure omits paint");
	host.injectSymbolSuccess(node.id, recoveredValue, epochValue);
	require(host.frameDemand(), "a successful raster restores playback");
	require(same(requirePaint(host).transform.b, 0.0), "restored playback starts at rest");
}

void themeResolvesCurrentColor() {
	stana::testing::TestHost host;
	host.mount(stana::symbol::Symbol{.asset = icon(false)});
	stana::Environment environment;
	environment.theme = stana::Theme::dark;
	host.setEnvironment(environment);
	require(
		requirePaint(host).color == stana::Color::text().resolved(stana::Theme::dark),
		"currentColor follows the document theme"
	);
}
} // namespace

auto main() -> int {
	try {
		fittedSquareMatchesTheSwiftPlacement();
		measurementIgnoresTextScale();
		zeroSizePaintsNothing();
		sizeChangesMeasureWithoutRemounting();
		mountedTransformFollowsLayout();
		rightToLeftMirrorsWhenRequested();
		loopAdvancesOnlyAfterTheResetFrame();
		reducedMotionReturnsToRest();
		hiddenAndCollapsedAncestorsPause();
		hiddenHostPausesPlayback();
		equalOnceRequestsDoNotReplay();
		failureRetriesOnTheNextRendererEpoch();
		themeResolvesCurrentColor();
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
