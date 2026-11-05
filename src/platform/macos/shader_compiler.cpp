#include "shader_compiler.hpp"
#include "../../gpu/shader/parsed.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <src/tint/lang/msl/writer/writer.h>
#include <utility>

namespace stana::platform::detail::gpu::shader {
namespace {
[[nodiscard]] auto failure(Error::Category category, std::string message)
	-> std::unexpected<Error> {
	constexpr std::size_t messageLimit = 8192;
	if (message.size() > messageLimit) {
		message.resize(messageLimit);
	}
	return std::unexpected(Error{.category = category, .message = std::move(message)});
}

} // namespace

auto validateBindings(const Entry &entry, const std::vector<Binding> &bindings)
	-> std::expected<void, Error> {
	for (const auto &required : entry.bindings) {
		const stana::gpu::detail::shader::BindingRequirement requirement{
			.logical = required.logical,
			.minimumBytes = required.minimumBytes
		};
		const auto checked =
			stana::gpu::detail::shader::validateBindings(std::span{&requirement, 1}, bindings);
		if (!checked) {
			return checked;
		}
	}
	return {};
}

auto compile(const Request &request) -> std::expected<Entry, Error> {
	const auto validated = stana::gpu::detail::shader::validate(request);
	if (!validated) {
		return std::unexpected(validated.error());
	}
	return lower(*validated);
}

auto lower(const stana::gpu::detail::shader::ValidatedEntry &entry) -> std::expected<Entry, Error> {
	if (!entry.parsed) {
		return failure(Error::Category::compiler, "Missing validated WGSL program");
	}
	tint::msl::writer::Options options;
	options.entry_point_name = entry.function;
	options.remapped_entry_point_name = "stana_entry";
	options.immediate_binding_point =
		tint::BindingPoint{.group = 0, .binding = immediateBufferSlot};
	constexpr std::uint32_t immediateHeaderBytes = 16;
	options.non_constant_zero_offset = 0;
	options.array_length_from_constants.buffer_sizes_offset = immediateHeaderBytes;
	for (const auto &[id, value] : entry.constants) {
		options.substitute_overrides_config.map.emplace(tint::OverrideId{.value = id}, value);
	}
	Entry result{.function = options.remapped_entry_point_name, .stage = entry.stage};
	std::array<std::uint32_t, 3> nextSlot{};
	result.immediateWords.resize(immediateHeaderBytes / sizeof(std::uint32_t));
	std::uint32_t storageIndex = 0;
	for (const auto &resource : entry.bindings) {
		const auto *bound = &resource.logical;
		SlotKind kind = SlotKind::buffer;
		if (bound->kind == stana::gpu::detail::shader::Kind::sampler) {
			kind = SlotKind::sampler;
		} else if (bound->kind == stana::gpu::detail::shader::Kind::texture ||
			bound->kind == stana::gpu::detail::shader::Kind::target ||
			bound->kind == stana::gpu::detail::shader::Kind::state) {
			kind = SlotKind::texture;
		}
		auto &next = nextSlot.at(static_cast<std::size_t>(kind));
		constexpr std::uint32_t samplerLimit = 16;
		constexpr std::uint32_t textureLimit = 128;
		const auto limit = kind == SlotKind::buffer
			? immediateBufferSlot
			: (kind == SlotKind::sampler ? samplerLimit : textureLimit);
		if (next >= limit) {
			return failure(
				Error::Category::capability,
				"Entry point exceeds adapter binding profile"
			);
		}
		const auto slot = next++;
		std::optional<std::uint32_t> sizeWord;
		const tint::BindingPoint logical{.group = bound->group, .binding = bound->binding};
		const tint::BindingPoint mapped{.group = 0, .binding = slot};
		if (kind == SlotKind::sampler) {
			options.bindings.sampler.emplace(logical, mapped);
		} else if (kind == SlotKind::texture) {
			if (bound->writable) {
				options.bindings.storage_texture.emplace(logical, mapped);
			} else {
				options.bindings.texture.emplace(logical, mapped);
			}
		} else if (resource.uniform) {
			options.bindings.uniform.emplace(logical, mapped);
		} else {
			options.bindings.storage.emplace(logical, mapped);
			sizeWord = immediateHeaderBytes / sizeof(std::uint32_t) + storageIndex;
			options.array_length_from_constants.bindpoint_to_size_index.emplace(
				mapped,
				storageIndex++
			);
			if (bound->byteLength > std::numeric_limits<std::uint32_t>::max()) {
				return failure(
					Error::Category::binding,
					"Buffer size cannot be represented by the lowered shader"
				);
			}
			result.immediateWords.push_back(static_cast<std::uint32_t>(bound->byteLength));
		}
		result.bindings.push_back(
			{.logical = *bound,
				.kind = kind,
				.slot = slot,
				.minimumBytes = resource.minimumBytes,
				.sizeWord = sizeWord}
		);
	}
	while (result.immediateWords.size() % 4 != 0) {
		result.immediateWords.push_back(0);
	}
	auto ir = tint::wgsl::reader::ProgramToLoweredIR(entry.parsed->program);
	if (ir != tint::Success) {
		return failure(Error::Category::compiler, ir.Failure().reason);
	}
	auto output = tint::msl::writer::Generate(ir.Get(), options);
	if (output != tint::Success) {
		return failure(Error::Category::compiler, output.Failure().reason);
	}
	result.source = std::move(output->msl);
	if (entry.stage == Stage::compute) {
		const auto &work = output->workgroup_info;
		constexpr std::uint64_t threadsLimit = 256;
		constexpr std::uint64_t memoryLimit = 16ULL * 1024;
		std::uint64_t product = 1;
		for (const auto axis : {work.x, work.y, work.z}) {
			if (axis == 0 || axis > threadsLimit / product) {
				return failure(
					Error::Category::capability,
					"WGSL workgroup exceeds portable thread limit"
				);
			}
			product *= axis;
		}
		if (work.storage_size > memoryLimit || work.subgroup_size) {
			return failure(
				Error::Category::capability,
				"WGSL workgroup memory/subgroup exceeds portable profile"
			);
		}
		result.workgroupSize = {work.x, work.y, work.z};
		result.workgroupBytes = work.storage_size;
		result.dynamicWorkgroupBytes = std::move(output->workgroup_allocations);
	}
	return result;
}
} // namespace stana::platform::detail::gpu::shader
