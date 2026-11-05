#ifndef STANA_GPU_CONVERSION_HPP
#define STANA_GPU_CONVERSION_HPP

// Included after the application module declaration/imports. No native types escape modules.
namespace stana::application_detail {
[[nodiscard]] inline auto convertFormat(GPUTextureFormat value) -> native::gpu::Format {
	switch (value) {
	case GPUTextureFormat::bgra8Unorm:
		return native::gpu::Format::bgra8;
	case GPUTextureFormat::rgba16Float:
		return native::gpu::Format::rgba16;
	case GPUTextureFormat::r8Unorm:
		return native::gpu::Format::r8;
	}
	throw std::logic_error("Unknown texture format");
}

[[nodiscard]] inline auto convertEncoding(GPUColorEncoding value) -> native::gpu::Encoding {
	switch (value) {
	case GPUColorEncoding::encodedSRGB:
		return native::gpu::Encoding::encoded;
	case GPUColorEncoding::extendedLinear:
		return native::gpu::Encoding::linear;
	}
	throw std::logic_error("Unknown color encoding");
}

[[nodiscard]] inline auto convertMode(RenderingColorMode value) -> native::gpu::ColorMode {
	switch (value) {
	case RenderingColorMode::automatic:
		return native::gpu::ColorMode::automatic;
	case RenderingColorMode::sdr:
		return native::gpu::ColorMode::sdr;
	case RenderingColorMode::extendedLinear:
		return native::gpu::ColorMode::extendedLinear;
	}
	throw std::logic_error("Unknown color mode");
}

[[nodiscard]] inline auto convertStage(GPUBinding::Stage value) -> native::gpu::Stage {
	switch (value) {
	case GPUBinding::Stage::vertex:
		return native::gpu::Stage::vertex;
	case GPUBinding::Stage::fragment:
		return native::gpu::Stage::fragment;
	case GPUBinding::Stage::compute:
		return native::gpu::Stage::compute;
	}
	throw std::logic_error("Unknown shader stage");
}

[[nodiscard]] inline auto convertBindings(const std::vector<GPUBinding> &bindings)
	-> std::vector<native::gpu::Binding> {
	std::vector<native::gpu::Binding> result;
	result.reserve(bindings.size());
	for (const auto &binding : bindings) {
		result.push_back(
			{.resource = binding.resource,
				.group = binding.group,
				.binding = binding.binding,
				.write = binding.access == GPUBinding::Access::write,
				.stage = convertStage(binding.stage),
				.previous = binding.previous}
		);
	}
	return result;
}

/// @brief Weak bounded conversion caches; blobs are shared, packed mesh conversion happens once.
struct GPUConversion {
	std::map<std::uint64_t, std::weak_ptr<const native::gpu::Program>> programs;
	std::map<std::uint64_t, std::weak_ptr<const native::gpu::Mesh>> meshes;
	using DrawingKey = std::tuple<std::uint64_t, Theme, bool>;
	std::map<DrawingKey, std::weak_ptr<const native::gpu::Drawing>> drawings;

	[[nodiscard]] auto program(const GPUProgram &value)
		-> std::shared_ptr<const native::gpu::Program> {
		if (const auto found = programs.find(value.version()); found != programs.end()) {
			if (auto retained = found->second.lock()) {
				return retained;
			}
		}
		const auto &descriptor = value.descriptor();
		auto result = std::make_shared<native::gpu::Program>();
		result->version = value.version();
		result->pipelineVersion = value.pipelineVersion();
		result->stateVersion = value.stateVersion();
		result->source = detail::GPUAccess::sourceOwner(descriptor.shader);
		result->output = descriptor.output;
		result->outputColor = convertEncoding(descriptor.outputColor);
		result->sourcePeak = descriptor.sourcePeak;
		result->constants = descriptor.constants;
		result->order.assign(value.executionOrder().begin(), value.executionOrder().end());
		for (const auto &[name, resource] : descriptor.resources) {
			result->resources.emplace(
				name,
				std::visit(
					[](const auto &input) -> native::gpu::Resource {
						using T = std::decay_t<decltype(input)>;
						if constexpr (std::is_same_v<T, GPUBuffer>) {
							return native::gpu::Buffer{
								.version = input.version(),
								.bytes = detail::GPUAccess::bytesOwner(input)
							};
						} else if constexpr (std::is_same_v<T, GPUStorage>) {
							return native::gpu::Storage{.length = input.length};
						} else if constexpr (std::is_same_v<T, GPUTexture>) {
							const auto &desc = input.descriptor();
							return native::gpu::Texture{
								.version = input.version(),
								.width = desc.width,
								.height = desc.height,
								.format = convertFormat(desc.format),
								.bytes = detail::GPUAccess::bytesOwner(input),
								.encoding = convertEncoding(desc.encoding),
								.premultiplied = desc.premultiplied,
								.sourcePeak = desc.sourcePeak
							};
						} else if constexpr (std::is_same_v<T, GPUTarget>) {
							return native::gpu::Target{
								.width = input.width,
								.height = input.height,
								.format = convertFormat(input.format)
							};
						} else if constexpr (std::is_same_v<T, GPUStateSlot>) {
							const auto &desc = input.descriptor();
							return native::gpu::State{
								.version = input.version(),
								.width = desc.width,
								.height = desc.height,
								.format = convertFormat(desc.format),
								.initial = detail::GPUAccess::initialOwner(input),
								.resetKey = desc.resetKey
							};
						} else if constexpr (std::is_same_v<T, GPUSampler>) {
							switch (input.filter) {
							case GPUSampler::Filter::nearest:
								return native::gpu::Sampler{.linear = false};
							case GPUSampler::Filter::linear:
								return native::gpu::Sampler{.linear = true};
							}
							throw std::logic_error("Unknown sampler filter");
						} else {
							static_assert(!sizeof(T), "Unhandled GPU resource");
						}
					},
					resource
				)
			);
		}
		for (const auto &pass : descriptor.passes) {
			result->passes.push_back(
				std::visit(
					[](const auto &input) -> native::gpu::Pass {
						using T = std::decay_t<decltype(input)>;
						if constexpr (std::is_same_v<T, GPURenderPass>) {
							return native::gpu::RenderPass{
								.vertex = input.vertex,
								.fragment = input.fragment,
								.target = input.target,
								.vertexCount = input.vertexCount,
								.instanceCount = input.instanceCount,
								.indexBuffer = input.indexBuffer,
								.bindings = convertBindings(input.bindings),
								.blends = input.blends
							};
						} else if constexpr (std::is_same_v<T, GPUComputePass>) {
							return native::gpu::ComputePass{
								.function = input.function,
								.workgroupCount = input.workgroupCount,
								.bindings = convertBindings(input.bindings)
							};
						} else {
							static_assert(!sizeof(T), "Unhandled GPU pass");
						}
					},
					pass
				)
			);
		}
		if (programs.size() >= graphics_detail::descriptorLimit) {
			programs.erase(programs.begin());
		}
		programs.insert_or_assign(value.version(), result);
		return result;
	}

	[[nodiscard]] auto mesh(const GPUMesh &value) -> std::shared_ptr<const native::gpu::Mesh> {
		if (const auto found = meshes.find(value.version()); found != meshes.end()) {
			if (auto retained = found->second.lock()) {
				return retained;
			}
		}
		auto result = std::make_shared<native::gpu::Mesh>();
		result->version = value.version();
		result->indices = detail::GPUAccess::indicesOwner(value);
		result->vertices.reserve(value.vertices().size());
		for (const auto &vertex : value.vertices()) {
			const auto &color = vertex.color;
			result->vertices.push_back(
				{.position =
						{static_cast<float>(vertex.position.x),
							static_cast<float>(vertex.position.y)},
					.color = {
						static_cast<float>(color.red() * color.alpha()),
						static_cast<float>(color.green() * color.alpha()),
						static_cast<float>(color.blue() * color.alpha()),
						static_cast<float>(color.alpha())
					}}
			);
		}
		if (meshes.size() >= graphics_detail::descriptorLimit) {
			meshes.erase(meshes.begin());
		}
		meshes.insert_or_assign(value.version(), result);
		return result;
	}

	[[nodiscard]] static auto color(const detail::GPUColor &value, Environment environment)
		-> native::gpu::Color {
		return std::visit(
			[&](const auto &input) -> native::gpu::Color {
				using T = std::decay_t<decltype(input)>;
				if constexpr (std::is_same_v<T, Color>) {
					const auto resolved =
						input.resolved(environment.theme, environment.highContrast);
					return {
						.rgba = {resolved.red, resolved.green, resolved.blue, resolved.alpha},
						.linear = false
					};
				} else {
					return {
						.rgba = {input.red(), input.green(), input.blue(), input.alpha()},
						.linear = true
					};
				}
			},
			value
		);
	}

	[[nodiscard]] static auto rect(Rect bounds) -> native::gpu::Rect {
		return {
			.origin = {.x = bounds.origin.x, .y = bounds.origin.y},
			.width = bounds.size.width,
			.height = bounds.size.height
		};
	}

	[[nodiscard]] auto drawing(const GPUDrawing &value, Environment environment)
		-> std::shared_ptr<const native::gpu::Drawing> {
		const DrawingKey key{value.version(), environment.theme, environment.highContrast};
		if (const auto found = drawings.find(key); found != drawings.end()) {
			if (auto retained = found->second.lock()) {
				return retained;
			}
		}
		auto result = std::make_shared<native::gpu::Drawing>();
		result->version = value.version();
		result->extended = value.requiresExtended();
		for (const auto &command : detail::GPUAccess::commands(value)) {
			result->commands.push_back(
				std::visit(
					[&](const auto &input) -> native::gpu::Command {
						using T = std::decay_t<decltype(input)>;
						if constexpr (std::is_same_v<T, detail::GPURect>) {
							return native::gpu::Shape{
								.bounds = rect(input.bounds),
								.radius = input.radius,
								.color = color(input.color, environment)
							};
						} else if constexpr (std::is_same_v<T, detail::GPUEllipse>) {
							return native::gpu::Shape{
								.bounds = rect(input.bounds),
								.color = color(input.color, environment),
								.ellipse = true
							};
						} else if constexpr (std::is_same_v<T, GPUMesh>) {
							return mesh(input);
						} else if constexpr (std::is_same_v<T, GPUProgram>) {
							return program(input);
						} else if constexpr (std::is_same_v<T, detail::GPUPolyline>) {
							native::gpu::Polyline line{
								.width = input.width,
								.color = color(input.color, environment),
								.closed = input.closed
							};
							line.points.reserve(input.points.size());
							for (const auto point : input.points) {
								line.points.push_back({.x = point.x, .y = point.y});
							}
							return line;
						} else {
							static_assert(!sizeof(T), "Unhandled GPU command");
						}
					},
					command
				)
			);
		}
		constexpr std::size_t conversionLimit = 256;
		if (drawings.size() >= conversionLimit) {
			drawings.erase(drawings.begin());
		}
		drawings.insert_or_assign(key, result);
		return result;
	}
};
} // namespace stana::application_detail
#endif
