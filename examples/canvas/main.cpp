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

using namespace stana::literals;

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
	const std::array vertices{
		stana::rendering::GPUVertex{
			.position = {.x = 24_pt, .y = 24_pt},
			.color = stana::rendering::ExtendedColor{peak, 0, 0}
		},
		stana::rendering::GPUVertex{
			.position = {.x = 114_pt, .y = 24_pt},
			.color = stana::rendering::ExtendedColor{0, peak, 0}
		},
		stana::rendering::GPUVertex{
			.position = {.x = 24_pt, .y = 124_pt},
			.color = stana::rendering::ExtendedColor{0, 0, peak}
		}
	};
	const std::array<std::uint32_t, 3> indices{0, 1, 2};
	drawing.drawMesh(checked(stana::rendering::GPUMesh::create(vertices, indices)));
	const std::array points{
		stana::Point{.x = 24_pt, .y = 156_pt},
		stana::Point{.x = 160_pt, .y = 90_pt},
		stana::Point{.x = 296_pt, .y = 24_pt}
	};
	drawing.strokePolyline(points, stana::Color::text(), 4_pt);
	return drawing;
}

auto chart(int update) -> stana::rendering::Drawing {
	stana::rendering::Drawing drawing;
	constexpr int cycle = 4;
	for (int index = 0; index < cycle; ++index) {
		const auto height = 80_pt + static_cast<double>((update + index) % cycle) * 20_pt;
		drawing.fillRect(
			{.origin =
					{.x = 20_pt + static_cast<double>(index) * (40_pt + 12_pt),
						.y = 160_pt - height},
				.size = {.width = 40_pt, .height = height}},
			stana::Color::accent()
		);
	}
	return drawing;
}
} // namespace

auto main(int argc, char **) -> int {
	using namespace stana::literals;
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
				.width = stana::Length::points(320_pt),
				.height = stana::Length::points(180_pt)
			}
		};
		return stana::application::Application::run(
			stana::application::Window{
				.content =
					stana::ui::VStack{
						.spacing = 12_pt,
						.layout = {.padding = stana::Insets::all(20_pt)},
						.children =
							stana::ui::children(
								stana::ui::Text{
									.text = "Reactive CPU chart and WGSL compute → render, mesh "
											"and round line"
								},
								stana::ui::HStack{
									.spacing = 12_pt,
									.children = stana::ui::children(
										stana::ui::CPUCanvas{
											.drawing = updates.map(chart),
											.layout = fixed,
											.accessibility =
												{.label = "Four bars; each update rotates "
														  "heights from 80 "
														  "to 140 points"}
										},
										stana::ui::GPUCanvas{
											.drawing = gpu,
											.layout = fixed,
											.accessibility =
												{.label = "Generated red-green gradient with a "
														  "colored "
														  "triangle and rising line"},
											.onError = [](const stana::rendering::GPUDiagnostic
															   &error) -> void {
												std::cerr << error.message << '\n';
											}
										}
									)
								},
								stana::ui::Text{
									.text = updates.map(
										[](int value) -> std::string {
											return "Updates: " + std::to_string(value) +
												". Gradient brightness alternates; temporal "
												"history advances once "
												"per new program version.";
										}
									)
								},
								stana::ui::Button{
									.text = "Update charts",
									.accessibility =
										{.label = "Update both charts and advance the GPU history"},
									.events =
										{
											stana::ui::on(
												stana::ui::Event::click,
												[updates,
													gpu,
													current =
														std::move(initial)]() mutable -> void {
													const auto value = updates.get() + 1;
													const float strength = value % 2 == 0
														? 1.0F
														: static_cast<float>(peak);
													current = checked(current.updatingResources(
														{{"level", uniform(strength)}}
													));
													gpu.set(graphics(current));
													updates.set(value);
												}
											)
										}
								}
							)
					},
				.options = {
					.contentSize = {.width = 720_pt, .height = 370_pt},
					.title = "Stana CPU / GPU Canvas"
				}
			}
		);
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
