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

namespace {

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
template <typename T> auto checked(std::expected<T, stana::rendering::GPUError> result) -> T {
	if (!result) {
		throw std::runtime_error(result.error().message);
	}
	return std::move(*result);
}

auto uniform(float value) -> stana::rendering::GPUBuffer {
	constexpr std::size_t alignment = 16;
	constexpr unsigned byteBits = 8;
	constexpr std::uint32_t mask = 255;
	const auto bits = std::bit_cast<std::uint32_t>(value);
	std::array<std::byte, alignment> bytes{};
	for (std::size_t index = 0; index < sizeof(bits); ++index) {
		bytes.at(index) = static_cast<std::byte>((bits >> (index * byteBits)) & mask);
	}
	return checked(stana::rendering::GPUBuffer::create(bytes));
}

auto program() -> stana::rendering::GPUProgram {
	const auto shader =
		checked(stana::rendering::GPUShader::source(std::string{canvas_example::shaderSource}));
	const auto history = checked(
		stana::rendering::GPUStateSlot::create({.width = 1, .height = 1, .resetKey = "demo"})
	);
	return checked(
		stana::rendering::GPUProgram::create(
			{.shader = shader,
				.resources =
					{{"output", stana::rendering::GPUTarget{}},
						{"field",
							stana::rendering::GPUTarget{.width = fieldSize, .height = fieldSize}},
						{"level", uniform(1)},
						{"history", history},
						{"sampler", stana::rendering::GPUSampler{}}},
				.passes = {
					stana::rendering::GPUComputePass{
						.function = "generate",
						.workgroupCount = {groups, groups, 1},
						.bindings =
							{{.resource = "level",
								 .stage = stana::rendering::GPUBinding::Stage::compute},
								{.resource = "field",
									.binding = 1,
									.access = stana::rendering::GPUBinding::Access::write,
									.stage = stana::rendering::GPUBinding::Stage::compute},
								{.resource = "history",
									.binding = 4,
									.stage = stana::rendering::GPUBinding::Stage::compute,
									.previous = true},
								{.resource = "history",
									.binding = nextBinding,
									.access = stana::rendering::GPUBinding::Access::write,
									.stage = stana::rendering::GPUBinding::Stage::compute}}
					},
					stana::rendering::GPURenderPass{
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

auto graphics(const stana::rendering::GPUProgram &value) -> stana::rendering::GPUDrawing {
	stana::rendering::GPUDrawing drawing{value};
	constexpr double inset = 24;
	constexpr double triangleWidth = 90;
	constexpr double triangleHeight = 100;
	const std::array vertices{
		stana::rendering::GPUVertex{
			.position = {.x = stana::Points{inset}, .y = stana::Points{inset}},
			.color = stana::rendering::ExtendedColor{peak, 0, 0}
		},
		stana::rendering::GPUVertex{
			.position = {.x = stana::Points{inset + triangleWidth}, .y = stana::Points{inset}},
			.color = stana::rendering::ExtendedColor{0, peak, 0}
		},
		stana::rendering::GPUVertex{
			.position = {.x = stana::Points{inset}, .y = stana::Points{inset + triangleHeight}},
			.color = stana::rendering::ExtendedColor{0, 0, peak}
		}
	};
	const std::array<std::uint32_t, 3> indices{0, 1, 2};
	drawing.drawMesh(checked(stana::rendering::GPUMesh::create(vertices, indices)));
	const std::array points{
		stana::Point{.x = stana::Points{inset}, .y = stana::Points{canvasHeight - inset}},
		stana::Point{.x = stana::Points{canvasWidth / 2}, .y = stana::Points{canvasHeight / 2}},
		stana::Point{.x = stana::Points{canvasWidth - inset}, .y = stana::Points{inset}}
	};
	constexpr double lineWidth = 4;
	drawing.strokePolyline(points, stana::Color::text(), stana::Points{lineWidth});
	return drawing;
}

auto chart(int update) -> stana::rendering::Drawing {
	stana::rendering::Drawing drawing;
	constexpr int cycle = 4;
	constexpr double increment = 20;
	constexpr double baseline = canvasHeight - padding;
	for (int index = 0; index < cycle; ++index) {
		const auto height = barHeight + static_cast<double>((update + index) % cycle) * increment;
		drawing.fillRect(
			{.origin =
					{.x =
							stana::Points{
								padding + static_cast<double>(index) * (barWidth + spacing)
							},
						.y = stana::Points{baseline - height}},
				.size = {.width = stana::Points{barWidth}, .height = stana::Points{height}}},
			stana::Color::accent()
		);
	}
	return drawing;
}
} // namespace

auto main(int argc, char **) -> int {
	try {
		auto initial = program();
		// Headless mode checks descriptor/drawing construction, not shader compilation.
		if (argc > 1) {
			static_cast<void>(graphics(initial));
			return 0;
		}
		const stana::reactive::Signal updates{0};
		const stana::reactive::Signal gpu{graphics(initial)};
		const auto fixed = stana::layout::LayoutProperties{
			.frame = stana::Frame{
				.width = stana::Length::points(stana::Points{canvasWidth}),
				.height = stana::Length::points(stana::Points{canvasHeight})
			}
		};
		constexpr double windowWidth = 720;
		constexpr double windowHeight = 370;
		return stana::application::Application::run(
			stana::application::Window{
				.content = stana::ui::VStack{
					.spacing = stana::Points{spacing},
					.layout = {.padding = stana::Insets::all(stana::Points{padding})},
					.children = stana::ui::children(
						stana::ui::Text{
							.text =
								"Reactive CPU chart and WGSL compute → render, mesh and round line"
						},
						stana::ui::HStack{
							.spacing = stana::Points{spacing},
							.children = stana::ui::children(
								stana::ui::CPUCanvas{
									.drawing = updates.map(chart),
									.layout = fixed,
									.accessibility =
										{.label = "Four bars; each update rotates heights from 80 "
												  "to 140 points"}
								},
								stana::ui::GPUCanvas{
									.drawing = gpu,
									.layout = fixed,
									.accessibility =
										{.label = "Generated red-green gradient with a colored "
												  "triangle and rising line"},
									.onError =
										[](const stana::rendering::GPUDiagnostic &error) -> void {
										std::cerr << error.message << '\n';
									}
								}
							)
						},
						stana::ui::Text{.text = updates.map([](int value) -> std::string {
							return "Updates: " + std::to_string(value) +
								". Gradient brightness alternates; temporal history advances once "
								"per new program version.";
						})},
						stana::ui::Button{
							.text = "Update charts",
							.accessibility =
								{.label = "Update both charts and advance the GPU history"},
							.events = {stana::ui::on(
								stana::ui::Event::click,
								[updates, gpu, current = std::move(initial)]() mutable -> void {
									const auto value = updates.get() + 1;
									const float strength =
										value % 2 == 0 ? 1.0F : static_cast<float>(peak);
									current = checked(
										current.updatingResources({{"level", uniform(strength)}})
									);
									gpu.set(graphics(current));
									updates.set(value);
								}
							)}
						}
					)
				},
				.options =
					{.geometry =
							{.contentSize =
									{.width = stana::Points{windowWidth},
										.height = stana::Points{windowHeight}}},
						.appearance = {.title = "Stana CPU / GPU Canvas"}}
			}
		);
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
