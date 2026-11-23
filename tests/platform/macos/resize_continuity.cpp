#include "window_fixture.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
import stana;

namespace {

namespace fixture = stana::testing::fixtures::platform::macos;

constexpr double cpuWidth = 480;
constexpr double cpuHeight = 320;
constexpr double hybridWidth = 400;
constexpr double hybridHeight = 300;
constexpr double cpuFinalWidth = 360;
constexpr double cpuFinalHeight = 240;
constexpr double hybridFinalWidth = 300;
constexpr double hybridFinalHeight = 220;
constexpr double tinyEdge = 2;
constexpr unsigned burstCount = 5;
constexpr auto pollDelay = std::chrono::milliseconds{40};
constexpr auto presentDelay = std::chrono::milliseconds{600};
constexpr auto scenarioDeadline = std::chrono::seconds{12};
constexpr auto cpuTitle = "Stana resize continuity";
constexpr auto hybridTitle = "Stana resize hybrid";

enum class Stage : std::uint8_t {
	initial,
	burst,
	observe,
	abandon,
	settle,
	backing,
	backingHold,
	backingSettle,
	tiny,
	tinyHold,
	tinySettle,
	closing
};

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] auto ready(const stana::platform::detail::WindowStatistics &statistics) -> bool {
	return statistics.confirmedRevision != 0 &&
		statistics.confirmedRevision == statistics.submittedRevision &&
		!statistics.renderingWork.hasActiveBatch && !statistics.renderingWork.hasPendingScene;
}

[[nodiscard]] auto tileCount(double width, double height, double scale) -> std::uint64_t {
	const auto pixelsWide = static_cast<std::uint64_t>(std::ceil(width * scale));
	const auto pixelsHigh = static_cast<std::uint64_t>(std::ceil(height * scale));
	if (pixelsWide == 0 || pixelsHigh == 0) {
		return 0;
	}
	constexpr std::uint64_t tile = 512;
	return ((pixelsWide + tile - 1) / tile) * ((pixelsHigh + tile - 1) / tile);
}

[[nodiscard]] auto near(double actual, double expected) -> bool {
	return std::abs(actual - expected) < 1.0;
}

void resizeLogical(const std::string &title, double width, double height, double scale) {
	fixture::resizeWindow(title, {.width = width * scale, .height = height * scale});
}

[[nodiscard]] auto picture() -> stana::rendering::GPUDrawing {
	stana::rendering::GPUDrawing drawing;
	drawing.fillRect(
		{.size = {.width = stana::Points{160}, .height = stana::Points{120}}},
		stana::Color::rgb(0.1, 0.45, 0.9)
	);
	return drawing;
}

[[nodiscard]] auto markers() -> stana::rendering::Drawing {
	stana::rendering::Drawing drawing;
	drawing.fill(
		{.size = {.width = stana::Points{cpuWidth}, .height = stana::Points{cpuHeight}}},
		stana::Color::rgb(0.9, 0.2, 0.15)
	);
	return drawing;
}

/// @brief Drives a real window so a delayed present can be observed before it settles.
struct Scenario : std::enable_shared_from_this<Scenario> {
	std::shared_ptr<stana::document::detail::Document> cpu;
	std::shared_ptr<stana::document::detail::Document> hybrid;
	stana::platform::detail::WindowStatistics cpuBefore;
	stana::platform::detail::WindowStatistics hybridBefore;
	double scale = 1;
	std::uint64_t cpuResets = 0;
	std::uint64_t hybridResets = 0;
	std::uint64_t cpuSoft = 0;
	std::uint64_t hybridSoft = 0;
	std::uint64_t cpuPeak = 0;
	Stage stage = Stage::initial;
	bool verified = false;
	std::string failure;
	std::chrono::steady_clock::time_point stageStarted = std::chrono::steady_clock::now();

	void next(Stage nextStage) {
		stage = nextStage;
		stageStarted = std::chrono::steady_clock::now();
	}

	void queue() {
		const auto owner = shared_from_this();
		stana::application::Application::postAfter(pollDelay, [owner] -> void {
			owner->tick();
		});
	}

	void tick() {
		try {
			require(
				std::chrono::steady_clock::now() - stageStarted < scenarioDeadline,
				"resize continuity stage timed out: " + std::to_string(static_cast<unsigned>(stage))
			);
			const auto windows = stana::platform::detail::diagnostics();
			require(windows.size() == 2, "resize continuity keeps two native windows");
			const auto &cpuWindow = windows.front();
			const auto &hybridWindow = windows.back();
			require(
				cpuWindow.failure.empty() && hybridWindow.failure.empty(),
				"native adapter failed during resize"
			);
			if (stage == Stage::initial && ready(cpuWindow) && ready(hybridWindow)) {
				scale = cpuWindow.backingScale;
				require(
					scale > 0 && near(hybridWindow.backingScale, scale),
					"shared backing scale"
				);
				cpuBefore = cpuWindow;
				hybridBefore = hybridWindow;
				cpuResets = fixture::compositorFullResets(cpuTitle);
				hybridResets = fixture::compositorFullResets(hybridTitle);
				cpuSoft = fixture::compositorResizeInvalidations(cpuTitle);
				hybridSoft = fixture::compositorResizeInvalidations(hybridTitle);
				cpuPeak = cpuWindow.renderingWork.peakRasterBytes;
				next(Stage::burst);
			} else if (stage == Stage::burst) {
				fixture::setPresentDelay(cpuTitle, presentDelay);
				fixture::setPresentDelay(hybridTitle, presentDelay);
				for (unsigned index = 0; index < burstCount; ++index) {
					const double grow = static_cast<double>(index) * 24.0;
					resizeLogical(cpuTitle, 420 + grow, 280 + grow, scale);
					resizeLogical(hybridTitle, 360 + grow, 260 + grow, scale);
				}
				next(Stage::observe);
			} else if (stage == Stage::observe) {
				require(
					fixture::surfaceContinuity(cpuTitle) && fixture::surfaceContinuity(hybridTitle),
					"resize burst exposed an empty replacement"
				);
				const auto cpuSoftNow = fixture::compositorResizeInvalidations(cpuTitle);
				const auto hybridSoftNow = fixture::compositorResizeInvalidations(hybridTitle);
				if (!fixture::fallbackCoversContent(cpuTitle) ||
					!fixture::fallbackCoversContent(hybridTitle) || cpuSoftNow == cpuSoft ||
					hybridSoftNow == hybridSoft) {
					queue();
					return;
				}
				require(
					!ready(cpuWindow) && !ready(hybridWindow),
					"delayed present confirmed early"
				);
				require(
					fixture::replacementBackgroundReady(cpuTitle) &&
						fixture::replacementBackgroundReady(hybridTitle),
					"replacement background is missing before the first present"
				);
				require(
					fixture::retainedHostLayers(cpuTitle) <= 2 &&
						fixture::retainedHostLayers(hybridTitle) <= 2,
					"resize kept more than one fallback surface"
				);
				require(
					fixture::compositorFullResets(cpuTitle) == cpuResets &&
						fixture::compositorFullResets(hybridTitle) == hybridResets,
					"size-only resize rebuilt the compositor"
				);
				require(
					cpuSoftNow <= cpuSoft + burstCount && hybridSoftNow <= hybridSoft + burstCount,
					"resize invalidations grew past the burst"
				);
				require(
					cpuWindow.renderingWork.peakRasterBytes <
						cpuPeak + cpuWindow.renderingWork.rasterBytes * 2,
					"resize burst retained extra raster grids"
				);
				resizeLogical(cpuTitle, cpuFinalWidth, cpuFinalHeight, scale);
				resizeLogical(hybridTitle, hybridFinalWidth, hybridFinalHeight, scale);
				next(Stage::abandon);
			} else if (stage == Stage::abandon) {
				require(
					fixture::surfaceContinuity(cpuTitle) && fixture::surfaceContinuity(hybridTitle),
					"abandoned resize present removed the fallback"
				);
				if (!fixture::fallbackCoversContent(cpuTitle) ||
					!fixture::fallbackCoversContent(hybridTitle)) {
					queue();
					return;
				}
				require(
					fixture::compositorFullResets(cpuTitle) == cpuResets &&
						fixture::compositorFullResets(hybridTitle) == hybridResets,
					"abandoned resize rebuilt the compositor"
				);
				fixture::setPresentDelay(cpuTitle, std::chrono::milliseconds{0});
				fixture::setPresentDelay(hybridTitle, std::chrono::milliseconds{0});
				next(Stage::settle);
			} else if (stage == Stage::settle && ready(cpuWindow) && ready(hybridWindow) &&
				fixture::surfaceSettled(cpuTitle) && fixture::surfaceSettled(hybridTitle) &&
				cpuWindow.surfaceEpoch > cpuBefore.surfaceEpoch &&
				hybridWindow.surfaceEpoch > hybridBefore.surfaceEpoch &&
				near(cpuWindow.logicalViewport.width, cpuFinalWidth) &&
				near(cpuWindow.logicalViewport.height, cpuFinalHeight) &&
				near(hybridWindow.logicalViewport.width, hybridFinalWidth) &&
				near(hybridWindow.logicalViewport.height, hybridFinalHeight)) {
				require(
					cpuWindow.renderingWork.tileCount ==
						tileCount(cpuFinalWidth, cpuFinalHeight, scale),
					"cpu resize settled on the wrong tile grid"
				);
				require(
					hybridWindow.renderingWork.tileCount ==
						tileCount(hybridFinalWidth, hybridFinalHeight, scale),
					"hybrid resize settled on the wrong tile grid"
				);
				require(
					hybridWindow.compositedTiles > 0 || hybridWindow.gpuLeafDraws > 0,
					"hybrid window did not composite during resize"
				);
				next(Stage::backing);
			} else if (stage == Stage::backing) {
				fixture::setPresentDelay(cpuTitle, presentDelay);
				fixture::setPresentDelay(hybridTitle, presentDelay);
				fixture::staleBackingScale(cpuTitle);
				fixture::staleBackingScale(hybridTitle);
				next(Stage::backingHold);
			} else if (stage == Stage::backingHold) {
				require(
					fixture::surfaceContinuity(cpuTitle) && fixture::surfaceContinuity(hybridTitle),
					"backing-scale change exposed an empty replacement"
				);
				if (!fixture::fallbackCoversContent(cpuTitle) ||
					!fixture::fallbackCoversContent(hybridTitle)) {
					queue();
					return;
				}
				require(
					fixture::compositorFullResets(cpuTitle) == cpuResets &&
						fixture::compositorFullResets(hybridTitle) == hybridResets,
					"backing-scale change rebuilt the compositor"
				);
				fixture::setPresentDelay(cpuTitle, std::chrono::milliseconds{0});
				fixture::setPresentDelay(hybridTitle, std::chrono::milliseconds{0});
				next(Stage::backingSettle);
			} else if (stage == Stage::backingSettle && ready(cpuWindow) && ready(hybridWindow) &&
				fixture::surfaceSettled(cpuTitle) && fixture::surfaceSettled(hybridTitle) &&
				near(cpuWindow.backingScale, scale) && near(hybridWindow.backingScale, scale)) {
				next(Stage::tiny);
			} else if (stage == Stage::tiny) {
				fixture::setPresentDelay(cpuTitle, presentDelay);
				fixture::setPresentDelay(hybridTitle, presentDelay);
				resizeLogical(cpuTitle, tinyEdge, tinyEdge, scale);
				resizeLogical(hybridTitle, tinyEdge, tinyEdge, scale);
				next(Stage::tinyHold);
			} else if (stage == Stage::tinyHold) {
				require(
					fixture::surfaceContinuity(cpuTitle) && fixture::surfaceContinuity(hybridTitle),
					"tiny resize exposed an empty replacement"
				);
				if (!fixture::fallbackCoversContent(cpuTitle) ||
					!fixture::fallbackCoversContent(hybridTitle)) {
					queue();
					return;
				}
				resizeLogical(cpuTitle, cpuFinalWidth, cpuFinalHeight, scale);
				resizeLogical(hybridTitle, hybridFinalWidth, hybridFinalHeight, scale);
				fixture::setPresentDelay(cpuTitle, std::chrono::milliseconds{0});
				fixture::setPresentDelay(hybridTitle, std::chrono::milliseconds{0});
				next(Stage::tinySettle);
			} else if (stage == Stage::tinySettle && ready(cpuWindow) && ready(hybridWindow) &&
				fixture::surfaceSettled(cpuTitle) && fixture::surfaceSettled(hybridTitle) &&
				near(cpuWindow.logicalViewport.width, cpuFinalWidth) &&
				near(cpuWindow.logicalViewport.height, cpuFinalHeight)) {
				next(Stage::closing);
			} else if (stage == Stage::closing) {
				fixture::setPresentDelay(cpuTitle, presentDelay);
				resizeLogical(cpuTitle, cpuFinalWidth + 40, cpuFinalHeight + 20, scale);
				verified = true;
				fixture::closeWindow(hybridTitle);
				fixture::closeWindow(cpuTitle);
				return;
			}
			queue();
		} catch (const std::exception &error) {
			failure = error.what();
			std::cerr << failure << '\n';
			stana::application::Application::stop();
		}
	}
};

[[nodiscard]] auto runScenario() -> int {
	const auto scenario = std::make_shared<Scenario>();
	scenario->cpu = stana::document::detail::Document::create(
		{.width = stana::Points{cpuWidth}, .height = stana::Points{cpuHeight}}
	);
	scenario->hybrid = stana::document::detail::Document::create(
		{.width = stana::Points{hybridWidth}, .height = stana::Points{hybridHeight}}
	);
	stana::application::detail::installNativeTaskDispatcher();
	const stana::Frame fill{.width = stana::Length::fill(), .height = stana::Length::fill()};
	stana::reactive::Transaction::perform([&] -> void {
		stana::ui::MountContext cpuContext{
			.document = scenario->cpu,
			.parent = scenario->cpu->root()
		};
		static_cast<void>(stana::ui::mount(
			stana::ui::CPUCanvas{.drawing = markers(), .layout = {.frame = fill}},
			cpuContext
		));
		stana::ui::MountContext hybridContext{
			.document = scenario->hybrid,
			.parent = scenario->hybrid->root()
		};
		static_cast<void>(stana::ui::mount(
			stana::ui::GPUCanvas{.drawing = picture(), .layout = {.frame = fill}},
			hybridContext
		));
	});
	scenario->queue();
	std::vector<stana::application::detail::WindowSession> sessions(2);
	sessions.at(0) = {
		.id = stana::application::WindowId::create(),
		.options =
			{.contentSize =
					stana::Size{
						.width = stana::Points{cpuWidth},
						.height = stana::Points{cpuHeight}
					},
				.title = cpuTitle},
		.document = scenario->cpu
	};
	sessions.at(1) = {
		.id = stana::application::WindowId::create(),
		.options =
			{.contentSize =
					stana::Size{
						.width = stana::Points{hybridWidth},
						.height = stana::Points{hybridHeight}
					},
				.title = hybridTitle},
		.document = scenario->hybrid
	};
	const int status = stana::application::detail::runWindows(sessions);
	return status == 0 && scenario->verified && scenario->failure.empty() ? 0 : 1;
}
} // namespace

auto main() -> int {
	try {
		const auto output = fixture::resultPath();
		if (!output.empty()) {
			std::ofstream pid{output + ".pid"};
			pid << fixture::processIdentity();
		}
		int result = 1;
		try {
			result = runScenario();
		} catch (const std::exception &error) {
			result = 1;
			std::cerr << error.what() << '\n';
		}
		if (!output.empty()) {
			std::ofstream status{output};
			status << result;
		}
		return result;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
