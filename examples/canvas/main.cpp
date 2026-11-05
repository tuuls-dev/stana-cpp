#include "canvas_resource.hpp"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import stana;
import stana.testing;

namespace {
using namespace stana;
constexpr double canvasWidth = 320;
constexpr double canvasHeight = 180;
constexpr double padding = 20;
constexpr double spacing = 12;
constexpr double barHeight = 80;
constexpr double barWidth = 40;
constexpr double peak = 2;
constexpr std::uint32_t fieldSize = 128;
constexpr std::uint32_t groups = fieldSize / 8;
constexpr std::uint32_t nextBinding = 5;

// Factories report recoverable descriptor errors; this demo treats startup/update
// failures as application errors and prints their owned diagnostic in main.
template <typename T> auto checked(std::expected<T, GPUError> result) -> T {
	if (!result) {
		throw std::runtime_error(result.error().message);
	}
	return std::move(*result);
}

auto uniform(float value) -> GPUBuffer {
	constexpr std::size_t alignment = 16;
	constexpr unsigned byteBits = 8;
	constexpr std::uint32_t mask = 255;
	const auto bits = std::bit_cast<std::uint32_t>(value);
	std::array<std::byte, alignment> bytes{};
	for (std::size_t index = 0; index < sizeof(bits); ++index) {
		bytes.at(index) = static_cast<std::byte>((bits >> (index * byteBits)) & mask);
	}
	return checked(GPUBuffer::create(bytes));
}

auto program() -> GPUProgram {
	const auto shader = checked(GPUShader::source(std::string{canvas_example::shaderSource}));
	const auto history =
		checked(GPUStateSlot::create({.width = 1, .height = 1, .resetKey = "demo"}));
	return checked(
		GPUProgram::create(
			{.shader = shader,
				.resources =
					{{"output", GPUTarget{}},
						{"field", GPUTarget{.width = fieldSize, .height = fieldSize}},
						{"level", uniform(1)},
						{"history", history},
						{"sampler", GPUSampler{}}},
				.passes = {
					GPUComputePass{
						.function = "generate",
						.workgroupCount = {groups, groups, 1},
						.bindings =
							{{.resource = "level", .stage = GPUBinding::Stage::compute},
								{.resource = "field",
									.binding = 1,
									.access = GPUBinding::Access::write,
									.stage = GPUBinding::Stage::compute},
								{.resource = "history",
									.binding = 4,
									.stage = GPUBinding::Stage::compute,
									.previous = true},
								{.resource = "history",
									.binding = nextBinding,
									.access = GPUBinding::Access::write,
									.stage = GPUBinding::Stage::compute}}
					},
					GPURenderPass{
						.vertex = "fullscreen",
						.fragment = "shade",
						.bindings =
							{{.resource = "field", .binding = 2},
								{.resource = "sampler", .binding = 3}}
					}
				}}
		)
	);
}

auto graphics(const GPUProgram &value) -> GPUDrawing {
	GPUDrawing drawing{value};
	constexpr double inset = 24;
	constexpr double triangleWidth = 90;
	constexpr double triangleHeight = 100;
	const std::array vertices{
		GPUVertex{.position = {.x = inset, .y = inset}, .color = ExtendedColor{peak, 0, 0}},
		GPUVertex{
			.position = {.x = inset + triangleWidth, .y = inset},
			.color = ExtendedColor{0, peak, 0}
		},
		GPUVertex{
			.position = {.x = inset, .y = inset + triangleHeight},
			.color = ExtendedColor{0, 0, peak}
		}
	};
	const std::array<std::uint32_t, 3> indices{0, 1, 2};
	drawing.drawMesh(checked(GPUMesh::create(vertices, indices)));
	const std::array points{
		Point{.x = inset, .y = canvasHeight - inset},
		Point{.x = canvasWidth / 2, .y = canvasHeight / 2},
		Point{.x = canvasWidth - inset, .y = inset}
	};
	constexpr double lineWidth = 4;
	drawing.strokePolyline(points, Color::text(), lineWidth);
	return drawing;
}

auto chart(int update) -> Drawing {
	Drawing drawing;
	constexpr int cycle = 4;
	constexpr double increment = 20;
	constexpr double baseline = canvasHeight - padding;
	for (int index = 0; index < cycle; ++index) {
		const auto height = barHeight + static_cast<double>((update + index) % cycle) * increment;
		drawing.fillRect(
			{.origin =
					{.x = padding + static_cast<double>(index) * (barWidth + spacing),
						.y = baseline - height},
				.size = {.width = barWidth, .height = height}},
			Color::accent()
		);
	}
	return drawing;
}
} // namespace

auto main(int argc, char **) -> int {
	try {
		auto initial = program();
		const auto validation = validateGPUProgram(initial);
		if (!validation) {
			throw std::runtime_error(validation.error().message);
		}
		if (argc > 1) {
			static_cast<void>(graphics(initial));
			return 0;
		}
		const Signal updates{0};
		const Signal gpu{graphics(initial)};
		const auto fixed = LayoutProperties{
			.frame =
				Frame{.width = Length::points(canvasWidth), .height = Length::points(canvasHeight)}
		};
		constexpr double windowWidth = 720;
		constexpr double windowHeight = 370;
		return Application::run(
			Window{
				.title = "Stana CPU / GPU Canvas",
				.size = {.width = windowWidth, .height = windowHeight},
				.content = VStack{
					.spacing = spacing,
					.layout = {.padding = Insets::all(padding)},
					.children = children(
						Text{
							.text =
								"Reactive CPU chart and WGSL compute → render, mesh and round line"
						},
						HStack{
							.spacing = spacing,
							.children = children(
								CPUCanvas{
									.drawing = updates.map(chart),
									.layout = fixed,
									.accessibility =
										{.label = "Four bars; each update rotates heights from 80 "
												  "to 140 points"}
								},
								GPUCanvas{
									.drawing = gpu,
									.layout = fixed,
									.accessibility =
										{.label = "Generated red-green gradient with a colored "
												  "triangle and rising line"},
									.onError = [](const GPUDiagnostic &error) -> void {
										std::cerr << error.message << '\n';
									}
								}
							)
						},
						Text{.text = updates.map([](int value) -> std::string {
							return "Updates: " + std::to_string(value) +
								". Gradient brightness alternates; temporal history advances once "
								"per new program version.";
						})},
						Button{
							.text = "Update charts",
							.onClick =
								[updates, gpu, current = std::move(initial)]() mutable -> void {
								const auto value = updates.get() + 1;
								const float strength =
									value % 2 == 0 ? 1.0F : static_cast<float>(peak);
								current = checked(
									current.updatingResources({{"level", uniform(strength)}})
								);
								gpu.set(graphics(current));
								updates.set(value);
							},
							.accessibility =
								{.label = "Update both charts and advance the GPU history"}
						}
					)
				}
			}
		);
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
