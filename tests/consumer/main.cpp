#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

import stana;
import stana.testing;

auto main() -> int {
	using namespace std::string_view_literals;
	constexpr std::array tiles{stana::TileId{0}, stana::TileId{1}};
	stana::PresentationTracker tracker;
	const auto attempt = tracker.begin(1, 1, tiles);
	if (!attempt ||
		tracker.reportPresented(
			*attempt,
			stana::TilePresentation{.tile = tiles.front(), .timestamp = 1}
		)) {
		return 1;
	}
	stana::Drawing drawing;
	drawing.fillRect({.size = {.width = 1, .height = 1}}, stana::Color::accent());
	const stana::Signal model{drawing};
	const stana::CPUCanvas canvas{.drawing = model, .appearance = {.clip = false}};
	static_cast<void>(canvas);
	const auto shader = stana::GPUShader::source(R"(
@vertex fn vs(@builtin(vertex_index) id: u32) -> @builtin(position) vec4<f32> {
 let p=array<vec2<f32>,3>(vec2<f32>(-1.0,-1.0),vec2<f32>(3.0,-1.0),vec2<f32>(-1.0,3.0));
 return vec4<f32>(p[id],0.0,1.0);
}
@fragment fn fs() -> @location(0) vec4<f32> { return vec4<f32>(0.25,0.5,1.0,1.0); }
)");
	if (!shader) {
		return 1;
	}
	const auto program = stana::GPUProgram::create(
		{.shader = *shader, .passes = {stana::GPURenderPass{.vertex = "vs", .fragment = "fs"}}}
	);
	if (!program || !stana::validateGPUProgram(*program)) {
		return 1;
	}
	stana::GPUDrawing graphics{*program};
	const stana::Signal gpu{graphics};
	stana::TestHost host;
	host.mount(stana::GPUCanvas{.drawing = gpu});
	if (host.nodes().back().kind != "gpuCanvas" || host.mixedPaint().size() != 3) {
		return 1;
	}
	const stana::Window positional{
		std::string{"canvas"},
		stana::Size{.width = 1, .height = 1},
		stana::CPUCanvas{}
	};
	if (positional.renderingColorMode != stana::RenderingColorMode::automatic) {
		return 1;
	}
	stana::Metrics metrics;
	stana::WindowDiagnostics diagnostics;
	if (metrics.measurements != 0 || diagnostics.renderingWork.cpuUploadBytes != 0) {
		return 1;
	}
	return stana::version() == "0.1.0"sv ? 0 : 1;
}
