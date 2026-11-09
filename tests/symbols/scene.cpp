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
constexpr stana::FrameDuration quarterTurn{0.25};
constexpr stana::FrameDuration halfSecond{0.5};
constexpr stana::FrameDuration threeQuarters{0.75};
constexpr stana::FrameDuration oneSecond{1};
constexpr stana::FrameDuration fiveQuarters{1.25};
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

[[nodiscard]] auto at(stana::FrameDuration offset) -> stana::FrameTimePoint {
	return stana::FrameTimePoint{offset};
}

[[nodiscard]] auto icon(bool mirrorsInRTL) -> stana::symbol::Asset {
	using stana::symbol::Element;
	Element::Path path;
	path.commands = {
		stana::symbol::MoveTo{
			.x = stana::symbol::AssetUnits{viewOriginX},
			.y = stana::symbol::AssetUnits{viewOriginY}
		},
		stana::symbol::LineTo{
			.x = stana::symbol::AssetUnits{viewWidth + viewOriginX},
			.y = stana::symbol::AssetUnits{viewHeight + viewOriginY}
		}
	};
	path.fill = stana::symbol::NoPaint{};
	path.stroke = stana::symbol::CurrentColor{};
	path.strokeWidth = stana::symbol::AssetUnits{strokeWidth};
	const stana::symbol::AssetRect viewBox{
		.origin =
			stana::symbol::AssetPoint{
				.x = stana::symbol::AssetUnits{viewOriginX},
				.y = stana::symbol::AssetUnits{viewOriginY}
			},
		.size = stana::symbol::AssetSize{
			.width = stana::symbol::AssetUnits{viewWidth},
			.height = stana::symbol::AssetUnits{viewHeight}
		}
	};
	return stana::symbol::Asset::compiled(
		bridgeVersion,
		viewBox,
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
		.origin = stana::Point{.x = stana::Points{placedX}, .y = stana::Points{placedY}},
		.size =
			stana::Size{.width = stana::Points{placedWidth}, .height = stana::Points{placedHeight}}
	};
	const auto ink = stana::symbol::inkBounds(asset, std::nullopt);
	const auto presentation = stana::symbol::presentationFor(
		asset,
		stana::Points{symbolSize},
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
	const auto topLeft =
		placed.transform.apply({.x = stana::Points{viewOriginX}, .y = stana::Points{viewOriginY}});
	const auto bottomRight = placed.transform.apply(
		{.x = stana::Points{viewWidth + viewOriginX}, .y = stana::Points{viewHeight + viewOriginY}}
	);
	require(
		same(topLeft.x.value(), fittedTopLeftX) && same(topLeft.y.value(), fittedTopLeftY) &&
			same(bottomRight.x.value(), fittedBottomRightX) &&
			same(bottomRight.y.value(), fittedBottomRightY),
		"viewBox aspect-fits into the centered square"
	);
}

void measurementIgnoresTextScale() {
	stana::Environment environment;
	environment.textScale = stana::ScaleFactor{textScale};
	stana::testing::TestHost host{
		stana::Size{
			.width = stana::Points{placedWidth * textScale},
			.height = stana::Points{placedHeight * textScale}
		},
		environment
	};
	host.mount(
		stana::ui::VStack{
			.children = stana::ui::children(
				stana::symbol::Symbol{
					.asset = icon(false),
					.layout = {.padding = stana::Insets::all(stana::Points{padding})}
				}
			)
		}
	);
	const auto node = findKind(host, "symbol");
	const auto baseline = host.symbolBaseline(node.id);
	require(node.kind == "symbol", "zero and positive symbols share a kind");
	require(
		same(node.bounds.size.width.value(), measuredSide) &&
			same(node.bounds.size.height.value(), measuredSide),
		"padding grows the intrinsic square"
	);
	require(
		baseline.has_value() && same(baseline->value(), measuredBaseline),
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
					.size = stana::Points{zeroSide},
					.layout = {.padding = stana::Insets::all(stana::Points{padding})}
				}
			)
		}
	);
	const auto node = findKind(host, "symbol");
	const auto baseline = host.symbolBaseline(node.id);
	require(
		same(node.bounds.size.width.value(), zeroMeasured) &&
			same(node.bounds.size.height.value(), zeroMeasured),
		"zero artwork keeps padding"
	);
	require(
		baseline.has_value() && same(baseline->value(), zeroBaseline),
		"zero baseline is the top padding"
	);
	require(!painted(host).has_value(), "zero square omits the leaf");
}

void sizeChangesMeasureWithoutRemounting() {
	const stana::reactive::Signal size{stana::Points{symbolSize}};
	stana::testing::TestHost host;
	host.mount(
		stana::ui::VStack{
			.children = stana::ui::children(
				stana::symbol::Symbol{
					.asset = icon(false),
					.size = size,
					.layout = {.padding = stana::Insets::all(stana::Points{padding})}
				}
			)
		}
	);
	const auto before = findKind(host, "symbol");
	size.set(stana::Points{shrunkSize});
	host.flush();
	const auto after = findKind(host, "symbol");
	require(after.id == before.id, "size updates the same occurrence");
	require(
		same(after.bounds.size.width.value(), shrunkMeasured) &&
			same(after.bounds.size.height.value(), shrunkMeasured),
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
					.size = stana::Points{symbolSize},
					.layout = {
						.frame = stana::Frame{
							.width = stana::Length::points(stana::Points{placedWidth}),
							.height = stana::Length::points(stana::Points{placedHeight})
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
		stana::Points{symbolSize},
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
	stana::testing::TestHost host{
		stana::Size{.width = stana::Points{placedWidth}, .height = stana::Points{placedHeight}},
		environment
	};
	host.mount(stana::symbol::Symbol{.asset = icon(true), .size = stana::Points{symbolSize}});
	require(
		requirePaint(host).transform.a.value() < 0.0,
		"RTL mirror negates the horizontal scale"
	);
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
	host.tickFrames({.timestamp = at(stana::FrameDuration{0}), .targetTimestamp = at(quarterTurn)});
	require(same(requirePaint(host).transform.b.value(), 0.0), "the reset frame stays at rest");
	host.tickFrames({.timestamp = at(quarterTurn), .targetTimestamp = at(halfSecond)});
	require(requirePaint(host).transform.b.value() > 0.0, "the next frame rotates");
}

void reducedMotionReturnsToRest() {
	stana::testing::TestHost host;
	host.mount(
		stana::symbol::Symbol{
			.asset = icon(false),
			.animation = stana::symbol::Playback::loop(stana::symbol::Effect::rotate)
		}
	);
	host.tickFrames({.timestamp = at(stana::FrameDuration{0}), .targetTimestamp = at(quarterTurn)});
	host.tickFrames({.timestamp = at(quarterTurn), .targetTimestamp = at(halfSecond)});
	stana::Environment environment;
	environment.reducedMotion = true;
	host.setEnvironment(environment);
	require(!host.frameDemand(), "reduced motion releases the clock");
	require(
		same(requirePaint(host).transform.b.value(), 0.0),
		"reduced motion paints the resting pose"
	);
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
	constexpr stana::FrameDuration motionStep{0.1};
	constexpr stana::FrameDuration secondStep{0.2};
	constexpr stana::FrameDuration thirdStep{0.3};
	constexpr stana::FrameDuration fourthStep{0.4};
	host.tickFrames({.timestamp = at(stana::FrameDuration{0}), .targetTimestamp = at(motionStep)});
	host.tickFrames({.timestamp = at(motionStep), .targetTimestamp = at(secondStep)});
	const double advanced = requirePaint(host).transform.b.value();
	require(advanced > 0.0, "a new once-request leaves rest");
	animation.set(stana::symbol::Playback::once(stana::symbol::Effect::rotate, firstRequest));
	host.flush();
	host.tickFrames({.timestamp = at(secondStep), .targetTimestamp = at(thirdStep)});
	const double continued = requirePaint(host).transform.b.value();
	require(continued > advanced, "the same once-request keeps playing");
	animation.set(stana::symbol::Playback::once(stana::symbol::Effect::rotate, secondRequest));
	host.flush();
	host.tickFrames({.timestamp = at(thirdStep), .targetTimestamp = at(fourthStep)});
	require(
		same(requirePaint(host).transform.b.value(), 0.0),
		"a different once-request restarts at rest"
	);
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
	host.tickFrames({.timestamp = at(stana::FrameDuration{0}), .targetTimestamp = at(quarterTurn)});
	host.tickFrames({.timestamp = at(quarterTurn), .targetTimestamp = at(halfSecond)});
	const auto paused = requirePaint(host);
	require(paused.transform.b.value() > 0.0, "the host paints motion before hiding");
	host.setHostVisible(false);
	require(!host.frameDemand(), "a hidden host pauses playback");
	require(
		same(requirePaint(host).transform.a.value(), paused.transform.a.value()) &&
			same(requirePaint(host).transform.b.value(), paused.transform.b.value()),
		"hiding keeps the current pose"
	);
	host.tickFrames({.timestamp = at(halfSecond), .targetTimestamp = at(threeQuarters)});
	require(
		same(requirePaint(host).transform.a.value(), paused.transform.a.value()) &&
			same(requirePaint(host).transform.b.value(), paused.transform.b.value()),
		"a hidden host does not advance"
	);
	host.setHostVisible(true);
	require(host.frameDemand(), "showing the host resumes playback");
	host.tickFrames({.timestamp = at(threeQuarters), .targetTimestamp = at(oneSecond)});
	require(
		same(requirePaint(host).transform.a.value(), paused.transform.a.value()) &&
			same(requirePaint(host).transform.b.value(), paused.transform.b.value()),
		"resume drops the first elapsed sample"
	);
	host.tickFrames({.timestamp = at(oneSecond), .targetTimestamp = at(fiveQuarters)});
	const auto resumed = requirePaint(host);
	require(
		!same(resumed.transform.a.value(), paused.transform.a.value()) ||
			!same(resumed.transform.b.value(), paused.transform.b.value()),
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
	host.tickFrames({.timestamp = at(stana::FrameDuration{0}), .targetTimestamp = at(quarterTurn)});
	host.tickFrames({.timestamp = at(quarterTurn), .targetTimestamp = at(halfSecond)});
	require(requirePaint(host).transform.b.value() > 0.0, "the loop leaves rest before failure");
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
	require(same(requirePaint(host).transform.b.value(), 0.0), "a recovered loop restarts at rest");
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
	require(same(requirePaint(host).transform.b.value(), 0.0), "restored playback starts at rest");
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
