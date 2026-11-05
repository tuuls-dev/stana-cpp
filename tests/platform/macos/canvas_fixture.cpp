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
#include "../../../src/application/gpu_conversion.hpp"

namespace stana::native::fixtures {
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
