#include "compiler.hpp"
#include "../limits.hpp"
#include "parsed.hpp"
#include <algorithm>
#include <cmath>
#include <src/tint/lang/wgsl/inspector/inspector.h>
#include <stdexcept>
#include <utility>

namespace stana::gpu::detail::shader {
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

auto validateBindings(
	std::span<const BindingRequirement> requirements,
	std::span<const Binding> bindings
) -> std::expected<void, Error> {
	for (const auto &required : requirements) {
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

auto validate(const Request &request) -> std::expected<ValidatedEntry, Error> {
	if (request.source.empty() || request.source.size() > stana::gpu::detail::sourceLimit) {
		return failure(Error::Category::compiler, "WGSL source exceeds the supported profile");
	}
	const auto parsed = std::make_shared<ParsedShader>(request.source);
	const auto &program = parsed->program;
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
	ValidatedEntry result{.parsed = parsed, .function = request.entry, .stage = request.stage};
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
		result.constants.emplace(
			id->second.value,
			std::visit(
				[](auto constant) -> double {
					return static_cast<double>(constant);
				},
				value
			)
		);
	}
	auto resources = inspector.GetResourceBindings(request.entry);
	std::ranges::sort(resources, [](const RB &left, const RB &right) -> bool {
		return std::pair{left.bind_group, left.binding} <
			std::pair{right.bind_group, right.binding};
	});
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
		result.bindings.push_back(
			{.logical = *bound,
				.minimumBytes = resource.size,
				.uniform = resource.resource_type == RB::ResourceType::kUniformBuffer}
		);
	}
	if (inspector.has_error()) {
		return failure(Error::Category::compiler, inspector.error());
	}
	const auto checked = validateBindings(result.bindings, request.bindings);
	if (!checked) {
		return std::unexpected(checked.error());
	}
	return result;
}
} // namespace stana::gpu::detail::shader
