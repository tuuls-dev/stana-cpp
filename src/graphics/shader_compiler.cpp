#include "shader_compiler.hpp"
#include "limits.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <limits>
#include <optional>
#include <set>
#include <src/tint/lang/msl/writer/writer.h>
#include <src/tint/lang/wgsl/inspector/inspector.h>
#include <src/tint/lang/wgsl/reader/reader.h>
#include <src/tint/utils/diagnostic/diagnostic.h>
#include <type_traits>
#include <utility>

namespace stana::shader_detail {
namespace {
using RB = tint::inspector::ResourceBinding;

[[nodiscard]] auto failure(Error::Category category, std::string message)
	-> std::unexpected<Error> {
	constexpr std::size_t messageLimit = 8192;
	if (message.size() > messageLimit) {
		message.resize(messageLimit);
	}
	return std::unexpected(Error{.category = category, .message = std::move(message)});
}

[[nodiscard]] auto stage(tint::inspector::PipelineStage value) -> Stage {
	switch (value) {
	case tint::inspector::PipelineStage::kVertex:
		return Stage::vertex;
	case tint::inspector::PipelineStage::kFragment:
		return Stage::fragment;
	case tint::inspector::PipelineStage::kCompute:
		return Stage::compute;
	}
	throw std::logic_error("Unknown compiler stage");
}

[[nodiscard]] auto matches(const Binding &binding, const RB &reflected) -> bool {
	const bool buffer = binding.kind == Kind::buffer || binding.kind == Kind::storage;
	const bool texture = binding.kind == Kind::texture || binding.kind == Kind::target ||
		binding.kind == Kind::state;
	if (reflected.array_size) {
		return false;
	}
	switch (reflected.resource_type) {
	case RB::ResourceType::kUniformBuffer:
		return binding.kind == Kind::buffer && !binding.writable;
	case RB::ResourceType::kReadOnlyStorageBuffer:
		return buffer && !binding.writable;
	case RB::ResourceType::kStorageBuffer:
		return binding.kind == Kind::storage && binding.writable;
	case RB::ResourceType::kSampler:
		return binding.kind == Kind::sampler &&
			reflected.sampler_type != RB::SamplerType::kComparison;
	case RB::ResourceType::kSampledTexture:
		return texture && !binding.writable && reflected.dim == RB::TextureDimension::k2d &&
			reflected.sampled_kind != RB::SampledKind::kUInt &&
			reflected.sampled_kind != RB::SampledKind::kSInt;
	case RB::ResourceType::kWriteOnlyStorageTexture:
	case RB::ResourceType::kReadWriteStorageTexture:
		return texture && binding.kind != Kind::texture && binding.writable &&
			binding.format == Format::rgba16 && reflected.dim == RB::TextureDimension::k2d &&
			reflected.image_format == RB::TexelFormat::kRgba16Float;
	case RB::ResourceType::kMultisampledTexture:
	case RB::ResourceType::kReadOnlyStorageTexture:
	case RB::ResourceType::kDepthTexture:
	case RB::ResourceType::kDepthMultisampledTexture:
	case RB::ResourceType::kExternalTexture:
	case RB::ResourceType::kReadOnlyTexelBuffer:
	case RB::ResourceType::kReadWriteTexelBuffer:
	case RB::ResourceType::kInputAttachment:
		return false;
	}
	return false;
}

[[nodiscard]] auto overrideMatches(const Constant &value, tint::inspector::Override::Type type)
	-> bool {
	switch (type) {
	case tint::inspector::Override::Type::kBool:
		return std::holds_alternative<bool>(value);
	case tint::inspector::Override::Type::kFloat32:
		return std::holds_alternative<float>(value) && std::isfinite(std::get<float>(value));
	case tint::inspector::Override::Type::kInt32:
		return std::holds_alternative<std::int32_t>(value);
	case tint::inspector::Override::Type::kUint32:
	case tint::inspector::Override::Type::kFloat16:
		return false;
	}
	return false;
}
} // namespace

auto validateBindings(const Entry &entry, const std::vector<Binding> &bindings)
	-> std::expected<void, Error> {
	for (const auto &required : entry.bindings) {
		const auto found = std::ranges::find_if(bindings, [&](const Binding &value) -> bool {
			return value.group == required.logical.group &&
				value.binding == required.logical.binding;
		});
		if (found == bindings.end() || found->kind != required.logical.kind ||
			found->format != required.logical.format ||
			found->writable != required.logical.writable ||
			found->byteLength < required.minimumBytes) {
			return failure(
				Error::Category::binding,
				"Incompatible or undersized resource at logical group " +
					std::to_string(required.logical.group) + " binding " +
					std::to_string(required.logical.binding)
			);
		}
	}
	return {};
}

auto compile(const Request &request) -> std::expected<Entry, Error> {
	if (request.source.empty() || request.source.size() > graphics_detail::sourceLimit) {
		return failure(Error::Category::compiler, "WGSL source exceeds the supported profile");
	}
	const tint::Source::File file{"application.wgsl", request.source};
	const auto program = tint::wgsl::reader::Parse(&file);
	if (!program.IsValid()) {
		return failure(Error::Category::compiler, program.Diagnostics().Str());
	}
	tint::inspector::Inspector inspector{program};
	if (!inspector.GetUsedExtensionNames().empty()) {
		return failure(
			Error::Category::capability,
			"WGSL extensions are outside the portable canvas profile"
		);
	}
	const auto entries = inspector.GetEntryPoints();
	const auto selected =
		std::ranges::find(entries, request.entry, &tint::inspector::EntryPoint::name);
	if (selected == entries.end() || stage(selected->stage) != request.stage) {
		return failure(
			Error::Category::compiler,
			"Missing WGSL entry point or stage mismatch: " + request.entry
		);
	}
	if (request.stage == Stage::vertex && !selected->input_variables.empty()) {
		return failure(
			Error::Category::capability,
			"Vertex attributes are unsupported; use vertex_index and declared buffers"
		);
	}
	if (selected->immediate_data_size != 0 || !selected->pixel_local_members.empty() ||
		selected->uses_subgroup_matrix || selected->subgroup_invocation_id_used ||
		selected->subgroup_size_used || selected->frag_depth_used || selected->sample_index_used) {
		return failure(
			Error::Category::capability,
			"Entry point uses features outside the canvas profile"
		);
	}
	tint::msl::writer::Options options;
	options.entry_point_name = request.entry;
	options.remapped_entry_point_name = "stana_entry";
	options.immediate_binding_point =
		tint::BindingPoint{.group = 0, .binding = immediateBufferSlot};
	constexpr std::uint32_t immediateHeaderBytes = 16;
	options.non_constant_zero_offset = 0;
	options.array_length_from_constants.buffer_sizes_offset = immediateHeaderBytes;
	const auto overrides = inspector.GetNamedOverrideIds();
	for (const auto &[name, value] : request.constants) {
		const auto id = overrides.find(name);
		if (id == overrides.end()) {
			return failure(Error::Category::compiler, "Unknown WGSL override: " + name);
		}
		bool typeMatched = false;
		for (const auto &entry : entries) {
			for (const auto &declaration : entry.overrides) {
				if (declaration.name == name) {
					typeMatched = overrideMatches(value, declaration.type);
				}
			}
		}
		if (!typeMatched) {
			return failure(Error::Category::compiler, "WGSL override type mismatch: " + name);
		}
		options.substitute_overrides_config.map.emplace(
			id->second,
			std::visit(
				[](auto constant) -> double {
					return static_cast<double>(constant);
				},
				value
			)
		);
	}
	Entry result{.function = options.remapped_entry_point_name, .stage = request.stage};
	std::array<std::uint32_t, 3> nextSlot{};
	auto resources = inspector.GetResourceBindings(request.entry);
	std::ranges::sort(resources, [](const RB &left, const RB &right) -> bool {
		return std::pair{left.bind_group, left.binding} <
			std::pair{right.bind_group, right.binding};
	});
	result.immediateWords.resize(immediateHeaderBytes / sizeof(std::uint32_t));
	std::uint32_t storageIndex = 0;
	for (const auto &resource : resources) {
		const auto bound =
			std::ranges::find_if(request.bindings, [&](const Binding &value) -> bool {
				return value.group == resource.bind_group && value.binding == resource.binding;
			});
		if (bound == request.bindings.end() || !matches(*bound, resource)) {
			return failure(
				Error::Category::binding,
				"Missing or incompatible logical group " + std::to_string(resource.bind_group) +
					" binding " + std::to_string(resource.binding)
			);
		}
		SlotKind kind = SlotKind::buffer;
		if (bound->kind == Kind::sampler) {
			kind = SlotKind::sampler;
		} else if (bound->kind == Kind::texture || bound->kind == Kind::target ||
			bound->kind == Kind::state) {
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
		const tint::BindingPoint logical{.group = resource.bind_group, .binding = resource.binding};
		const tint::BindingPoint mapped{.group = 0, .binding = slot};
		if (kind == SlotKind::sampler) {
			options.bindings.sampler.emplace(logical, mapped);
		} else if (kind == SlotKind::texture) {
			if (bound->writable) {
				options.bindings.storage_texture.emplace(logical, mapped);
			} else {
				options.bindings.texture.emplace(logical, mapped);
			}
		} else if (resource.resource_type == RB::ResourceType::kUniformBuffer) {
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
				.minimumBytes = resource.size,
				.sizeWord = sizeWord}
		);
	}
	if (inspector.has_error()) {
		return failure(Error::Category::compiler, inspector.error());
	}
	const auto checked = validateBindings(result, request.bindings);
	if (!checked) {
		return std::unexpected(checked.error());
	}
	while (result.immediateWords.size() % 4 != 0) {
		result.immediateWords.push_back(0);
	}
	auto ir = tint::wgsl::reader::ProgramToLoweredIR(program);
	if (ir != tint::Success) {
		return failure(Error::Category::compiler, ir.Failure().reason);
	}
	auto output = tint::msl::writer::Generate(ir.Get(), options);
	if (output != tint::Success) {
		return failure(Error::Category::compiler, output.Failure().reason);
	}
	result.source = std::move(output->msl);
	if (request.stage == Stage::compute) {
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
} // namespace stana::shader_detail
