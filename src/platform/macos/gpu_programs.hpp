#ifndef STANA_MACOS_GPU_PROGRAMS_HPP
#define STANA_MACOS_GPU_PROGRAMS_HPP
#include "gpu_resources.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace stana::platform::detail::gpu {
struct RenderPipeline {
	stana::rendering::detail::shader::Entry vertex;
	stana::rendering::detail::shader::Entry fragment;
	id<MTLRenderPipelineState> state = nil;
};

struct ComputePipeline {
	stana::rendering::detail::shader::Entry entry;
	id<MTLComputePipelineState> state = nil;
};

struct Pipeline {
	std::uint64_t version = 0;
	std::vector<std::variant<RenderPipeline, ComputePipeline>> passes;
};

[[nodiscard]] auto compileProgram(const std::shared_ptr<DeviceResources> &, const Program &)
	-> Result<std::shared_ptr<const Pipeline>>;
/// @brief UI admission: one active compiler/device and at most one latest queued job/window.
void requestCompilation(
	const std::shared_ptr<DeviceResources> &,
	std::uint64_t window,
	std::shared_ptr<const Program>,
	std::function<void(Result<std::shared_ptr<const Pipeline>>, bool)>
);
void cancelCompilation(const std::shared_ptr<DeviceResources> &, std::uint64_t window);

struct ProgramBundle {
	std::shared_ptr<const Program> source;
	std::shared_ptr<const Pipeline> pipeline;
	std::map<std::string, std::shared_ptr<TextureOwner>, std::less<>> states;
	std::map<std::string, std::shared_ptr<TextureOwner>, std::less<>> textures;
	std::map<std::string, std::shared_ptr<BufferOwner>, std::less<>> buffers;
	std::map<std::string, id<MTLSamplerState>, std::less<>> samplers;
	std::shared_ptr<TextureOwner> output;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
};

struct ProgramCandidate {
	std::shared_ptr<ProgramBundle> next;
	std::vector<std::shared_ptr<TextureOwner>> retainedTextures{};
	std::vector<std::shared_ptr<BufferOwner>> retainedBuffers{};
};

/// @brief Encodes a complete dependency plan; caller commits next only after GPU success.
[[nodiscard]] auto encodeProgram(
	Resources &,
	id<MTLCommandBuffer>,
	std::shared_ptr<const Program>,
	std::shared_ptr<const Pipeline>,
	std::uint32_t width,
	std::uint32_t height,
	const std::shared_ptr<const ProgramBundle> &previous
) -> Result<ProgramCandidate>;
} // namespace stana::platform::detail::gpu
#endif
