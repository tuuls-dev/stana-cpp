#include "canvas_fixture.hpp"
#include "../../../src/graphics/limits.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
import stana;
import stana.core;
#include "../../../src/application/gpu_conversion.hpp"
#include "../../../src/application/scene_conversion.hpp"

namespace stana::native::fixtures {
auto mixedScene(bool extended) -> std::shared_ptr<const Scene> {
	constexpr double extent = 32;
	constexpr double topOrigin = 8;
	constexpr double topExtent = 8;
	constexpr double translucent = 0.5;
	Drawing back;
	Drawing front;
	GPUDrawing picture;
	if (extended) {
		picture.fillRect({.size = {.width = extent, .height = extent}}, ExtendedColor{2, 0, 0});
	} else {
		back.fillRect({.size = {.width = extent, .height = extent}}, Color::rgb(1, 0, 0));
		picture.fillRect(
			{.size = {.width = extent, .height = extent}},
			Color::rgb(0, 1, 0, translucent)
		);
		front.fillRect(
			{.origin = {.x = topOrigin, .y = topOrigin},
				.size = {.width = topExtent, .height = topExtent}},
			Color::rgb(0, 0, 1)
		);
	}
	const Frame fill{.width = Length::fill(), .height = Length::fill()};
	auto document = detail::Document::create({.width = extent, .height = extent});
	Transaction::perform([&] -> void {
		MountContext context{.document = document, .parent = document->root()};
		static_cast<void>(stana::mount(
			ZStack{
				.children = children(
					CPUCanvas{.drawing = back, .layout = {.frame = fill}},
					GPUCanvas{.drawing = picture, .layout = {.frame = fill}},
					CPUCanvas{.drawing = front, .layout = {.frame = fill}}
				)
			},
			context
		));
	});
	document->commit();
	application_detail::SceneConversion conversion;
	const auto result = conversion.convert(document->renderSnapshot());
	document->close();
	return result;
}

auto counterPrograms() -> std::vector<std::shared_ptr<const gpu::Program>> {
	const auto shader = GPUShader::source(R"(
@group(0) @binding(0) var previous: texture_2d<f32>;
@group(0) @binding(1) var next: texture_storage_2d<rgba16float, write>;
@compute @workgroup_size(1) fn step() {
 let value = textureLoad(previous, vec2<i32>(0, 0), 0).x;
 textureStore(next, vec2<i32>(0, 0), vec4<f32>(value + 1.0, 0.0, 0.0, 1.0));
}
)");
	if (!shader) {
		throw std::runtime_error(shader.error().message);
	}
	const auto state = GPUStateSlot::create({.width = 1, .height = 1});
	if (!state) {
		throw std::runtime_error(state.error().message);
	}
	const GPUProgramDescriptor descriptor{
		.shader = *shader,
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
		.output = "history"
	};
	auto program = GPUProgram::create(descriptor);
	if (!program) {
		throw std::runtime_error(program.error().message);
	}
	application_detail::GPUConversion conversion;
	std::vector<std::shared_ptr<const gpu::Program>> result;
	constexpr std::size_t steps = 1000;
	result.reserve(steps);
	for (std::size_t index = 0; index < steps; ++index) {
		result.push_back(conversion.program(*program));
		program = program->updatingResources(descriptor.resources);
		if (!program) {
			throw std::runtime_error(program.error().message);
		}
	}
	return result;
}
} // namespace stana::native::fixtures
