#include "../../../src/platform/macos/presentation.hpp"
#include "fixtures.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <span>

namespace {
/// @brief Actual shared CGContext -> Metal transfer, warm reuse and independent in-flight
/// ownership.
void transfer(id<MTLDevice> device, stana::platform::detail::LogicalSize size, double scale) {

	id<MTLCommandQueue> const queue = [device newCommandQueue];
	stana::testing::fixtures::platform::macos::require(queue != nil, "command queue");
	auto source = std::make_shared<stana::platform::detail::Scene>(
		*stana::testing::fixtures::platform::macos::scene()
	);
	source->size = size;
	const auto prepared = stana::platform::detail::prepareScene(source, scale);
	const auto grid = stana::platform::detail::makeTileGrid(
		size,
		scale,
		{.maximumBufferLength = device.maxBufferLength}
	);
	const auto accounting = std::make_shared<stana::platform::detail::RasterAccounting>();
	std::vector<std::shared_ptr<stana::platform::detail::RasterTile>> tiles;
	tiles.reserve(grid.tiles.size());
	std::vector<id<MTLTexture>> textures;
	textures.reserve(grid.tiles.size());
	id<MTLCommandBuffer> const buffer = [queue commandBuffer];
	id<MTLBlitCommandEncoder> const encoder = [buffer blitCommandEncoder];
	for (const auto &layout : grid.tiles) {
		auto tile =
			std::make_shared<stana::platform::detail::RasterTile>(layout, device, accounting);
		stana::platform::detail::rasterScene(tile->bitmapContext, *prepared, layout.pixels);
		MTLTextureDescriptor *const descriptor =
			[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
															   width:layout.pixels.width
															  height:layout.pixels.height
														   mipmapped:NO];
		descriptor.storageMode = MTLStorageModeShared;
		id<MTLTexture> const texture = [device newTextureWithDescriptor:descriptor];
		stana::testing::fixtures::platform::macos::require(texture != nil, "readback texture");
		stana::platform::detail::encodeTile(encoder, *tile, texture);
		textures.push_back(texture);
		tiles.push_back(std::move(tile));
	}
	[encoder endEncoding];
	[buffer commit];
	[buffer waitUntilCompleted];
	stana::testing::fixtures::platform::macos::require(
		buffer.status == MTLCommandBufferStatusCompleted &&
			accounting->bytes.load() == grid.rasterBytes,
		"actual full-grid GPU success and allocation accounting"
	);
	for (const auto &layout : grid.tiles) {
		std::vector<std::byte> readback(layout.allocationBytes);
		[textures.at(layout.ordinal)
			   getBytes:readback.data()
			bytesPerRow:layout.bytesPerRow
			 fromRegion:MTLRegionMake2D(0, 0, layout.pixels.width, layout.pixels.height)
			mipmapLevel:0];
		stana::testing::fixtures::platform::macos::Bitmap reference{layout.pixels};
		stana::platform::detail::rasterScene(
			reference.context.get(),
			*prepared,
			layout.pixels,
			false
		);
		for (std::size_t row = 0; row < layout.pixels.height; ++row) {
			const auto count = layout.pixels.width * stana::platform::detail::bytesPerPixel;
			stana::testing::fixtures::platform::macos::require(
				std::ranges::equal(
					std::span{readback}.subspan(row * layout.bytesPerRow, count),
					std::span{reference.bytes}.subspan(row * reference.bytesPerRow, count)
				),
				"GPU readback must exactly match written tile pixels"
			);
		}
	}
	const auto allocations = accounting->allocations.load();
	constexpr unsigned warmIterations = 10;
	const auto start = std::chrono::steady_clock::now();
	double gpuSeconds = 0;
	for (unsigned iteration = 0; iteration < warmIterations; ++iteration) {
		const auto &tile = *tiles.front();
		stana::platform::detail::rasterScene(tile.bitmapContext, *prepared, tile.layout.pixels);
		id<MTLCommandBuffer> const repeated = [queue commandBuffer];
		id<MTLBlitCommandEncoder> const repeatedEncoder = [repeated blitCommandEncoder];
		stana::platform::detail::encodeTile(repeatedEncoder, tile, textures.front());
		[repeatedEncoder endEncoding];
		[repeated commit];
		[repeated waitUntilCompleted];
		stana::testing::fixtures::platform::macos::require(
			repeated.status == MTLCommandBufferStatusCompleted &&
				accounting->allocations.load() == allocations,
			"warm raster/blit allocates no shared storage"
		);
		gpuSeconds += repeated.GPUEndTime - repeated.GPUStartTime;
	}
	const auto elapsed =
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	std::cout << device.name.UTF8String << " scale=" << scale << " physical=" << grid.viewport.width
			  << 'x' << grid.viewport.height << " tiles=" << grid.tiles.size()
			  << " rasterBytes=" << accounting->bytes.load() << " peak=" << accounting->peak.load()
			  << " warmIterations=" << warmIterations << " rasterEncodeWaitMs=" << elapsed
			  << " gpuSeconds=" << gpuSeconds << '\n';
	// Hold a real GPU read behind an event, then drop the host's source owners.
	// Only production BatchResources retained by completion may keep pixels alive.
	id<MTLSharedEvent> const event = [device newSharedEvent];
	stana::testing::fixtures::platform::macos::require(event != nil, "shared GPU event");
	id<MTLCommandBuffer> const pending = [queue commandBuffer];
	[pending encodeWaitForEvent:event value:1];
	id<MTLBlitCommandEncoder> const pendingEncoder = [pending blitCommandEncoder];
	stana::platform::detail::encodeTile(pendingEncoder, *tiles.front(), textures.front());
	[pendingEncoder endEncoding];
	auto resources = std::make_shared<stana::platform::detail::BatchResources>();
	resources->sources = tiles;
	resources->target = prepared;
	const auto finished = std::make_shared<std::atomic_bool>(false);
	[pending addCompletedHandler:^(id<MTLCommandBuffer> completed) {
	  static_cast<void>(completed);
	  resources->sources.clear();
	  resources->target.reset();
	  finished->store(true);
	}];
	[pending commit];
	tiles.clear();
	resources.reset();
	stana::testing::fixtures::platform::macos::require(
		accounting->bytes.load() == grid.rasterBytes,
		"closed host cannot free in-flight GPU sources"
	);
	event.signaledValue = 1;
	[pending waitUntilCompleted];
	stana::testing::fixtures::platform::macos::require(
		pending.status == MTLCommandBufferStatusCompleted && finished->load() &&
			accounting->bytes.load() == 0,
		"independent owner releases safely after GPU read ends"
	);
}
} // namespace

/// @brief Opt-in actual-device qualification; no synthetic success or NSApplication.
auto main() -> int {
	@autoreleasepool {
		try {

			stana::testing::fixtures::platform::macos::require(
				NSApp == nil,
				"offscreen GPU fixture must not create an application"
			);
			id<MTLDevice> const device = MTLCreateSystemDefaultDevice();
			stana::testing::fixtures::platform::macos::require(
				device != nil && device.hasUnifiedMemory &&
					[device supportsFamily:MTLGPUFamilyApple1],
				"Apple Silicon Metal required"
			);
			transfer(
				device,
				{.width = stana::testing::fixtures::platform::macos::fixtureWidth,
					.height = stana::testing::fixtures::platform::macos::fixtureHeight},
				1
			);
			transfer(
				device,
				{.width = stana::testing::fixtures::platform::macos::fixtureWidth / 2,
					.height = stana::testing::fixtures::platform::macos::fixtureHeight / 2},
				2
			);
			constexpr stana::platform::detail::LogicalSize fourK{.width = 3840, .height = 2160};
			constexpr stana::platform::detail::LogicalSize eightK{.width = 7680, .height = 4320};
			transfer(device, fourK, 1);
			transfer(device, eightK, 1);
			stana::testing::fixtures::platform::macos::require(
				NSApp == nil,
				"offscreen GPU fixture created an application"
			);
			return 0;
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	}
}
