#include "../../../src/rendering/shader/compiler.hpp"
#include <array>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void portableCompiler() {

	constexpr const char *render = R"(
struct Parameters { color: vec4<f32> }
@group(2) @binding(7) var<uniform> params: Parameters;
override gain: f32 = 1.0;
@vertex fn vertexMain(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
 let x = f32(index & 1u) * 2.0 - 1.0;
 let y = f32(index & 2u) - 1.0;
 return vec4<f32>(x, y, 0.0, 1.0);
}
@fragment fn fragmentMain() -> @location(0) vec4<f32> { return params.color * gain; }
)";
	stana::rendering::detail::shader::Request vertex{
		.source = render,
		.entry = "vertexMain",
		.stage = stana::rendering::detail::shader::Stage::vertex
	};
	const auto v = stana::rendering::detail::shader::compile(vertex);
	require(v.has_value(), v ? "" : v.error().message);
	constexpr std::uint64_t uniformBytes = 16;
	constexpr std::uint32_t colorBinding = 7;
	constexpr float overrideGain = 2.0F;
	stana::rendering::detail::shader::Request fragment{
		.source = render,
		.entry = "fragmentMain",
		.stage = stana::rendering::detail::shader::Stage::fragment,
		.bindings =
			{{.group = 2,
				.binding = colorBinding,
				.kind = stana::rendering::detail::shader::Kind::buffer,
				.byteLength = uniformBytes}},
		.constants = {{"gain", overrideGain}}
	};
	const auto f = stana::rendering::detail::shader::compile(fragment);
	require(f.has_value(), f ? "" : f.error().message);
	require(
		!f->source.empty() && f->bindings.size() == 1 && f->bindings.front().slot == 0,
		"private lowering remaps logical bindings"
	);
	fragment.bindings.front().byteLength = 4;
	require(
		!stana::rendering::detail::shader::validateBindings(*f, fragment.bindings),
		"undersized replacement rejected on cache hit"
	);
	require(
		!stana::rendering::detail::shader::compile(fragment),
		"undersized initial binding rejected"
	);
	fragment.bindings.front().byteLength = uniformBytes;
	fragment.constants.insert_or_assign("gain", true);
	require(!stana::rendering::detail::shader::compile(fragment), "exact WGSL override type");
	fragment.constants.clear();
	fragment.bindings.clear();
	require(!stana::rendering::detail::shader::compile(fragment), "missing binding rejected");
	vertex.stage = stana::rendering::detail::shader::Stage::fragment;
	require(!stana::rendering::detail::shader::compile(vertex), "entry stage mismatch rejected");
	vertex.source = "this is not WGSL";
	require(
		!stana::rendering::detail::shader::compile(vertex),
		"invalid WGSL rejected by production compiler"
	);
	constexpr const char *compute = R"(
@group(0) @binding(0) var<storage, read_write> values: array<u32>;
override block: i32 = 1;
@compute @workgroup_size(block) fn step(@builtin(global_invocation_id) id: vec3<u32>) {
 values[id.x] += 1u;
}
)";
	constexpr std::uint64_t storageBytes = 64;
	stana::rendering::detail::shader::Request step{
		.source = compute,
		.entry = "step",
		.stage = stana::rendering::detail::shader::Stage::compute,
		.bindings =
			{{.kind = stana::rendering::detail::shader::Kind::storage,
				.byteLength = storageBytes,
				.writable = true}},
		.constants = {{"block", std::int32_t{4}}}
	};
	const auto lowered = stana::rendering::detail::shader::compile(step);
	require(lowered.has_value(), lowered ? "" : lowered.error().message);
	require(
		lowered->workgroupSize == std::array<std::uint32_t, 3>{4, 1, 1},
		"resolved WGSL workgroup size"
	);
	require(
		lowered->immediateWords.size() >= 4 && lowered->immediateWords.at(4) == storageBytes,
		"runtime storage bounds supplied privately"
	);
	step.bindings.front().writable = false;
	require(
		!stana::rendering::detail::shader::compile(step),
		"read-only descriptor cannot bind writable WGSL storage"
	);
}
} // namespace

auto main() -> int {
	try {
		portableCompiler();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
