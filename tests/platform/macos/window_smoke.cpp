#include "../../../src/platform/native_contracts.hpp"
#include "window_fixture.hpp"
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
import stana;
import stana.core;

namespace {
using namespace stana;
constexpr double initialWidth = 800;
constexpr double initialHeight = 600;
constexpr double padding = 24;
constexpr double smallWidth = 320;
constexpr double smallHeight = 200;
constexpr std::uint64_t fullTilePayload = std::uint64_t{512} * 512 * 4;
constexpr auto pollDelay = std::chrono::milliseconds{50};
constexpr auto scenarioDeadline = std::chrono::seconds{8};
constexpr auto primaryTitle = "Stana tile qualification";
constexpr auto secondaryTitle = "Stana independent window";

constexpr fixtures::PhysicalViewport edgeViewport{.width = 1025, .height = 769};
enum class Stage : std::uint8_t {
	initial,
	sparse,
	anchor,
	smallResize,
	reattach,
	occluded,
	revealed,
	edge
};

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

/// @brief Drives actual production sessions; synthetic evidence cannot satisfy this fixture.
struct Scenario : std::enable_shared_from_this<Scenario> {
	Signal<Color> foreground{Color::black()};
	Signal<bool> anchor{false};
	Signal<std::string> secondaryText{std::string{"independent"}};
	std::shared_ptr<detail::Document> document;
	std::shared_ptr<detail::Document> otherDocument;
	native::WindowStatistics before;
	native::WindowStatistics otherBefore;
	Metrics coreBefore;
	Stage stage = Stage::initial;
	bool verified = false;
	bool signaledReady = false;
	std::string failure;
	std::chrono::steady_clock::time_point stageStarted = std::chrono::steady_clock::now();

	void next(Stage nextStage) {
		stage = nextStage;
		stageStarted = std::chrono::steady_clock::now();
	}

	void queue() {
		const auto owner = shared_from_this();
		Application::postAfter(pollDelay, [owner] -> void {
			owner->tick();
		});
	}

	[[nodiscard]] static auto ready(const native::WindowStatistics &statistics) -> bool {
		return statistics.confirmedRevision != 0 &&
			statistics.confirmedRevision == statistics.submittedRevision &&
			!statistics.renderingWork.hasActiveBatch && !statistics.renderingWork.hasPendingScene;
	}

	void record(const native::WindowStatistics &statistics) const {
		const auto &work = statistics.renderingWork;
		std::cout << "stage=" << static_cast<unsigned>(stage)
				  << " epoch=" << statistics.surfaceEpoch
				  << " confirmed=" << statistics.confirmedRevision
				  << " viewportPoints=" << statistics.logicalViewport.width << 'x'
				  << statistics.logicalViewport.height << " scale=" << statistics.backingScale
				  << " tiles=" << work.tileCount << " rasterBytes=" << work.rasterBytes
				  << " peak=" << work.peakRasterBytes
				  << " estimatedDrawables=" << work.estimatedDrawableBytes
				  << " copied=" << work.copiedBytes << '\n';
	}

	void tick() {
		try {
			require(
				std::chrono::steady_clock::now() - stageStarted < scenarioDeadline,
				"native stage timed out: " + std::to_string(static_cast<unsigned>(stage))
			);
			const auto windows = native::diagnostics();
			require(windows.size() == 2, "two independent native windows remain attached");
			if (!signaledReady) {
				const auto path = fixtures::resultPath();
				if (!path.empty()) {
					std::ofstream readyFile{path + ".ready"};
					readyFile << "ready";
				}
				signaledReady = true;
			}
			const auto &current = windows.front();
			const auto &other = windows.back();
			require(current.failure.empty() && other.failure.empty(), "native adapter failure");
			if (stage == Stage::initial && ready(current) && ready(other)) {
				record(current);
				before = current;
				otherBefore = other;
				coreBefore = document->metrics();
				foreground.set(Color::accent());
				next(Stage::sparse);
			} else if (stage == Stage::sparse && ready(current) &&
				current.confirmedRevision > before.confirmedRevision) {
				const auto &work = current.renderingWork;
				require(
					work.rasterizedTiles == before.renderingWork.rasterizedTiles + 1 &&
						work.copiedBytes == before.renderingWork.copiedBytes + fullTilePayload,
					"one in-tile color edit rasters/copies one tile"
				);
				require(
					work.bufferAllocations == before.renderingWork.bufferAllocations &&
						work.cpuUploadBytes == 0,
					"warm shared buffers reused without image upload"
				);
				require(
					current.drawableAcquisitions.size() == 1 &&
						current.drawableAcquisitions.front().value == 0,
					"actual acquisition record selects tile zero only"
				);
				require(
					current.preparedPaints.size() == 1,
					"only changed local native paint prepared"
				);
				const auto core = document->metrics();
				require(
					core.measurements == coreBefore.measurements &&
						core.arrangements == coreBefore.arrangements &&
						core.paintBuilds == coreBefore.paintBuilds + 1 &&
						core.sceneMaterializations == 0 && core.nodeMaterializations == 0,
					"end-to-end sparse core and no native flat materialization"
				);
				require(
					other.submittedRevision == otherBefore.submittedRevision &&
						other.renderingWork.bufferAllocations ==
							otherBefore.renderingWork.bufferAllocations,
					"other window work/evidence isolated"
				);
				record(current);
				before = current;
				anchor.set(true);
				next(Stage::anchor);
			} else if (stage == Stage::anchor && ready(current) &&
				current.confirmedRevision > before.confirmedRevision) {
				require(
					current.renderingWork.rasterizedTiles == before.renderingWork.rasterizedTiles &&
						current.renderingWork.anchorAttempts ==
							before.renderingWork.anchorAttempts + 1 &&
						current.renderingWork.copiedBytes ==
							before.renderingWork.copiedBytes + fullTilePayload,
					"paint-equivalent revision uses a real anchor, no raster"
				);
				record(current);
				before = current;
				fixtures::resizeWindow(primaryTitle, edgeViewport);
				next(Stage::smallResize);
			} else if (stage == Stage::smallResize && ready(current) &&
				current.surfaceEpoch > before.surfaceEpoch) {
				constexpr std::uint64_t smallEdgeTiles = 6;
				require(
					current.renderingWork.tileCount == smallEdgeTiles,
					"onscreen partial-edge resize uses six initialized tiles"
				);
				record(current);
				before = current;
				fixtures::replaceAttachment(primaryTitle);
				next(Stage::reattach);
			} else if (stage == Stage::reattach && ready(current) &&
				current.surfaceEpoch > before.surfaceEpoch) {
				require(
					current.confirmedRevision == before.confirmedRevision,
					"replacement attachment confirms the same scene revision in a new epoch"
				);
				record(current);
				before = current;
				fixtures::showWindow(primaryTitle, false);
				constexpr unsigned publications = 129;
				for (unsigned index = 0; index < publications; ++index) {
					foreground.set(index % 2 == 0 ? Color::black() : Color::accent());
				}
				next(Stage::occluded);
			} else if (stage == Stage::occluded) {
				require(
					current.submittedRevision == before.submittedRevision &&
						current.renderingWork.rasterizedTiles ==
							before.renderingWork.rasterizedTiles &&
						current.renderingWork.hasPendingScene,
					"occluded publications coalesce without acquisition/raster"
				);
				fixtures::showWindow(primaryTitle, true);
				next(Stage::revealed);
			} else if (stage == Stage::revealed && ready(current) &&
				current.confirmedRevision > before.confirmedRevision) {
				record(current);
				before = current;
				constexpr fixtures::PhysicalViewport restoredViewport{.width = 1200, .height = 900};
				fixtures::resizeWindow(primaryTitle, restoredViewport);
				next(Stage::edge);
			} else if (stage == Stage::edge && ready(current) &&
				current.surfaceEpoch > before.surfaceEpoch) {
				constexpr std::uint64_t edgeTiles = 6;
				require(
					current.renderingWork.tileCount == edgeTiles &&
						current.renderingWork.cpuUploadBytes == 0,
					"partial edge grid fully initialized without upload"
				);
				record(current);
				verified = true;
				secondaryText.set("close with pending publication");
				fixtures::closeWindow(secondaryTitle);
				fixtures::closeWindow(primaryTitle);
				return;
			}
			queue();
		} catch (const std::exception &error) {
			failure = error.what();
			std::cerr << failure << '\n';
			Application::stop();
		}
	}
};

/// @brief Asymmetric markers cross physical tile edges at both tested scales.
[[nodiscard]] auto markers() -> Drawing {
	Drawing drawing;
	constexpr double step = 128;
	constexpr double markerWidth = 8;
	constexpr double markerHeight = 12;
	constexpr double blueOffset = 10;
	constexpr double blueExtent = 4;
	constexpr unsigned rows = 5;
	constexpr unsigned columns = 7;
	for (unsigned row = 0; row < rows; ++row) {
		const auto y = static_cast<double>(row) * step;
		for (unsigned column = 0; column < columns; ++column) {
			const auto x = static_cast<double>(column) * step;
			drawing.fill(
				{.origin = {.x = x, .y = y},
					.size = {.width = markerWidth, .height = markerHeight}},
				Color::rgb(1, 0, 0)
			);
			drawing.fill(
				{.origin = {.x = x + blueOffset, .y = y},
					.size = {.width = blueExtent, .height = blueExtent}},
				Color::rgb(0, 0, 1)
			);
		}
	}
	return drawing;
}

[[nodiscard]] auto runScenario() -> int {
	const auto scenario = std::make_shared<Scenario>();
	scenario->document = detail::Document::create({.width = initialWidth, .height = initialHeight});
	scenario->otherDocument =
		detail::Document::create({.width = smallWidth, .height = smallHeight});
	detail::installNativeTaskDispatcher();
	Transaction::perform([&] -> void {
		MountContext mainContext{
			.document = scenario->document,
			.parent = scenario->document->root()
		};
		static_cast<void>(mount(
			ZStack{
				.children = children(
					Canvas{
						.drawing = markers(),
						.layout =
							{.frame = Frame{.width = Length::fill(), .height = Length::fill()}}
					},
					VStack{
						.layout =
							{.padding = Insets::all(padding),
								.frame = Frame{.width = Length::fill(), .height = Length::fill()}},
						.children = children(
							Text{
								.text = "sparse label",
								.textStyle = {.foreground = scenario->foreground}
							},
							Toggle{.text = "anchor", .value = scenario->anchor.binding()}
						)
					}
				)
			},
			mainContext
		));
		MountContext otherContext{
			.document = scenario->otherDocument,
			.parent = scenario->otherDocument->root()
		};
		static_cast<void>(mount(Text{.text = scenario->secondaryText}, otherContext));
	});
	scenario->queue();
	const int status = detail::runWindows(
		{{.title = primaryTitle,
			 .size = {.width = initialWidth, .height = initialHeight},
			 .document = scenario->document},
			{.title = secondaryTitle,
				.size = {.width = smallWidth, .height = smallHeight},
				.document = scenario->otherDocument}}
	);
	require(
		scenario->document->snapshots().empty() && scenario->otherDocument->snapshots().empty(),
		"closed documents cannot resurrect"
	);
	return status == 0 && scenario->verified && scenario->failure.empty() ? 0 : 1;
}
} // namespace

/// @brief Bundle-launched real tile test. The launcher reads an explicit result, never open(1)'s
/// status alone.
auto main() -> int {
	try {
		const auto output = stana::fixtures::resultPath();
		if (!output.empty()) {
			std::ofstream pid{output + ".pid"};
			pid << stana::fixtures::processIdentity();
		}
		int result = 1;
		try {
			result = runScenario();
		} catch (const std::exception &error) {
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
