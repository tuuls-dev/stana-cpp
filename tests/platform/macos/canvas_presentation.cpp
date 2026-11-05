#include "window_fixture.hpp"
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>
import stana;
import stana.core;

namespace {
using namespace stana;
constexpr double width = 520;
constexpr double height = 260;
constexpr double extent = 128;
constexpr auto delay = std::chrono::milliseconds{50};
constexpr auto timeout = std::chrono::seconds{10};

[[nodiscard]] auto picture() -> GPUDrawing {
	const auto shader = GPUShader::source(R"(
@group(0) @binding(0) var history: texture_2d<f32>;
@group(0) @binding(1) var next: texture_storage_2d<rgba16float, write>;
@compute @workgroup_size(1) fn step() {
 textureStore(next, vec2<i32>(0), vec4<f32>(textureLoad(history, vec2<i32>(0), 0).x + 1.0, 0.0, 0.0, 1.0));
}
)");
	const auto state = GPUStateSlot::create({.width = 1, .height = 1});
	if (!shader || !state) {
		throw std::runtime_error("Invalid canvas qualification resources");
	}
	const auto program = GPUProgram::create(
		{.shader = *shader,
			.resources = {{"history", *state}},
			.passes = {GPUComputePass{
				.function = "step",
				.bindings =
					{{.resource = "history", .stage = GPUBinding::Stage::compute, .previous = true},
						{.resource = "history",
							.binding = 1,
							.access = GPUBinding::Access::write,
							.stage = GPUBinding::Stage::compute}}
			}},
			.output = "history"}
	);
	if (!program) {
		throw std::runtime_error(program.error().message);
	}
	GPUDrawing result{*program};
	result.fillRect({.size = {.width = extent, .height = extent}}, ExtendedColor{2, 0, 0});
	return result;
}

struct Scenario : std::enable_shared_from_this<Scenario> {
	Signal<GPUDrawing> drawing{picture()};
	Signal<bool> metadata{false};
	std::shared_ptr<detail::Document> document;
	WindowDiagnostics before;
	Metrics coreBefore;
	unsigned stage = 0;
	bool verified = false;
	bool announced = false;
	std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

	void queue() {
		const auto owner = shared_from_this();
		Application::postAfter(delay, [owner] -> void {
			owner->tick();
		});
	}

	void tick() {
		try {
			if (std::chrono::steady_clock::now() - started > timeout) {
				throw std::runtime_error("Canvas presentation stage timed out");
			}
			if (!announced) {
				const auto path = fixtures::resultPath();
				if (!path.empty()) {
					std::ofstream ready{path + ".ready"};
					ready << "ready";
				}
				announced = true;
			}
			const auto windows = Application::renderingDiagnostics();
			if (windows.size() != 1) {
				queue();
				return;
			}
			const auto &current = windows.front();
			if (current.lastFailure) {
				throw std::runtime_error(*current.lastFailure);
			}
			const bool ready = current.confirmedSceneRevision &&
				current.confirmedSceneRevision == current.submittedSceneRevision &&
				!current.renderingWork.hasActiveBatch && !current.renderingWork.hasPendingScene;
			if (!ready) {
				queue();
				return;
			}
			if (stage == 0) {
				if (current.stateSteps == 0) {
					queue();
					return;
				}
				if (current.offscreenRevision == 0 || current.compositedTiles == 0 ||
					current.gpuLeafDraws == 0 || !current.linearWorking || current.linearOutput) {
					throw std::runtime_error(
						"HDR mixed output was not mapped once to the requested SDR display profile"
					);
				}
				before = current;
				coreBefore = document->metrics();
				auto next = drawing.get();
				next.fillEllipse(
					{.origin = {.x = extent / 2, .y = extent / 2},
						.size = {.width = extent, .height = extent}},
					ExtendedColor{0, 2, 0}
				);
				drawing.set(std::move(next));
				++stage;
				started = std::chrono::steady_clock::now();
			} else if (stage == 1 &&
				current.confirmedSceneRevision > before.confirmedSceneRevision) {
				const auto core = document->metrics();
				if (core.mounts != coreBefore.mounts ||
					core.measurements != coreBefore.measurements ||
					core.arrangements != coreBefore.arrangements ||
					current.stateSteps != before.stateSteps ||
					current.renderingWork.rasterizedPixels !=
						before.renderingWork.rasterizedPixels) {
					throw std::runtime_error(
						"GPU-only update rebuilt unrelated CPU content or layout"
					);
				}
				before = current;
				metadata.set(true);
				++stage;
				started = std::chrono::steady_clock::now();
			} else if (stage == 2 &&
				current.confirmedSceneRevision > before.confirmedSceneRevision) {
				if (current.compositedTiles != before.compositedTiles ||
					current.resourceUploadBytes != before.resourceUploadBytes ||
					current.stateSteps != before.stateSteps) {
					throw std::runtime_error(
						"Paint-equivalent anchor performed new GPU content work"
					);
				}
				std::cout << "Native canvas display confirmed revision="
						  << *current.confirmedSceneRevision << " device=" << current.deviceName
						  << " trackedBytes=" << current.trackedBytes
						  << " tiles=" << current.compositedTiles << '\n';
				verified = true;
				Application::stop();
				return;
			}
			queue();
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			Application::stop();
		}
	}
};

[[nodiscard]] auto run() -> int {
	auto scenario = std::make_shared<Scenario>();
	scenario->document = detail::Document::create({.width = width, .height = height});
	Drawing background;
	background.fillRect({.size = {.width = extent, .height = extent}}, Color::accent());
	const Frame fill{.width = Length::fill(), .height = Length::fill()};
	Transaction::perform([&] -> void {
		MountContext context{.document = scenario->document, .parent = scenario->document->root()};
		static_cast<void>(mount(
			ZStack{
				.children = children(
					CPUCanvas{.drawing = background, .layout = {.frame = fill}},
					GPUCanvas{.drawing = scenario->drawing, .layout = {.frame = fill}},
					VStack{
						.children = children(
							Text{.text = "CPU above GPU"},
							Toggle{.text = "metadata anchor", .value = scenario->metadata.binding()}
						)
					}
				)
			},
			context
		));
	});
	scenario->queue();
	const int status = detail::runWindows(
		{{.title = "Stana canvas qualification",
			.size = {.width = width, .height = height},
			.renderingColorMode = RenderingColorMode::sdr,
			.document = scenario->document}}
	);
	return status == 0 && scenario->verified ? 0 : 1;
}
} // namespace

auto main() -> int {
	try {
		const auto path = stana::fixtures::resultPath();
		if (!path.empty()) {
			std::ofstream pid{path + ".pid"};
			pid << stana::fixtures::processIdentity();
		}
		int status = 1;
		try {
			status = run();
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
		}
		if (!path.empty()) {
			std::ofstream result{path};
			result << status;
		}
		return status;

	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
