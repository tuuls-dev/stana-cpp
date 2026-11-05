#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>
import stana;
import stana.testing;

namespace {
void require(bool value, const std::string &message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

void appendAfterMove(stana::GPUDrawing &value) {
	value.fillEllipse({.size = {.width = 1, .height = 1}}, stana::Color::white());
}

void drawingValues() {
	using namespace stana;
	GPUDrawing drawing;
	const auto old = drawing;
	drawing.fillRect({.size = {.width = 2, .height = 2}}, ExtendedColor{0, 2, 0});
	require(!(drawing == old) && old == GPUDrawing{old}, "COW immutable drawing identity");
	require(!(GPUDrawing{} == GPUDrawing{}), "independent empty versions");
	const auto valid = drawing;
	bool rejected = false;
	try {
		drawing.fillRect({.size = {.width = -1, .height = 1}}, Color::black());
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	require(rejected && drawing == valid, "failed append preserves version");
	const std::array points{Point{.x = 1, .y = 1}, Point{.x = 1, .y = 1}, Point{.x = 1, .y = 1}};
	drawing.strokePolyline(points, Color::accent());
	const GPUDrawing moved{std::move(drawing)};
	appendAfterMove(drawing);
	require(!(drawing == moved), "moved-from drawing reusable");
	const std::array vertices{
		GPUVertex{.position = {.x = 0, .y = 0}, .color = ExtendedColor{1, 0, 0}},
		GPUVertex{.position = {.x = 1, .y = 0}, .color = ExtendedColor{0, 1, 0}},
		GPUVertex{.position = {.x = 0, .y = 1}, .color = ExtendedColor{0, 0, 1}}
	};
	const std::array<std::uint32_t, 3> indices{0, 1, 2};
	const auto mesh = GPUMesh::create(vertices, indices);
	require(mesh.has_value() && *mesh == GPUMesh{*mesh}, "owned mesh factory");
	const std::array<std::uint32_t, 3> bad{0, 1, 3};
	require(!GPUMesh::create(vertices, bad), "mesh index validation");
	const Signal model{moved};
	TestHost host;
	host.mount(
		GPUCanvas{
			.drawing = model,
			.layout = {
				.padding = Insets::all(1),
				.frame = Frame{.width = Length::points(4), .height = Length::points(4)}
			}
		}
	);
	const auto metrics = host.metrics();
	const auto paint = host.mixedPaint();
	require(
		paint.size() == 3 && std::holds_alternative<GPUCanvasPaint>(paint.at(1)),
		"typed mixed leaf between clip commands"
	);
	require(host.scene()->commands.size() == 2, "CPU-only flattening excludes GPU pixels");
	model.set(moved);
	require(host.metrics().sceneRevisions == metrics.sceneRevisions, "same GPU version no-op");
	model.set(drawing);
	require(
		host.metrics().mounts == metrics.mounts &&
			host.metrics().measurements == metrics.measurements &&
			host.metrics().arrangements == metrics.arrangements,
		"GPU paint-only update"
	);
	const auto incremental = host.mixedPaint();
	host.forceFullRecompute();
	const auto rebuilt = host.mixedPaint();
	require(
		std::get<GPUCanvasPaint>(incremental.at(1)).drawing ==
			std::get<GPUCanvasPaint>(rebuilt.at(1)).drawing,
		"full/patch same GPU version"
	);
}

void resourcesAndGraph() {
	using namespace stana;
	const auto shader = GPUShader::source("@compute @workgroup_size(1) fn step() {}");
	require(shader.has_value(), "owned shader source");
	require(!GPUTexture::create({.width = 1, .height = 1}), "texture exact bytes");
	require(
		!GPUTexture::create(
			{.width = std::numeric_limits<std::uint32_t>::max(),
				.height = std::numeric_limits<std::uint32_t>::max()}
		),
		"overflow rejected before product"
	);
	const std::vector<std::byte> bytes(4);
	const auto texture = GPUTexture::create({.width = 1, .height = 1, .bytes = bytes});
	require(texture.has_value(), "valid transparent texture");
	const auto state = GPUStateSlot::create({.width = 1, .height = 1, .resetKey = "initial"});
	require(state.has_value(), "zero initializer state");
	GPUProgramDescriptor descriptor{.shader = *shader};
	descriptor.resources.emplace("history", *state);
	descriptor.resources.emplace("temporary", GPUTarget{});
	descriptor.passes = {
		GPURenderPass{.vertex = "vs", .fragment = "fs", .bindings = {{.resource = "temporary"}}},
		GPUComputePass{
			.function = "step",
			.bindings = {
				{.resource = "temporary",
					.access = GPUBinding::Access::write,
					.stage = GPUBinding::Stage::compute},
				{.resource = "history",
					.group = 1,
					.stage = GPUBinding::Stage::compute,
					.previous = true}
			}
		}
	};
	const auto program = GPUProgram::create(descriptor);
	require(program.has_value(), "forward producer and previous-state read accepted");
	require(
		program->executionOrder().front() == 1 && program->executionOrder().back() == 0,
		"stable topological execution"
	);
	const auto replacement = program->updatingResources(descriptor.resources);
	require(
		replacement.has_value() && replacement->version() != program->version() &&
			replacement->pipelineVersion() == program->pipelineVersion() &&
			replacement->stateVersion() == program->stateVersion(),
		"resource update identities"
	);
	auto &binding = std::get<GPUComputePass>(descriptor.passes.back()).bindings.front();
	binding.resource = "output";
	require(!GPUProgram::create(descriptor), "multiple writers rejected");
	binding.resource = "temporary";
	std::get<GPURenderPass>(descriptor.passes.front()).bindings.front().resource = "history";
	require(!GPUProgram::create(descriptor), "unproduced current state rejected");
	std::get<GPURenderPass>(descriptor.passes.front()).bindings.front().resource = "temporary";
	descriptor.passes.emplace_back(
		GPURenderPass{
			.vertex = "vs",
			.fragment = "fs",
			.target = "history",
			.bindings = {{.resource = "output"}}
		}
	);
	std::get<GPUComputePass>(descriptor.passes.at(1)).bindings.back().previous = false;
	require(!GPUProgram::create(descriptor), "cycle rejected across all passes");
}
} // namespace

auto main() -> int {
	try {
		drawingValues();
		resourcesAndGraph();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
