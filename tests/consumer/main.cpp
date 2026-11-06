#include <array>
#include <cstdint>
#include <future>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

import stana;

auto main() -> int {
	using namespace std::string_view_literals;
	constexpr std::array tiles{stana::rendering::TileId{0}, stana::rendering::TileId{1}};
	stana::rendering::PresentationTracker tracker;
	const auto attempt = tracker.begin(1, 1, tiles);
	if (!attempt ||
		tracker.reportPresented(
			*attempt,
			stana::rendering::TilePresentation{.tile = tiles.front(), .timestamp = 1}
		)) {
		return 1;
	}
	stana::rendering::Drawing drawing;
	drawing.fillRect({.size = {.width = 1, .height = 1}}, stana::Color::accent());
	const stana::reactive::Signal model{drawing};
	const stana::ui::CPUCanvas canvas{.drawing = model, .appearance = {.clip = false}};
	static_cast<void>(canvas);
	const auto shader = stana::rendering::GPUShader::source(R"(
@vertex fn vs(@builtin(vertex_index) id: u32) -> @builtin(position) vec4<f32> {
 let p=array<vec2<f32>,3>(vec2<f32>(-1.0,-1.0),vec2<f32>(3.0,-1.0),vec2<f32>(-1.0,3.0));
 return vec4<f32>(p[id],0.0,1.0);
}
@fragment fn fs() -> @location(0) vec4<f32> { return vec4<f32>(0.25,0.5,1.0,1.0); }
)");
	if (!shader) {
		return 1;
	}
	const auto program = stana::rendering::GPUProgram::create(
		{.shader = *shader,
			.passes = {stana::rendering::GPURenderPass{.vertex = "vs", .fragment = "fs"}}}
	);
	if (!program) {
		return 1;
	}
	stana::rendering::GPUDrawing graphics{*program};
	const std::vector<stana::rendering::BoxShadow> shadows{
		{.offset = {.x = 0, .y = 6}, .blurRadius = 16, .color = stana::Color::rgb(0, 0, 0, 0.2)}
	};
	const auto interpolated = stana::rendering::interpolateBoxShadows({}, shadows, 0.5);
	graphics.drawBoxShadows(
		{.size = {.width = 120, .height = 60}},
		interpolated,
		stana::CornerRadii::all(12),
		stana::BorderWidths::all(2)
	);
	const stana::reactive::Signal gpu{graphics};
	stana::testing::TestHost host;
	host.mount(stana::ui::GPUCanvas{.drawing = gpu});
	if (host.nodes().back().kind != "gpuCanvas" || host.mixedPaint().size() != 3) {
		return 1;
	}
	const stana::reactive::Signal queued{0};
	std::async(std::launch::async, [queued] -> void {
		queued.setThreadSafe(1);
	}).get();
	host.flush();
	if (queued.get() != 1) {
		return 1;
	}
	const stana::application::Window positional{
		std::string{"canvas"},
		stana::Size{.width = 1, .height = 1},
		stana::ui::CPUCanvas{}
	};
	if (positional.renderingColorMode != stana::rendering::RenderingColorMode::automatic) {
		return 1;
	}
	stana::document::Metrics metrics;
	stana::rendering::WindowDiagnostics diagnostics;
	if (metrics.measurements != 0 || diagnostics.renderingWork.cpuUploadBytes != 0 ||
		diagnostics.gpuLeafDraws != 0) {
		return 1;
	}
	return stana::version() == "0.1.0"sv ? 0 : 1;
}
