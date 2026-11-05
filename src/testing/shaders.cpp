module;
#include "../graphics/limits.hpp"
#include "../graphics/shader_compiler.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
module stana.testing;

namespace stana {
namespace {
[[nodiscard]] auto format(GPUTextureFormat value) -> shader_detail::Format {
	switch (value) {
	case GPUTextureFormat::bgra8Unorm:
		return shader_detail::Format::bgra8;
	case GPUTextureFormat::rgba16Float:
		return shader_detail::Format::rgba16;
	case GPUTextureFormat::r8Unorm:
		return shader_detail::Format::r8;
	}
	throw std::invalid_argument("Unknown shader texture format");
}

[[nodiscard]] auto stage(GPUBinding::Stage value) -> shader_detail::Stage {
	switch (value) {
	case GPUBinding::Stage::vertex:
		return shader_detail::Stage::vertex;
	case GPUBinding::Stage::fragment:
		return shader_detail::Stage::fragment;
	case GPUBinding::Stage::compute:
		return shader_detail::Stage::compute;
	}
	throw std::invalid_argument("Unknown shader stage");
}

[[nodiscard]] auto binding(const GPUProgramDescriptor &descriptor, const GPUBinding &input)
	-> shader_detail::Binding {
	shader_detail::Binding result{
		.group = input.group,
		.binding = input.binding,
		.writable = input.access == GPUBinding::Access::write
	};
	std::visit(
		[&](const auto &resource) -> void {
			using T = std::decay_t<decltype(resource)>;
			if constexpr (std::is_same_v<T, GPUBuffer>) {
				result.kind = shader_detail::Kind::buffer;
				result.byteLength = resource.bytes().size();
			} else if constexpr (std::is_same_v<T, GPUStorage>) {
				result.kind = shader_detail::Kind::storage;
				result.byteLength = resource.length;
			} else if constexpr (std::is_same_v<T, GPUTexture>) {
				result.kind = shader_detail::Kind::texture;
				result.format = format(resource.descriptor().format);
			} else if constexpr (std::is_same_v<T, GPUTarget>) {
				result.kind = shader_detail::Kind::target;
				result.format = format(resource.format);
			} else if constexpr (std::is_same_v<T, GPUStateSlot>) {
				result.kind = shader_detail::Kind::state;
				result.format = format(resource.descriptor().format);
			} else if constexpr (std::is_same_v<T, GPUSampler>) {
				result.kind = shader_detail::Kind::sampler;
			} else {
				static_assert(!sizeof(T), "Unhandled GPU resource");
			}
		},
		descriptor.resources.at(input.resource)
	);
	return result;
}

[[nodiscard]] auto error(const shader_detail::Error &failure) -> GPUError {
	GPUError::Category category{};
	switch (failure.category) {
	case shader_detail::Error::Category::compiler:
		category = GPUError::Category::compiler;
		break;
	case shader_detail::Error::Category::binding:
		category = GPUError::Category::binding;
		break;
	case shader_detail::Error::Category::capability:
		category = GPUError::Category::capability;
		break;
	}
	return {.category = category, .message = failure.message};
}
} // namespace

auto validateGPUProgram(const GPUProgram &program) -> std::expected<void, GPUError> {
	const auto &descriptor = program.descriptor();
	for (const auto &pass : descriptor.passes) {
		const auto checked = std::visit(
			[&](const auto &input) -> std::expected<void, GPUError> {
				using T = std::decay_t<decltype(input)>;
				const auto entry = [&](const std::string &name, GPUBinding::Stage shaderStage)
					-> std::expected<shader_detail::Entry, GPUError> {
					shader_detail::Request request{
						.source = descriptor.shader.source(),
						.entry = name,
						.stage = stage(shaderStage),
						.constants = descriptor.constants
					};
					for (const auto &resource : input.bindings) {
						if (resource.stage == shaderStage) {
							request.bindings.push_back(binding(descriptor, resource));
						}
					}
					auto result = shader_detail::compile(request);
					if (!result) {
						return std::unexpected(error(result.error()));
					}
					return std::move(*result);
				};
				if constexpr (std::is_same_v<T, GPURenderPass>) {
					const auto vertex = entry(input.vertex, GPUBinding::Stage::vertex);
					if (!vertex) {
						return std::unexpected(vertex.error());
					}
					const auto fragment = entry(input.fragment, GPUBinding::Stage::fragment);
					if (!fragment) {
						return std::unexpected(fragment.error());
					}
				} else if constexpr (std::is_same_v<T, GPUComputePass>) {
					const auto compute = entry(input.function, GPUBinding::Stage::compute);
					if (!compute) {
						return std::unexpected(compute.error());
					}
					std::uint64_t product = 1;
					for (const auto axis : compute->workgroupSize) {
						if (axis == 0 || axis > graphics_detail::invocationLimit / product) {
							return std::unexpected(
								GPUError{
									.category = GPUError::Category::capability,
									.message = "Resolved compute invocation product exceeds limit"
								}
							);
						}
						product *= axis;
					}
					for (const auto axis : input.workgroupCount) {
						if (axis == 0 || axis > graphics_detail::invocationLimit / product) {
							return std::unexpected(
								GPUError{
									.category = GPUError::Category::capability,
									.message = "Resolved compute invocation product exceeds limit"
								}
							);
						}
						product *= axis;
					}
				} else {
					static_assert(!sizeof(T), "Unhandled GPU pass");
				}
				return {};
			},
			pass
		);
		if (!checked) {
			return checked;
		}
	}
	return {};
}
} // namespace stana
