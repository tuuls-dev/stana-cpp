#include "../../../src/graphics/color.hpp"
#include "../../../src/platform/macos/compositor.hpp"
#include "../../../src/platform/macos/gpu_programs.hpp"
#include "canvas_fixture.hpp"
#import <Foundation/Foundation.h>
#include <array>
#include <chrono>
#include <cmath>
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
	const std::shared_ptr<stana::native::gpu::DeviceResources> &device,
	const std::shared_ptr<const stana::native::gpu::CompositePacket> &packet,
	const stana::native::TileGrid &grid
) -> stana::native::gpu::Capture {
	using namespace stana::native;
	const auto signal = std::make_shared<std::promise<gpu::Result<gpu::Capture>>>();
	auto completed = signal->get_future();
	gpu::capture(device, packet, grid, [signal](gpu::Result<gpu::Capture> result) -> void {
		signal->set_value(std::move(result));
	});
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
	using namespace stana::native;
	const auto device = gpu::DeviceResources::get(MTLCreateSystemDefaultDevice());
	require(device.has_value(), device ? "" : device.error().message);
	constexpr double doubleScale = 2;
	constexpr double fractionalScale = 1.25;
	for (const double scale : {1.0, doubleScale, fractionalScale}) {
		const auto source = fixtures::mixedScene();
		const auto scene = prepareScene(source, scale);
		const auto grid = makeTileGrid(source->size, scale);
		const auto compositor =
			gpu::Compositor::create(*device, std::make_shared<RasterAccounting>());
		require(compositor.has_value(), compositor ? "" : compositor.error().message);
		const auto encoded = (*compositor)->prepare(scene, grid, 1, gpu::ColorMode::automatic);
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
		const auto retry = (*compositor)->prepare(scene, grid, 1, gpu::ColorMode::automatic);
		require(
			retry.has_value() && *retry &&
				(*compositor)->statistics().cpuRasterPixels == before.cpuRasterPixels &&
				(*compositor)->statistics().stateSteps == before.stateSteps,
			"presentation-only retry performed extra content work"
		);
		(*compositor)->close();
	}
	const auto source = fixtures::mixedScene(true);
	const auto grid = makeTileGrid(source->size, 1);
	const auto compositor = gpu::Compositor::create(*device, std::make_shared<RasterAccounting>());
	require(compositor.has_value(), compositor ? "" : compositor.error().message);
	const auto encoded =
		(*compositor)->prepare(prepareScene(source, 1), grid, 1, gpu::ColorMode::automatic);
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
		pixels.format == gpu::Format::rgba16 &&
			std::is_eq(stana::graphics_detail::halfAt(pixels.bytes, offset) <=> extendedRed),
		"extended color above one did not survive complete offscreen capture"
	);
	(*compositor)->close();
	std::cout << "Native mixed order and premultiplied overlap: 1x, 2x, 1.25x; linear HDR "
				 "readback; retry reuse\n";
}

void builtinsAndReconstruction() {
	using namespace stana::native;
	const auto device = gpu::DeviceResources::get(MTLCreateSystemDefaultDevice());
	require(device.has_value(), device ? "" : device.error().message);
	const auto sources = fixtures::builtinScenes();
	constexpr double doubled = 2;
	constexpr double fractional = 1.25;
	for (const double scale : {1.0, doubled, fractional}) {
		const auto incremental =
			gpu::Compositor::create(*device, std::make_shared<RasterAccounting>());
		require(incremental.has_value(), incremental ? "" : incremental.error().message);
		for (const auto &source : sources) {
			const auto scene = prepareScene(source, scale);
			const auto grid = makeTileGrid(source->size, scale);
			const auto render =
				[&](const std::shared_ptr<gpu::Compositor> &compositor) -> gpu::Capture {
				const auto result = compositor->prepare(scene, grid, 1, gpu::ColorMode::automatic);
				require(result.has_value(), result ? "" : result.error().message);
				pump([&] -> bool {
					return !compositor->isInFlight() && compositor->readyPacket() &&
						compositor->readyPacket()->scene == scene;
				});
				return read(*device, compositor->readyPacket(), grid);
			};
			const auto actual = render(*incremental);
			const auto full =
				gpu::Compositor::create(*device, std::make_shared<RasterAccounting>());
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
				actual.format == gpu::Format::rgba16,
				"mesh/extended scene requires linear composition"
			);
			const auto sample = [&](std::array<double, 2> point, std::size_t component) -> double {
				constexpr std::size_t halfStride = 8;
				const auto column = static_cast<std::size_t>(point.front() * scale);
				const auto row = static_cast<std::size_t>(point.back() * scale);
				return stana::graphics_detail::halfAt(
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
	using namespace stana::native;
	id<MTLDevice> const device = MTLCreateSystemDefaultDevice();
	require(device != nil, "Native offscreen gate requires a Metal device (no display claim)");
	const auto service = gpu::DeviceResources::get(device);
	require(service.has_value(), service ? "" : service.error().message);
	gpu::Resources resources{.device = *service};
	const auto programs = fixtures::counterPrograms();
	const auto pipeline = gpu::compileProgram(*service, *programs.front());
	require(pipeline.has_value(), pipeline ? "" : pipeline.error().message);
	std::shared_ptr<const gpu::ProgramBundle> committed;
	std::size_t step = 0;
	for (const auto &program : programs) {
		id<MTLCommandBuffer> const buffer = [(*service)->queue commandBuffer];
		require(buffer != nil, "offscreen command buffer");
		auto candidate = gpu::encodeProgram(resources, buffer, program, *pipeline, 1, 1, committed);
		require(candidate.has_value(), candidate ? "" : candidate.error().message);
		constexpr std::size_t readbackRow = 256;
		auto readback = resources.buffer(readbackRow);
		require(readback.has_value(), readback ? "" : readback.error().message);
		id<MTLBlitCommandEncoder> const copy = [buffer blitCommandEncoder];
		require(copy != nil, "offscreen copy encoder");
		[copy copyFromTexture:candidate->next->output->texture
						 sourceSlice:0
						 sourceLevel:0
						sourceOrigin:MTLOriginMake(0, 0, 0)
						  sourceSize:MTLSizeMake(1, 1, 1)
							toBuffer:(*readback)->buffer
				   destinationOffset:0
			  destinationBytesPerRow:readbackRow
			destinationBytesPerImage:readbackRow];
		if (!device.hasUnifiedMemory) {
			[copy synchronizeResource:(*readback)->buffer];
		}
		[copy endEncoding];
		const auto signal = std::make_shared<std::promise<bool>>();
		auto completed = signal->get_future();
		const auto owners = std::make_shared<gpu::ProgramCandidate>(std::move(*candidate));
		const auto staging = std::move(*readback);
		[buffer addCompletedHandler:^(id<MTLCommandBuffer> commands) {
		  static_cast<void>(owners);
		  static_cast<void>(staging);
		  signal->set_value(commands.status == MTLCommandBufferStatusCompleted);
		}];
		[buffer commit];
		constexpr std::chrono::seconds deadline{10};
		require(
			completed.wait_for(deadline) == std::future_status::ready && completed.get(),
			"offscreen execution did not succeed before deadline"
		);
		// State publication follows success, independently of native drawable timestamps.
		committed = owners->next;
		constexpr std::size_t pixelBytes = 8;
		std::array<std::byte, pixelBytes> pixel{};
		NSData *const data = [NSData dataWithBytes:staging->buffer.contents length:pixel.size()];
		[data getBytes:pixel.data() length:pixel.size()];
		++step;
		require(
			std::is_eq(stana::graphics_detail::halfAt(pixel, 0) <=> static_cast<double>(step)),
			"temporal counter differs from independently expected integer"
		);
	}
	std::cout << "Native offscreen: " << step
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
