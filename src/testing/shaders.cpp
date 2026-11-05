module;
#include "../rendering/limits.hpp"
#include "../rendering/shader/compiler.hpp"
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
module stana;
import :testing;

namespace stana::testing {
namespace {
[[nodiscard]] auto format(stana::rendering::GPUTextureFormat value)
	-> stana::rendering::detail::shader::Format {
	switch (value) {
	case stana::rendering::GPUTextureFormat::bgra8Unorm:
		return stana::rendering::detail::shader::Format::bgra8;
	case stana::rendering::GPUTextureFormat::rgba16Float:
		return stana::rendering::detail::shader::Format::rgba16;
	case stana::rendering::GPUTextureFormat::r8Unorm:
		return stana::rendering::detail::shader::Format::r8;
	}
	throw std::invalid_argument("Unknown shader texture format");
}

[[nodiscard]] auto stage(stana::rendering::GPUBinding::Stage value)
	-> stana::rendering::detail::shader::Stage {
	switch (value) {
	case stana::rendering::GPUBinding::Stage::vertex:
		return stana::rendering::detail::shader::Stage::vertex;
	case stana::rendering::GPUBinding::Stage::fragment:
		return stana::rendering::detail::shader::Stage::fragment;
	case stana::rendering::GPUBinding::Stage::compute:
		return stana::rendering::detail::shader::Stage::compute;
	}
	throw std::invalid_argument("Unknown shader stage");
}

[[nodiscard]] auto binding(
	const stana::rendering::GPUProgramDescriptor &descriptor,
	const stana::rendering::GPUBinding &input
) -> stana::rendering::detail::shader::Binding {
	stana::rendering::detail::shader::Binding result{
		.group = input.group,
		.binding = input.binding,
		.writable = input.access == stana::rendering::GPUBinding::Access::write
	};
	std::visit(
		[&](const auto &resource) -> void {
			using T = std::decay_t<decltype(resource)>;
			if constexpr (std::is_same_v<T, stana::rendering::GPUBuffer>) {
				result.kind = stana::rendering::detail::shader::Kind::buffer;
				result.byteLength = resource.bytes().size();
			} else if constexpr (std::is_same_v<T, stana::rendering::GPUStorage>) {
				result.kind = stana::rendering::detail::shader::Kind::storage;
				result.byteLength = resource.length;
			} else if constexpr (std::is_same_v<T, stana::rendering::GPUTexture>) {
				result.kind = stana::rendering::detail::shader::Kind::texture;
				result.format = format(resource.descriptor().format);
			} else if constexpr (std::is_same_v<T, stana::rendering::GPUTarget>) {
				result.kind = stana::rendering::detail::shader::Kind::target;
				result.format = format(resource.format);
			} else if constexpr (std::is_same_v<T, stana::rendering::GPUStateSlot>) {
				result.kind = stana::rendering::detail::shader::Kind::state;
				result.format = format(resource.descriptor().format);
			} else if constexpr (std::is_same_v<T, stana::rendering::GPUSampler>) {
				result.kind = stana::rendering::detail::shader::Kind::sampler;
			} else {
				static_assert(!sizeof(T), "Unhandled GPU resource");
			}
		},
		descriptor.resources.at(input.resource)
	);
	return result;
}

[[nodiscard]] auto error(const stana::rendering::detail::shader::Error &failure)
	-> stana::rendering::GPUError {
	stana::rendering::GPUError::Category category{};
	switch (failure.category) {
	case stana::rendering::detail::shader::Error::Category::compiler:
		category = stana::rendering::GPUError::Category::compiler;
		break;
	case stana::rendering::detail::shader::Error::Category::binding:
		category = stana::rendering::GPUError::Category::binding;
		break;
	case stana::rendering::detail::shader::Error::Category::capability:
		category = stana::rendering::GPUError::Category::capability;
		break;
	}
	return {.category = category, .message = failure.message};
}
} // namespace

auto validateGPUProgram(const stana::rendering::GPUProgram &program)
	-> std::expected<void, stana::rendering::GPUError> {
	const auto &descriptor = program.descriptor();
	for (const auto &pass : descriptor.passes) {
		const auto checked = std::visit(
			[&](const auto &input) -> std::expected<void, stana::rendering::GPUError> {
				using T = std::decay_t<decltype(input)>;
				const auto entry = [&](const std::string &name,
									   stana::rendering::GPUBinding::Stage shaderStage)
					-> std::expected<
						stana::rendering::detail::shader::Entry,
						stana::rendering::GPUError> {
					stana::rendering::detail::shader::Request request{
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
					auto result = stana::rendering::detail::shader::compile(request);
					if (!result) {
						return std::unexpected(error(result.error()));
					}
					return std::move(*result);
				};
				if constexpr (std::is_same_v<T, stana::rendering::GPURenderPass>) {
					const auto vertex =
						entry(input.vertex, stana::rendering::GPUBinding::Stage::vertex);
					if (!vertex) {
						return std::unexpected(vertex.error());
					}
					const auto fragment =
						entry(input.fragment, stana::rendering::GPUBinding::Stage::fragment);
					if (!fragment) {
						return std::unexpected(fragment.error());
					}
				} else if constexpr (std::is_same_v<T, stana::rendering::GPUComputePass>) {
					const auto compute =
						entry(input.function, stana::rendering::GPUBinding::Stage::compute);
					if (!compute) {
						return std::unexpected(compute.error());
					}
					std::uint64_t product = 1;
					for (const auto axis : compute->workgroupSize) {
						if (axis == 0 ||
							axis > stana::rendering::detail::invocationLimit / product) {
							return std::unexpected(
								stana::rendering::GPUError{
									.category = stana::rendering::GPUError::Category::capability,
									.message = "Resolved compute invocation product exceeds limit"
								}
							);
						}
						product *= axis;
					}
					for (const auto axis : input.workgroupCount) {
						if (axis == 0 ||
							axis > stana::rendering::detail::invocationLimit / product) {
							return std::unexpected(
								stana::rendering::GPUError{
									.category = stana::rendering::GPUError::Category::capability,
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
} // namespace stana::testing
