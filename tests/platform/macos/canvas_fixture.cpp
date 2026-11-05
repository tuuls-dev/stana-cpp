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

auto builtinScenes() -> std::vector<std::shared_ptr<const Scene>> {
	constexpr double width = 1031;
	constexpr double height = 173;
	constexpr double quarter = 0.25;
	constexpr double rounded = 48;
	constexpr double radius = 12;
	constexpr double ellipseX = 64;
	constexpr double ellipseWidth = 80;
	constexpr double ellipseHeight = 32;
	constexpr double top = 8;
	constexpr double meshX = 200;
	constexpr double meshExtent = 80;
	GPUDrawing picture;
	picture.fillRect(
		{.origin = {.x = quarter, .y = quarter}, .size = {.width = rounded, .height = rounded}},
		ExtendedColor{1, 0, 0},
		radius
	);
	picture.fillEllipse(
		{.origin = {.x = ellipseX + quarter, .y = top + quarter},
			.size = {.width = ellipseWidth, .height = ellipseHeight}},
		ExtendedColor{0, 1, 0}
	);
	const std::array vertices{
		GPUVertex{
			.position = {.x = meshX + quarter, .y = top + quarter},
			.color = ExtendedColor{1, 0, 0}
		},
		GPUVertex{
			.position = {.x = meshX + meshExtent + quarter, .y = top + quarter},
			.color = ExtendedColor{0, 1, 0}
		},
		GPUVertex{
			.position = {.x = meshX + quarter, .y = top + meshExtent + quarter},
			.color = ExtendedColor{0, 0, 1}
		}
	};
	const std::array<std::uint32_t, 3> indices{0, 1, 2};
	const auto mesh = GPUMesh::create(vertices, indices);
	if (!mesh) {
		throw std::runtime_error(mesh.error().message);
	}
	picture.drawMesh(*mesh);
	constexpr double lineStart = 400;
	constexpr double lineEnd = 560;
	constexpr double lineY = 30;
	constexpr double lineBottom = 90;
	constexpr double lineWidth = 10;
	const std::array line{
		Point{.x = lineStart, .y = lineY},
		Point{.x = lineStart, .y = lineY},
		Point{.x = lineEnd, .y = lineY},
		Point{.x = lineEnd, .y = lineBottom}
	};
	picture.strokePolyline(line, ExtendedColor{1, 0, 1}, lineWidth);
	constexpr double dotX = 700;
	const std::array point{Point{.x = dotX, .y = lineY}};
	picture.strokePolyline(point, ExtendedColor{0, 1, 1}, radius);
	const Signal model{picture};
	const Frame fill{.width = Length::fill(), .height = Length::fill()};
	auto document = detail::Document::create({.width = width, .height = height});
	Transaction::perform([&] -> void {
		MountContext context{.document = document, .parent = document->root()};
		static_cast<void>(
			stana::mount(GPUCanvas{.drawing = model, .layout = {.frame = fill}}, context)
		);
	});
	document->commit();
	application_detail::SceneConversion conversion;
	std::vector<std::shared_ptr<const Scene>> scenes{
		conversion.convert(document->renderSnapshot())
	};
	constexpr double changeX = 490.25;
	constexpr double changeY = 110.5;
	constexpr double changeWidth = 70;
	constexpr double changeHeight = 30;
	picture.fillRect(
		{.origin = {.x = changeX, .y = changeY},
			.size = {.width = changeWidth, .height = changeHeight}},
		ExtendedColor{1, 1, 0}
	);
	model.set(picture);
	document->commit();
	scenes.push_back(conversion.convert(document->renderSnapshot()));
	document->close();
	return scenes;
}

auto counterScenes() -> std::vector<std::shared_ptr<const Scene>> {
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
	constexpr double extent = 32;
	const Frame fill{.width = Length::fill(), .height = Length::fill()};
	auto document = detail::Document::create({.width = extent, .height = extent});
	const Signal model{GPUDrawing{*program}};
	Transaction::perform([&] -> void {
		MountContext context{.document = document, .parent = document->root()};
		static_cast<void>(
			stana::mount(GPUCanvas{.drawing = model, .layout = {.frame = fill}}, context)
		);
	});
	application_detail::SceneConversion conversion;
	std::vector<std::shared_ptr<const Scene>> result;
	constexpr std::size_t steps = 1000;
	result.reserve(steps);
	for (std::size_t index = 0; index < steps; ++index) {
		document->commit();
		result.push_back(conversion.convert(document->renderSnapshot()));
		program = program->updatingResources(descriptor.resources);
		if (!program) {
			throw std::runtime_error(program.error().message);
		}
		model.set(GPUDrawing{*program});
	}
	document->close();
	return result;
}
} // namespace stana::native::fixtures
