#include "../../../src/graphics/color.hpp"
#include "../../../src/platform/macos/gpu_programs.hpp"
#include "canvas_fixture.hpp"
#import <Foundation/Foundation.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <exception>
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
			return 0;
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	}
}
