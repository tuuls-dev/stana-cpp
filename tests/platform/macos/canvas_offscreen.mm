#include "../../../src/platform/macos/compositor.hpp"
#include "../../../src/rendering/color.hpp"
#include "canvas_fixture.hpp"
#import <Foundation/Foundation.h>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value, const std::string &message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] auto read(
	const std::shared_ptr<stana::platform::detail::gpu::DeviceResources> &device,
	const std::shared_ptr<const stana::platform::detail::gpu::CompositePacket> &packet,
	const stana::platform::detail::TileGrid &grid
) -> stana::platform::detail::gpu::Capture {

	const auto signal = std::make_shared<std::promise<
		stana::platform::detail::gpu::Result<stana::platform::detail::gpu::Capture>>>();
	auto completed = signal->get_future();
	stana::platform::detail::gpu::capture(
		device,
		packet,
		grid,
		[signal](
			stana::platform::detail::gpu::Result<stana::platform::detail::gpu::Capture> result
		) -> void {
			signal->set_value(std::move(result));
		}
	);
	constexpr std::chrono::seconds deadline{10};
	require(
		completed.wait_for(deadline) == std::future_status::ready,
		"asynchronous readback timed out"
	);
	auto result = completed.get();
	require(result.has_value(), result ? "" : result.error().message);
	return std::move(*result);
}

void pump(const std::function<bool()> &condition) {
	constexpr std::chrono::seconds deadline{10};
	constexpr double slice = 0.01;
	const auto start = std::chrono::steady_clock::now();
	while (!condition() && std::chrono::steady_clock::now() - start < deadline) {
		static_cast<void>(
			CFRunLoopRunInMode(kCFRunLoopDefaultMode, slice, static_cast<Boolean>(0))
		);
	}
	require(condition(), "offscreen UI completion did not settle");
}

void mixed() {

	const auto device =
		stana::platform::detail::gpu::DeviceResources::get(MTLCreateSystemDefaultDevice());
	require(device.has_value(), device ? "" : device.error().message);
	constexpr double doubleScale = 2;
	constexpr double fractionalScale = 1.25;
	for (const double scale : {1.0, doubleScale, fractionalScale}) {
		const auto source = stana::testing::fixtures::platform::macos::mixedScene();
		const auto scene = stana::platform::detail::prepareScene(source, scale);
		const auto grid = stana::platform::detail::makeTileGrid(source->size, scale);
		const auto compositor = stana::platform::detail::gpu::Compositor::create(
			*device,
			std::make_shared<stana::platform::detail::RasterAccounting>()
		);
		require(compositor.has_value(), compositor ? "" : compositor.error().message);
		const auto encoded =
			(*compositor)
				->prepare(scene, grid, 1, stana::platform::detail::gpu::ColorMode::automatic);
		require(encoded.has_value(), encoded ? "" : encoded.error().message);
		pump([&] -> bool {
			return !(*compositor)->isInFlight() && (*compositor)->readyPacket() != nullptr;
		});
		const auto pixels = read(*device, (*compositor)->readyPacket(), grid);
		const auto channel = [&](std::size_t x, std::size_t y, std::size_t component) -> unsigned {
			return std::to_integer<unsigned>(
				pixels.bytes.at((y * pixels.width + x) * 4 + component)
			);
		};
		constexpr double topSample = 12;
		const auto top = static_cast<std::size_t>(topSample * scale);
		constexpr unsigned opaque = 255;
		require(
			channel(top, top, 0) == opaque && channel(top, top, 1) == 0 &&
				channel(top, top, 2) == 0,
			"CPU foreground is not above translucent GPU content"
		);
		constexpr double underSample = 24;
		const auto under = static_cast<std::size_t>(underSample * scale);
		constexpr unsigned low = 126;
		constexpr unsigned high = 129;
		require(
			channel(under, under, 0) == 0 && channel(under, under, 1) >= low &&
				channel(under, under, 1) <= high && channel(under, under, 2) >= low &&
				channel(under, under, 2) <= high,
			"CPU/GPU painter order or premultiplied source-over is incorrect"
		);
		const auto before = (*compositor)->statistics();
		const auto retry =
			(*compositor)
				->prepare(scene, grid, 1, stana::platform::detail::gpu::ColorMode::automatic);
		require(
			retry.has_value() && *retry &&
				(*compositor)->statistics().cpuRasterPixels == before.cpuRasterPixels &&
				(*compositor)->statistics().stateSteps == before.stateSteps,
			"presentation-only retry performed extra content work"
		);
		(*compositor)->close();
	}
	const auto source = stana::testing::fixtures::platform::macos::mixedScene(true);
	const auto grid = stana::platform::detail::makeTileGrid(source->size, 1);
	const auto compositor = stana::platform::detail::gpu::Compositor::create(
		*device,
		std::make_shared<stana::platform::detail::RasterAccounting>()
	);
	require(compositor.has_value(), compositor ? "" : compositor.error().message);
	const auto encoded = (*compositor)
							 ->prepare(
								 stana::platform::detail::prepareScene(source, 1),
								 grid,
								 1,
								 stana::platform::detail::gpu::ColorMode::automatic
							 );
	require(encoded.has_value(), encoded ? "" : encoded.error().message);
	pump([&] -> bool {
		return !(*compositor)->isInFlight() && (*compositor)->readyPacket() != nullptr;
	});
	const auto pixels = read(*device, (*compositor)->readyPacket(), grid);
	constexpr std::size_t halfPixelBytes = 8;
	constexpr std::size_t middle = 16;
	constexpr double extendedRed = 2.0;
	const auto offset = (middle * pixels.width + middle) * halfPixelBytes;
	require(
		pixels.format == stana::platform::detail::gpu::Format::rgba16 &&
			std::is_eq(stana::rendering::detail::halfAt(pixels.bytes, offset) <=> extendedRed),
		"extended color above one did not survive complete offscreen capture"
	);
	(*compositor)->close();
	std::cout << "Native mixed order and premultiplied overlap: 1x, 2x, 1.25x; linear HDR "
				 "readback; retry reuse\n";
}

void builtinsAndReconstruction() {

	const auto device =
		stana::platform::detail::gpu::DeviceResources::get(MTLCreateSystemDefaultDevice());
	require(device.has_value(), device ? "" : device.error().message);
	const auto sources = stana::testing::fixtures::platform::macos::builtinScenes();
	require(sources.size() > 1, "GPU drawing mutation fixture");
	const auto first = stana::platform::detail::prepareScene(sources.front(), 1);
	const auto second = stana::platform::detail::prepareScene(sources.back(), 1, first);
	const auto damageGrid = stana::platform::detail::makeTileGrid(sources.front()->size, 1);
	stana::platform::detail::RenderingWork work;
	require(
		stana::platform::detail::sceneDamage(first, second, damageGrid, work).any(),
		"GPU-only drawing change must dirty content tiles"
	);
	constexpr double doubled = 2;
	constexpr double fractional = 1.25;
	for (const double scale : {1.0, doubled, fractional}) {
		const auto incremental = stana::platform::detail::gpu::Compositor::create(
			*device,
			std::make_shared<stana::platform::detail::RasterAccounting>()
		);
		require(incremental.has_value(), incremental ? "" : incremental.error().message);
		for (const auto &source : sources) {
			const auto scene = stana::platform::detail::prepareScene(source, scale);
			const auto grid = stana::platform::detail::makeTileGrid(source->size, scale);
			const auto render =
				[&](
					const std::shared_ptr<stana::platform::detail::gpu::Compositor> &compositor
				) -> stana::platform::detail::gpu::Capture {
				const auto result = compositor->prepare(
					scene,
					grid,
					1,
					stana::platform::detail::gpu::ColorMode::automatic
				);
				require(result.has_value(), result ? "" : result.error().message);
				pump([&] -> bool {
					return !compositor->isInFlight() && compositor->readyPacket() &&
						compositor->readyPacket()->scene == scene;
				});
				return read(*device, compositor->readyPacket(), grid);
			};
			const auto actual = render(*incremental);
			const auto full = stana::platform::detail::gpu::Compositor::create(
				*device,
				std::make_shared<stana::platform::detail::RasterAccounting>()
			);
			require(full.has_value(), full ? "" : full.error().message);
			const auto reference = render(*full);
			if (actual.bytes != reference.bytes) {
				std::size_t count = 0;
				for (std::size_t offset = 0; offset < actual.bytes.size(); ++offset) {
					if (actual.bytes.at(offset) != reference.bytes.at(offset)) {
						if (count == 0) {
							std::cerr
								<< "first byte=" << offset
								<< " actual=" << std::to_integer<unsigned>(actual.bytes.at(offset))
								<< " expected="
								<< std::to_integer<unsigned>(reference.bytes.at(offset)) << '\n';
						}
						++count;
					}
				}
				std::cerr << "reconstruction scale=" << scale << " revision=" << source->revision
						  << " differing bytes=" << count
						  << " samples=" << (*incremental)->readyPacket()->samples << "/"
						  << (*full)->readyPacket()->samples
						  << " dirty=" << (*incremental)->readyPacket()->dirty.count() << '\n';
			}
			require(
				actual.bytes == reference.bytes,
				"incremental built-ins differ from fresh full reconstruction"
			);
			require(
				actual.format == stana::platform::detail::gpu::Format::rgba16,
				"mesh/extended scene requires linear composition"
			);
			const auto sample = [&](std::array<double, 2> point, std::size_t component) -> double {
				constexpr std::size_t halfStride = 8;
				const auto column = static_cast<std::size_t>(point.front() * scale);
				const auto row = static_cast<std::size_t>(point.back() * scale);
				return stana::rendering::detail::halfAt(
					actual.bytes,
					(row * actual.width + column) * halfStride + component * 2
				);
			};
			constexpr double center = 24;
			constexpr double ellipseCenter = 104;
			constexpr double lineCenter = 512;
			constexpr double lineY = 30;
			constexpr double dot = 700;
			require(std::is_eq(sample({center, center}, 0) <=> 1.0), "rounded rectangle interior");
			require(
				std::is_eq(sample({ellipseCenter, center}, 1) <=> 1.0),
				"non-square ellipse interior"
			);
			require(
				std::is_eq(sample({lineCenter, lineY}, 0) <=> 1.0) &&
					std::is_eq(sample({lineCenter, lineY}, 2) <=> 1.0),
				"round polyline crosses tile seam"
			);
			require(std::is_eq(sample({dot, lineY}, 1) <=> 1.0), "one-point round disk");
			constexpr double meshSampleX = 220;
			constexpr double meshSampleY = 28;
			constexpr double meshX = 200.25;
			constexpr double meshY = 8.25;
			constexpr double meshExtent = 80;
			const double u = ((std::floor(meshSampleX * scale) + 0.5) / scale - meshX) / meshExtent;
			const double v = ((std::floor(meshSampleY * scale) + 0.5) / scale - meshY) / meshExtent;
			constexpr double tolerance = 0.002;
			require(
				std::abs(sample({meshSampleX, meshSampleY}, 0) - (1 - u - v)) <= tolerance &&
					std::abs(sample({meshSampleX, meshSampleY}, 1) - u) <= tolerance &&
					std::abs(sample({meshSampleX, meshSampleY}, 2) - v) <= tolerance,
				"independent linear gradient interpolation"
			);
			(*full)->close();
		}
		(*incremental)->close();
	}
	std::cout
		<< "Built-ins: rounded rect, non-square ellipse, gradient mesh, duplicate/one-point round "
		   "line; fractional origins, partial tiles, exact full reconstruction at 1x/2x/1.25x\n";
}

void temporal() {

	const auto device =
		stana::platform::detail::gpu::DeviceResources::get(MTLCreateSystemDefaultDevice());
	require(device.has_value(), device ? "" : device.error().message);
	const auto compositor = stana::platform::detail::gpu::Compositor::create(
		*device,
		std::make_shared<stana::platform::detail::RasterAccounting>()
	);
	require(compositor.has_value(), compositor ? "" : compositor.error().message);
	const auto sources = stana::testing::fixtures::platform::macos::counterScenes();
	require(!sources.empty(), "temporal compositor fixture");
	const auto grid = stana::platform::detail::makeTileGrid(sources.front()->size, 1);
	std::size_t step = 0;
	for (const auto &source : sources) {
		++step;
		const auto scene = stana::platform::detail::prepareScene(source, 1);
		pump([&] -> bool {
			const auto result =
				(*compositor)
					->prepare(scene, grid, 1, stana::platform::detail::gpu::ColorMode::automatic);
			require(result.has_value(), result ? "" : result.error().message);
			return *result && (*compositor)->readyPacket() &&
				(*compositor)->readyPacket()->scene == scene &&
				(*compositor)->statistics().stateSteps == step;
		});
		const auto pixels = read(*device, (*compositor)->readyPacket(), grid);
		constexpr std::size_t halfPixelBytes = 8;
		constexpr std::size_t sample = 16;
		require(
			pixels.format == stana::platform::detail::gpu::Format::rgba16 &&
				std::is_eq(
					stana::rendering::detail::halfAt(
						pixels.bytes,
						(sample * pixels.width + sample) * halfPixelBytes
					) <=> static_cast<double>(step)
				),
			"compositor temporal counter differs from independently expected integer"
		);
	}
	require(
		(*compositor)->statistics().gpuLeafDraws > 0,
		"temporal compositor must export GPU leaf draws"
	);
	(*compositor)->close();
	std::cout << "Native offscreen compositor: " << step
			  << " admitted successful temporal steps; no presentation evidence claimed\n";
}
} // namespace

auto main() -> int {
	@autoreleasepool {
		try {
			temporal();
			mixed();
			builtinsAndReconstruction();
			return 0;
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	}
}
