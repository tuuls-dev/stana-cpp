module;
#include "limits.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
module stana;
import :rendering;

namespace stana::rendering {
namespace {
using ResourceMap = std::map<std::string, GPUResource, std::less<>>;

[[nodiscard]] auto invalid(std::string message) -> std::unexpected<GPUError> {
	return std::unexpected(
		GPUError{.category = GPUError::Category::invalidDescriptor, .message = std::move(message)}
	);
}

[[nodiscard]] auto validName(std::string_view value) -> bool {
	if (value.empty() || value.size() > stana::rendering::detail::nameLimit ||
		value.find('\0') != std::string_view::npos) {
		return false;
	}
	constexpr unsigned asciiLimit = 128;
	constexpr unsigned continuationMask = 0xc0;
	constexpr unsigned continuation = 0x80;
	constexpr unsigned lowMask = 0x3f;
	constexpr unsigned shift = 6;
	for (std::size_t offset = 0; offset < value.size();) {
		const auto first = static_cast<unsigned char>(value.at(offset++));
		if (first < asciiLimit) {
			continue;
		}
		unsigned count = 0;
		std::uint32_t scalar = 0;
		std::uint32_t minimum = 0;
		constexpr unsigned twoMask = 0xe0;
		constexpr unsigned twoPrefix = 0xc0;
		constexpr unsigned threeMask = 0xf0;
		constexpr unsigned threePrefix = 0xe0;
		constexpr unsigned fourMask = 0xf8;
		constexpr unsigned fourPrefix = 0xf0;
		constexpr unsigned twoPayload = 0x1fU;
		constexpr unsigned threePayload = 0x0fU;
		constexpr unsigned fourPayload = 0x07U;
		constexpr unsigned twoMinimum = 0x80;
		constexpr unsigned threeMinimum = 0x800;
		constexpr unsigned fourMinimum = 0x10000;
		if ((first & twoMask) == twoPrefix) {
			count = 1;
			scalar = first & twoPayload;
			minimum = twoMinimum;
		} else if ((first & threeMask) == threePrefix) {
			count = 2;
			scalar = first & threePayload;
			minimum = threeMinimum;
		} else if ((first & fourMask) == fourPrefix) {
			count = 3;
			scalar = first & fourPayload;
			minimum = fourMinimum;
		} else {
			return false;
		}
		if (count > value.size() - offset) {
			return false;
		}
		for (unsigned index = 0; index < count; ++index) {
			const auto next = static_cast<unsigned char>(value.at(offset++));
			if ((next & continuationMask) != continuation) {
				return false;
			}
			scalar = (scalar << shift) | (next & lowMask);
		}
		constexpr std::uint32_t surrogateStart = 0xd800;
		constexpr std::uint32_t surrogateEnd = 0xdfff;
		constexpr std::uint32_t scalarMaximum = 0x10ffff;
		if (scalar < minimum || scalar > scalarMaximum ||
			(scalar >= surrogateStart && scalar <= surrogateEnd)) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] auto textureFormat(const GPUResource &resource) -> std::optional<GPUTextureFormat> {
	return std::visit(
		[](const auto &value) -> std::optional<GPUTextureFormat> {
			using T = std::decay_t<decltype(value)>;
			if constexpr (std::is_same_v<T, GPUTexture> || std::is_same_v<T, GPUStateSlot>) {
				return value.descriptor().format;
			} else if constexpr (std::is_same_v<T, GPUTarget>) {
				return value.format;
			} else {
				return {};
			}
		},
		resource
	);
}

[[nodiscard]] auto bytesPerPixel(GPUTextureFormat format) -> std::size_t {
	switch (format) {
	case GPUTextureFormat::bgra8Unorm:
		return 4;
	case GPUTextureFormat::rgba16Float: {
		constexpr std::size_t halfPixelBytes = 8;
		return halfPixelBytes;
	}
	case GPUTextureFormat::r8Unorm:
		return 1;
	}
	return 0;
}

struct PassPlan {
	std::set<std::string> reads;
	std::set<std::string> writes;
};

[[nodiscard]] auto validate(const GPUProgramDescriptor &descriptor)
	-> std::expected<std::vector<std::size_t>, GPUError> {
	const auto &resources = descriptor.resources;
	if (resources.empty() || resources.size() > stana::rendering::detail::descriptorLimit ||
		descriptor.passes.empty() ||
		descriptor.passes.size() > stana::rendering::detail::descriptorLimit ||
		descriptor.constants.size() > stana::rendering::detail::descriptorLimit) {
		return invalid("Program resource/pass/constant count exceeds limits");
	}
	if (!validName(descriptor.output) || !resources.contains(descriptor.output) ||
		!textureFormat(resources.at(descriptor.output)) ||
		(!std::holds_alternative<GPUTarget>(resources.at(descriptor.output)) &&
			!std::holds_alternative<GPUStateSlot>(resources.at(descriptor.output)))) {
		return invalid("Output must name a produced target or state texture");
	}
	if ((descriptor.outputColor != GPUColorEncoding::encodedSRGB &&
			descriptor.outputColor != GPUColorEncoding::extendedLinear) ||
		!std::isfinite(descriptor.sourcePeak) || descriptor.sourcePeak <= 0 ||
		descriptor.sourcePeak > stana::rendering::detail::halfMaximum ||
		(descriptor.samplingRadius &&
			(!std::isfinite(*descriptor.samplingRadius) || *descriptor.samplingRadius < 0))) {
		return invalid("Invalid program color peak or sampling radius");
	}
	std::size_t ownedBytes = descriptor.shader.source().size();
	std::set<std::uint64_t> counted;
	for (const auto &[name, resource] : resources) {
		if (!validName(name) || name.starts_with('$')) {
			return invalid("Invalid or reserved resource name");
		}
		const auto allocation = std::visit(
			[&](const auto &value) -> std::expected<std::size_t, GPUError> {
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, GPUBuffer> || std::is_same_v<T, GPUTexture>) {
					return counted.insert(value.version()).second ? value.bytes().size() : 0;
				} else if constexpr (std::is_same_v<T, GPUStateSlot>) {
					return counted.insert(value.version()).second
						? value.descriptor().initialBytes.size()
						: 0;
				} else if constexpr (std::is_same_v<T, GPUStorage>) {
					if (value.length == 0 ||
						value.length > stana::rendering::detail::payloadLimit) {
						return invalid("Invalid transient storage length");
					}
					return 0;
				} else if constexpr (std::is_same_v<T, GPUTarget>) {
					const auto bpp = bytesPerPixel(value.format);
					if (bpp == 0) {
						return invalid("Invalid target format");
					}
					const auto length = stana::rendering::detail::textureBytes(
						value.width,
						value.height,
						bpp,
						true
					);
					if (!length) {
						return invalid(length.error());
					}
					return 0;
				} else {
					if (value.filter != GPUSampler::Filter::nearest &&
						value.filter != GPUSampler::Filter::linear) {
						return invalid("Invalid sampler filter");
					}
					return 0;
				}
			},
			resource
		);
		if (!allocation) {
			return std::unexpected(allocation.error());
		}
		if (*allocation > stana::rendering::detail::payloadLimit - ownedBytes) {
			return invalid("Aggregate program input exceeds budget");
		}
		ownedBytes += *allocation;
	}
	for (const auto &[name, value] : descriptor.constants) {
		if (!validName(name) ||
			(std::holds_alternative<float>(value) && !std::isfinite(std::get<float>(value)))) {
			return invalid("Invalid override name or value");
		}
	}
	std::vector<PassPlan> plans(descriptor.passes.size());
	std::map<std::string, std::size_t, std::less<>> producers;
	for (std::size_t index = 0; index < descriptor.passes.size(); ++index) {
		auto &plan = plans.at(index);
		const auto &pass = descriptor.passes.at(index);
		const auto *render = std::get_if<GPURenderPass>(&pass);
		const auto *compute = std::get_if<GPUComputePass>(&pass);
		if (render == nullptr && compute == nullptr) {
			return invalid("Invalid pass variant");
		}
		const std::vector<GPUBinding> &bindings =
			render != nullptr ? render->bindings : compute->bindings;
		if (bindings.size() > stana::rendering::detail::descriptorLimit * 2) {
			return invalid("Too many stage binding declarations");
		}
		if (render != nullptr) {
			if (!validName(render->vertex) || !validName(render->fragment) ||
				render->vertexCount == 0 || render->instanceCount == 0 ||
				!resources.contains(render->target)) {
				return invalid("Invalid render entry, counts or target");
			}
			const auto &target = resources.at(render->target);
			if (!std::holds_alternative<GPUTarget>(target) &&
				!std::holds_alternative<GPUStateSlot>(target)) {
				return invalid("Render attachment must be a target or state slot");
			}
			plan.writes.insert(render->target);
			if (render->indexBuffer) {
				const auto found = resources.find(*render->indexBuffer);
				const auto *buffer =
					found == resources.end() ? nullptr : std::get_if<GPUBuffer>(&found->second);
				constexpr std::size_t indexBytes = 4;
				if (buffer == nullptr || buffer->bytes().size() % indexBytes != 0 ||
					render->vertexCount > buffer->bytes().size() / indexBytes) {
					return invalid("Invalid or undersized index buffer");
				}
			}
		} else {
			if (!validName(compute->function)) {
				return invalid("Invalid compute entry");
			}
			std::uint64_t product = 1;
			for (const auto axis : compute->workgroupCount) {
				if (axis == 0 || axis > stana::rendering::detail::invocationLimit / product) {
					return invalid("Compute workgroup count exceeds portable invocation limit");
				}
				product *= axis;
			}
		}
		std::map<std::pair<std::uint32_t, std::uint32_t>, GPUBinding> coordinates;
		std::set<std::tuple<std::uint32_t, std::uint32_t, GPUBinding::Stage>> declarations;
		for (const auto &binding : bindings) {
			if ((binding.stage != GPUBinding::Stage::vertex &&
					binding.stage != GPUBinding::Stage::fragment &&
					binding.stage != GPUBinding::Stage::compute) ||
				(binding.access != GPUBinding::Access::read &&
					binding.access != GPUBinding::Access::write) ||
				binding.group >= 4 ||
				binding.binding >= stana::rendering::detail::descriptorLimit ||
				!resources.contains(binding.resource)) {
				return invalid("Invalid logical binding coordinate or missing resource");
			}
			const bool computeStage = binding.stage == GPUBinding::Stage::compute;
			if (computeStage != (compute != nullptr)) {
				return invalid("Binding stage does not match pass");
			}
			const auto coordinate = std::pair{binding.group, binding.binding};
			if (!declarations.emplace(binding.group, binding.binding, binding.stage).second) {
				return invalid("Duplicate binding in the same stage");
			}
			if (const auto existing = coordinates.find(coordinate); existing != coordinates.end()) {
				const auto &previous = existing->second;
				if (previous.resource != binding.resource || previous.access != binding.access ||
					previous.previous != binding.previous) {
					return invalid("Conflicting logical binding declarations");
				}
				continue;
			}
			coordinates.emplace(coordinate, binding);
			if (coordinates.size() > stana::rendering::detail::descriptorLimit) {
				return invalid("Too many distinct logical bindings");
			}
			const auto &resource = resources.at(binding.resource);
			const bool state = std::holds_alternative<GPUStateSlot>(resource);
			const bool write = binding.access == GPUBinding::Access::write;
			if (binding.previous && (!state || write)) {
				return invalid("Previous binding requires read-only state");
			}
			if (write) {
				if (render != nullptr ||
					(!state && !std::holds_alternative<GPUTarget>(resource) &&
						!std::holds_alternative<GPUStorage>(resource))) {
					return invalid("Immutable resource or render-stage storage write");
				}
				if (!plan.writes.insert(binding.resource).second) {
					return invalid("Multiple write aliases or attachment binding");
				}
			} else if (!binding.previous) {
				plan.reads.insert(binding.resource);
			}
		}
		for (const auto &write : plan.writes) {
			if (plan.reads.contains(write)) {
				return invalid("Same-pass current read/write alias");
			}
			if (!producers.emplace(write, index).second) {
				return invalid("Resource has multiple writers");
			}
		}
	}
	if (!producers.contains(descriptor.output)) {
		return invalid("Output has no current-step producer");
	}
	std::vector<std::set<std::size_t>> edges(plans.size());
	std::vector<std::size_t> indegrees(plans.size());
	for (std::size_t index = 0; index < plans.size(); ++index) {
		for (const auto &read : plans.at(index).reads) {
			const auto &resource = resources.at(read);
			if (std::holds_alternative<GPUTarget>(resource) ||
				std::holds_alternative<GPUStorage>(resource) ||
				std::holds_alternative<GPUStateSlot>(resource)) {
				const auto writer = producers.find(read);
				if (writer == producers.end()) {
					return invalid("Current transient/state read has no producer");
				}
				if (edges.at(writer->second).insert(index).second) {
					++indegrees.at(index);
				}
			}
		}
	}
	std::set<std::size_t> ready;
	for (std::size_t index = 0; index < plans.size(); ++index) {
		if (indegrees.at(index) == 0) {
			ready.insert(index);
		}
	}
	std::vector<std::size_t> order;
	while (!ready.empty()) {
		const auto index = *ready.begin();
		ready.erase(ready.begin());
		order.push_back(index);
		for (const auto consumer : edges.at(index)) {
			if (--indegrees.at(consumer) == 0) {
				ready.insert(consumer);
			}
		}
	}
	if (order.size() != plans.size()) {
		return invalid("Cyclic program resource dependencies");
	}
	return order;
}

[[nodiscard]] auto compatible(const ResourceMap &before, const ResourceMap &after) -> bool {
	if (before.size() != after.size()) {
		return false;
	}
	for (const auto &[name, resource] : before) {
		const auto replacement = after.find(name);
		if (replacement == after.end() || resource.index() != replacement->second.index() ||
			textureFormat(resource) != textureFormat(replacement->second)) {
			return false;
		}
		if (const auto *texture = std::get_if<GPUTexture>(&resource)) {
			const auto &other = std::get<GPUTexture>(replacement->second).descriptor();
			if (texture->descriptor().encoding != other.encoding ||
				texture->descriptor().premultiplied != other.premultiplied) {
				return false;
			}
		}
	}
	return true;
}

[[nodiscard]] auto sameState(const ResourceMap &before, const ResourceMap &after) -> bool {
	if (before.size() != after.size()) {
		return false;
	}
	for (const auto &[name, resource] : before) {
		if (const auto *state = std::get_if<GPUStateSlot>(&resource)) {
			const auto &left = state->descriptor();
			const auto &right = std::get<GPUStateSlot>(after.at(name)).descriptor();
			if (left.width != right.width || left.height != right.height ||
				left.format != right.format || left.resetKey != right.resetKey ||
				left.initialBytes != right.initialBytes) {
				return false;
			}
		}
	}
	return true;
}
} // namespace

struct GPUProgram::Data {
	std::uint64_t version = stana::detail::nextIdentity();
	std::uint64_t pipeline = stana::detail::nextIdentity();
	std::uint64_t state = stana::detail::nextIdentity();
	GPUProgramDescriptor descriptor;
	std::vector<std::size_t> order;
};

auto GPUProgram::create(GPUProgramDescriptor descriptor) -> std::expected<GPUProgram, GPUError> {
	auto order = validate(descriptor);
	if (!order) {
		return std::unexpected(order.error());
	}
	return GPUProgram{std::make_shared<const Data>(
		Data{.descriptor = std::move(descriptor), .order = std::move(*order)}
	)};
}

auto GPUProgram::updatingResources(ResourceMap resources) const
	-> std::expected<GPUProgram, GPUError> {
	auto descriptor = data_->descriptor;
	descriptor.resources = std::move(resources);
	auto order = validate(descriptor);
	if (!order) {
		return std::unexpected(order.error());
	}
	const bool pipelineCompatible = compatible(data_->descriptor.resources, descriptor.resources);
	const bool stateCompatible =
		pipelineCompatible && sameState(data_->descriptor.resources, descriptor.resources);
	return GPUProgram{std::make_shared<const Data>(Data{
		.pipeline = pipelineCompatible ? data_->pipeline : stana::detail::nextIdentity(),
		.state = stateCompatible ? data_->state : stana::detail::nextIdentity(),
		.descriptor = std::move(descriptor),
		.order = std::move(*order)
	})};
}

auto GPUProgram::descriptor() const noexcept -> const GPUProgramDescriptor & {
	return data_->descriptor;
}

auto GPUProgram::executionOrder() const noexcept -> std::span<const std::size_t> {
	return data_->order;
}

auto GPUProgram::version() const noexcept -> std::uint64_t {
	return data_->version;
}

auto GPUProgram::pipelineVersion() const noexcept -> std::uint64_t {
	return data_->pipeline;
}

auto GPUProgram::stateVersion() const noexcept -> std::uint64_t {
	return data_->state;
}

auto GPUProgram::operator==(const GPUProgram &other) const noexcept -> bool {
	return data_ == other.data_;
}
} // namespace stana::rendering
