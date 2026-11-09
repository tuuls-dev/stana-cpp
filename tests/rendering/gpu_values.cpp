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

namespace {
void require(bool value, const std::string &message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

void appendAfterMove(stana::rendering::GPUDrawing &value) {
	value.fillEllipse(
		{.size = {.width = stana::Points{1}, .height = stana::Points{1}}},
		stana::Color::white()
	);
}

void drawingValues() {

	stana::rendering::GPUDrawing drawing;
	const auto old = drawing;
	drawing.fillRect(
		{.size = {.width = stana::Points{2}, .height = stana::Points{2}}},
		stana::rendering::ExtendedColor{0, 2, 0}
	);
	require(
		!(drawing == old) && old == stana::rendering::GPUDrawing{old},
		"COW immutable drawing identity"
	);
	require(
		!(stana::rendering::GPUDrawing{} == stana::rendering::GPUDrawing{}),
		"independent empty versions"
	);
	const auto valid = drawing;
	bool rejected = false;
	try {
		drawing.fillRect(
			{.size = {.width = stana::Points{-1}, .height = stana::Points{1}}},
			stana::Color::black()
		);
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	require(rejected && drawing == valid, "failed append preserves version");
	const std::array points{
		stana::Point{.x = stana::Points{1}, .y = stana::Points{1}},
		stana::Point{.x = stana::Points{1}, .y = stana::Points{1}},
		stana::Point{.x = stana::Points{1}, .y = stana::Points{1}}
	};
	drawing.strokePolyline(points, stana::Color::accent());
	const stana::rendering::GPUDrawing moved{std::move(drawing)};
	appendAfterMove(drawing);
	require(!(drawing == moved), "moved-from drawing reusable");
	const std::array vertices{
		stana::rendering::GPUVertex{
			.position = {.x = stana::Points{0}, .y = stana::Points{0}},
			.color = stana::rendering::ExtendedColor{1, 0, 0}
		},
		stana::rendering::GPUVertex{
			.position = {.x = stana::Points{1}, .y = stana::Points{0}},
			.color = stana::rendering::ExtendedColor{0, 1, 0}
		},
		stana::rendering::GPUVertex{
			.position = {.x = stana::Points{0}, .y = stana::Points{1}},
			.color = stana::rendering::ExtendedColor{0, 0, 1}
		}
	};
	const std::array<std::uint32_t, 3> indices{0, 1, 2};
	const auto mesh = stana::rendering::GPUMesh::create(vertices, indices);
	require(mesh.has_value() && *mesh == stana::rendering::GPUMesh{*mesh}, "owned mesh factory");
	const std::array<std::uint32_t, 3> bad{0, 1, 3};
	require(!stana::rendering::GPUMesh::create(vertices, bad), "mesh index validation");
	const stana::reactive::Signal model{moved};
	stana::testing::TestHost host;
	host.mount(
		stana::ui::GPUCanvas{
			.drawing = model,
			.layout = {
				.padding = stana::Insets::all(stana::Points{1}),
				.frame = stana::Frame{
					.width = stana::Length::points(stana::Points{4}),
					.height = stana::Length::points(stana::Points{4})
				}
			}
		}
	);
	const auto metrics = host.metrics();
	const auto paint = host.mixedPaint();
	require(
		paint.size() == 3 && std::holds_alternative<stana::testing::GPUCanvasPaint>(paint.at(1)),
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
		std::get<stana::testing::GPUCanvasPaint>(incremental.at(1)).drawing ==
			std::get<stana::testing::GPUCanvasPaint>(rebuilt.at(1)).drawing,
		"full/patch same GPU version"
	);
}

void resourcesAndGraph() {

	const auto shader =
		stana::rendering::GPUShader::source("@compute @workgroup_size(1) fn step() {}");
	require(shader.has_value(), "owned shader source");
	require(
		!stana::rendering::GPUTexture::create({.width = 1, .height = 1}),
		"texture exact bytes"
	);
	require(
		!stana::rendering::GPUTexture::create(
			{.width = std::numeric_limits<std::uint32_t>::max(),
				.height = std::numeric_limits<std::uint32_t>::max()}
		),
		"overflow rejected before product"
	);
	const std::vector<std::byte> bytes(4);
	const auto texture =
		stana::rendering::GPUTexture::create({.width = 1, .height = 1, .bytes = bytes});
	require(texture.has_value(), "valid transparent texture");
	const auto state =
		stana::rendering::GPUStateSlot::create({.width = 1, .height = 1, .resetKey = "initial"});
	require(state.has_value(), "zero initializer state");
	constexpr auto unknown = std::numeric_limits<std::uint8_t>::max();
	require(
		!stana::rendering::GPUTexture::create(
			{.width = 1,
				.height = 1,
				.format = static_cast<stana::rendering::GPUTextureFormat>(unknown),
				.bytes = bytes}
		),
		"unknown texture enum rejected"
	);
	require(
		!stana::rendering::GPUTexture::create(
			{.width = 1,
				.height = 1,
				.bytes = bytes,
				.encoding = static_cast<stana::rendering::GPUColorEncoding>(unknown)}
		),
		"unknown encoding rejected"
	);
	stana::rendering::GPUProgramDescriptor malformed{
		.shader = *shader,
		.passes = {stana::rendering::GPURenderPass{.vertex = "vs", .fragment = "fs"}}
	};
	malformed.outputColor = static_cast<stana::rendering::GPUColorEncoding>(unknown);
	require(!stana::rendering::GPUProgram::create(malformed), "unknown output encoding rejected");
	malformed.outputColor = stana::rendering::GPUColorEncoding::extendedLinear;
	malformed.resources.emplace(
		"sampler",
		stana::rendering::GPUSampler{
			.filter = static_cast<stana::rendering::GPUSampler::Filter>(unknown)
		}
	);
	require(!stana::rendering::GPUProgram::create(malformed), "unknown sampler rejected");
	malformed.resources.erase("sampler");
	for (const bool access : {false, true}) {
		auto &pass = std::get<stana::rendering::GPURenderPass>(malformed.passes.front());
		stana::rendering::GPUBinding binding{.resource = "output"};
		if (access) {
			binding.access = static_cast<stana::rendering::GPUBinding::Access>(unknown);
		} else {
			binding.stage = static_cast<stana::rendering::GPUBinding::Stage>(unknown);
		}
		pass.bindings = {binding};
		require(!stana::rendering::GPUProgram::create(malformed), "unknown binding enum rejected");
	}
	stana::rendering::GPUProgramDescriptor descriptor{.shader = *shader};
	descriptor.resources.emplace("history", *state);
	descriptor.resources.emplace("temporary", stana::rendering::GPUTarget{});
	descriptor.passes = {
		stana::rendering::GPURenderPass{
			.vertex = "vs",
			.fragment = "fs",
			.bindings = {{.resource = "temporary"}}
		},
		stana::rendering::GPUComputePass{
			.function = "step",
			.bindings = {
				{.resource = "temporary",
					.access = stana::rendering::GPUBinding::Access::write,
					.stage = stana::rendering::GPUBinding::Stage::compute},
				{.resource = "history",
					.group = 1,
					.stage = stana::rendering::GPUBinding::Stage::compute,
					.previous = true}
			}
		}
	};
	const auto program = stana::rendering::GPUProgram::create(descriptor);
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
	auto &binding =
		std::get<stana::rendering::GPUComputePass>(descriptor.passes.back()).bindings.front();
	binding.resource = "output";
	require(!stana::rendering::GPUProgram::create(descriptor), "multiple writers rejected");
	binding.resource = "temporary";
	std::get<stana::rendering::GPURenderPass>(descriptor.passes.front()).bindings.front().resource =
		"history";
	require(!stana::rendering::GPUProgram::create(descriptor), "unproduced current state rejected");
	std::get<stana::rendering::GPURenderPass>(descriptor.passes.front()).bindings.front().resource =
		"temporary";
	descriptor.passes.emplace_back(
		stana::rendering::GPURenderPass{
			.vertex = "vs",
			.fragment = "fs",
			.target = "history",
			.bindings = {{.resource = "output"}}
		}
	);
	std::get<stana::rendering::GPUComputePass>(descriptor.passes.at(1)).bindings.back().previous =
		false;
	require(!stana::rendering::GPUProgram::create(descriptor), "cycle rejected across all passes");
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
