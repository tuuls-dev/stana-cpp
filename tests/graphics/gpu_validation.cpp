#include "../../src/graphics/color.hpp"
#include "../../src/platform/allocation.hpp"
#include <array>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>
import stana;
import stana.testing;
import stana.core;

namespace {
void require(bool value, const std::string &message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

void halvesAndColor() {
	using namespace stana::graphics_detail;
	constexpr std::uint32_t halfCount = 65536;
	constexpr unsigned byteMask = 255;
	for (std::uint32_t bits = 0; bits < halfCount; ++bits) {
		const std::array bytes{
			static_cast<std::byte>(bits & byteMask),
			static_cast<std::byte>(bits >> byteBits)
		};
		const double value = halfAt(bytes, 0);
		if (std::isfinite(value)) {
			const auto encoded = toHalf(value);
			require(encoded.has_value() && *encoded == bits, "binary16 exact round-trip");
		}
	}
	constexpr double tolerance = 1e-10;
	constexpr double negative = -2;
	for (const double value : {negative, 0.0, 1.0, 2.0}) {
		require(
			std::abs(decodeSRGB(encodeSRGB(value)) - value) < tolerance,
			"signed sRGB transfer round-trip"
		);
	}
	const auto mapped = displayMap({2, 1, 0, 1}, 1);
	require(
		mapped.front() < 1 && mapped.front() > mapped.at(1),
		"headroom-one mapping is finite and hue-preserving"
	);
	require(displayMap({0, 0, 0, 0}, 1) == std::array<double, 4>{}, "alpha-zero mapping canonical");
}

void budgets() {
	using namespace stana::native;
	const auto device = std::make_shared<AllocationLedger>();
	const auto window = std::make_shared<AllocationLedger>();
	constexpr std::size_t cap = 128;
	device->limit = cap;
	window->limit = cap;
	auto reservation = AllocationLease::reserve(device, window, cap);
	require(
		reservation.has_value() && device->bytes == cap &&
			!AllocationLease::reserve(device, window, 1),
		"atomic peak admission"
	);
	auto retired = (*reservation)->split(cap / 2);
	reservation->reset();
	require(window->bytes == cap / 2, "retired owner remains charged independently");
	retired.reset();
	require(window->bytes == 0 && device->bytes == 0, "final ownership releases charges");
}

void diagnosticsAndGeometry() {
	using namespace stana;
	const GPUDrawing drawing;
	const Signal visible{true};
	unsigned calls = 0;
	TestHost host;
	host.mount(
		If{.condition = visible,
			.thenContent = GPUCanvas{
				.drawing = drawing,
				.onError = [&](const GPUDiagnostic &diagnostic) -> void {
					require(diagnostic.message == "owned", "owning diagnostic");
					++calls;
				}
			}}
	);
	const auto id = host.nodes().back().id;
	GPUDiagnostic diagnostic{
		.category = GPUDiagnostic::Category::compiler,
		.message = "owned",
		.instance = id
	};
	host.injectGPUDiagnostic(diagnostic, drawing.version());
	host.injectGPUDiagnostic(diagnostic, drawing.version());
	require(calls == 1, "diagnostic deduplication");
	visible.set(false);
	host.injectGPUDiagnostic(diagnostic, drawing.version());
	require(calls == 1, "removed occurrence rejects late diagnostics");
	host.mount(GPUCanvas{.drawing = drawing, .onError = [&](const GPUDiagnostic &) -> void {
							 host.close();
							 ++calls;
						 }});
	diagnostic.instance = host.nodes().back().id;
	host.injectGPUDiagnostic(diagnostic, drawing.version());
	require(calls == 2, "handler may close safely");
	constexpr double extent = 20;
	auto document = detail::Document::create({.width = extent, .height = extent});
	document->enablePresentationTracking();
	document->commit();
	const auto revision = document->renderSnapshot()->revision;
	document->holdPreparedGeometry(revision);
	constexpr unsigned replacements = 20;
	for (unsigned index = 0; index < replacements; ++index) {
		document->invalidate(document->root(), detail::Dirty::snapshot);
		document->commit();
	}
	const auto attempt = document->beginPresentation(1, revision);
	require(attempt.has_value(), "ready offscreen geometry survives short history eviction");
	document->close();
}

void graphs() {
	using namespace stana;
	const auto shader = GPUShader::source("@compute @workgroup_size(1) fn step() {}");
	require(shader.has_value(), "shader handle");
	constexpr std::uint32_t seed = 0x531a;
	std::uint32_t graphState = seed;
	const auto nextGraphChoice = [&]() -> std::uint32_t {
		constexpr unsigned left = 13;
		constexpr unsigned right = 17;
		constexpr unsigned finalLeft = 5;
		graphState ^= graphState << left;
		graphState ^= graphState >> right;
		graphState ^= graphState << finalLeft;
		return graphState;
	};
	constexpr std::size_t count = 8;
	constexpr unsigned iterations = 100;
	for (unsigned iteration = 0; iteration < iterations; ++iteration) {
		GPUProgramDescriptor descriptor{.shader = *shader, .resources = {{"output", GPUTarget{}}}};
		for (std::size_t index = 0; index < count; ++index) {
			descriptor.resources.emplace("r" + std::to_string(index), GPUTarget{});
		}
		std::vector<GPUPass> passes;
		for (std::size_t index = 0; index < count; ++index) {
			GPUComputePass pass{
				.function = "step",
				.bindings = {
					{.resource = "r" + std::to_string(index),
						.access = GPUBinding::Access::write,
						.stage = GPUBinding::Stage::compute}
				}
			};
			if (index > 0) {
				const auto previous = static_cast<std::size_t>(nextGraphChoice()) % index;
				pass.bindings.push_back(
					{.resource = "r" + std::to_string(previous),
						.binding = 1,
						.stage = GPUBinding::Stage::compute}
				);
			}
			passes.emplace_back(std::move(pass));
		}
		descriptor.resources.erase("output");
		descriptor.output = "r" + std::to_string(count - 1);
		descriptor.passes.assign(passes.rbegin(), passes.rend());
		const auto program = GPUProgram::create(descriptor);
		require(program.has_value(), "fixed-seed acyclic graph");
		std::array<std::size_t, count> ranks{};
		for (std::size_t rank = 0; rank < program->executionOrder().size(); ++rank) {
			ranks.at(program->executionOrder().subspan(rank, 1).front()) = rank;
		}
		for (std::size_t index = 0; index < descriptor.passes.size(); ++index) {
			const auto &pass = std::get<GPUComputePass>(descriptor.passes.at(index));
			if (pass.bindings.size() > 1) {
				const auto producer =
					static_cast<std::size_t>(std::stoul(pass.bindings.back().resource.substr(1)));
				require(
					ranks.at(count - 1 - producer) < ranks.at(index),
					"every producer precedes its consumer"
				);
			}
		}
	}
	std::cout << "Graph seed=" << seed << " iterations=" << iterations << '\n';
}
} // namespace

auto main() -> int {
	try {
		halvesAndColor();
		budgets();
		diagnosticsAndGeometry();
		graphs();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
