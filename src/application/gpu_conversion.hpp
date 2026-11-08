#ifndef STANA_GPU_CONVERSION_HPP
#define STANA_GPU_CONVERSION_HPP

// Included after the application module declaration/imports. No native types escape modules.
namespace stana::application::detail {
[[nodiscard]] inline auto convertFormat(stana::rendering::GPUTextureFormat value)
	-> stana::gpu::detail::Format {
	switch (value) {
	case stana::rendering::GPUTextureFormat::bgra8Unorm:
		return stana::gpu::detail::Format::bgra8;
	case stana::rendering::GPUTextureFormat::rgba16Float:
		return stana::gpu::detail::Format::rgba16;
	case stana::rendering::GPUTextureFormat::r8Unorm:
		return stana::gpu::detail::Format::r8;
	}
	throw std::logic_error("Unknown texture format");
}

[[nodiscard]] inline auto convertEncoding(stana::rendering::GPUColorEncoding value)
	-> stana::gpu::detail::Encoding {
	switch (value) {
	case stana::rendering::GPUColorEncoding::encodedSRGB:
		return stana::gpu::detail::Encoding::encoded;
	case stana::rendering::GPUColorEncoding::extendedLinear:
		return stana::gpu::detail::Encoding::linear;
	}
	throw std::logic_error("Unknown color encoding");
}

[[nodiscard]] inline auto convertMode(stana::rendering::RenderingColorMode value)
	-> stana::gpu::detail::ColorMode {
	switch (value) {
	case stana::rendering::RenderingColorMode::automatic:
		return stana::gpu::detail::ColorMode::automatic;
	case stana::rendering::RenderingColorMode::sdr:
		return stana::gpu::detail::ColorMode::sdr;
	case stana::rendering::RenderingColorMode::extendedLinear:
		return stana::gpu::detail::ColorMode::extendedLinear;
	}
	throw std::logic_error("Unknown color mode");
}

[[nodiscard]] inline auto convertStage(stana::rendering::GPUBinding::Stage value)
	-> stana::gpu::detail::Stage {
	switch (value) {
	case stana::rendering::GPUBinding::Stage::vertex:
		return stana::gpu::detail::Stage::vertex;
	case stana::rendering::GPUBinding::Stage::fragment:
		return stana::gpu::detail::Stage::fragment;
	case stana::rendering::GPUBinding::Stage::compute:
		return stana::gpu::detail::Stage::compute;
	}
	throw std::logic_error("Unknown shader stage");
}

[[nodiscard]] inline auto convertBindings(const std::vector<stana::rendering::GPUBinding> &bindings)
	-> std::vector<stana::gpu::detail::Binding> {
	std::vector<stana::gpu::detail::Binding> result;
	result.reserve(bindings.size());
	for (const auto &binding : bindings) {
		result.push_back(
			{.resource = binding.resource,
				.group = binding.group,
				.binding = binding.binding,
				.write = binding.access == stana::rendering::GPUBinding::Access::write,
				.stage = convertStage(binding.stage),
				.previous = binding.previous}
		);
	}
	return result;
}

[[nodiscard]] inline auto convertContour(const stana::rendering::detail::BoxContour &value)
	-> stana::gpu::detail::BoxContour {
	stana::gpu::detail::BoxContour result{
		.bounds = {
			value.bounds.origin.x.value(),
			value.bounds.origin.y.value(),
			value.bounds.size.width.value(),
			value.bounds.size.height.value()
		}
	};
	for (std::size_t index = 0; index < value.radii.size(); ++index) {
		result.radii.at(
			index
		) = {value.radii.at(index).width.value(), value.radii.at(index).height.value()};
	}
	return result;
}

[[nodiscard]] inline auto convertShadow(const stana::rendering::detail::ShadowPaint &value)
	-> stana::gpu::detail::Shadow {
	return {
		.clip = convertContour(value.clip),
		.caster = convertContour(value.caster),
		.offset = {value.offset.x.value(), value.offset.y.value()},
		.color =
			{value.color.red.value(),
				value.color.green.value(),
				value.color.blue.value(),
				value.color.alpha.value()},
		.blurRadius = value.blurRadius.value(),
		.isInset = value.isInset,
		.ink = {
			value.ink.origin.x.value(),
			value.ink.origin.y.value(),
			value.ink.size.width.value(),
			value.ink.size.height.value()
		}
	};
}

/// @brief Weak bounded conversion caches; blobs are shared, packed mesh conversion happens once.
struct GPUConversion {
	std::map<std::uint64_t, std::weak_ptr<const stana::gpu::detail::Program>> programs;
	std::map<std::uint64_t, std::weak_ptr<const stana::gpu::detail::Mesh>> meshes;
	using DrawingKey = std::tuple<std::uint64_t, Theme, bool, Direction>;
	std::map<DrawingKey, std::weak_ptr<const stana::gpu::detail::Drawing>> drawings;

	[[nodiscard]] auto program(const stana::rendering::GPUProgram &value)
		-> std::shared_ptr<const stana::gpu::detail::Program> {
		if (const auto found = programs.find(value.version()); found != programs.end()) {
			if (auto retained = found->second.lock()) {
				return retained;
			}
		}
		const auto &descriptor = value.descriptor();
		auto result = std::make_shared<stana::gpu::detail::Program>();
		result->version = value.version();
		result->pipelineVersion = value.pipelineVersion();
		result->stateVersion = value.stateVersion();
		result->source = stana::rendering::detail::GPUAccess::sourceOwner(descriptor.shader);
		result->output = descriptor.output;
		result->outputColor = convertEncoding(descriptor.outputColor);
		result->sourcePeak = descriptor.sourcePeak.value();
		result->constants = descriptor.constants;
		result->order.assign(value.executionOrder().begin(), value.executionOrder().end());
		for (const auto &[name, resource] : descriptor.resources) {
			result->resources.emplace(
				name,
				std::visit(
					[](const auto &input) -> stana::gpu::detail::Resource {
						using T = std::decay_t<decltype(input)>;
						if constexpr (std::is_same_v<T, stana::rendering::GPUBuffer>) {
							return stana::gpu::detail::Buffer{
								.version = input.version(),
								.bytes = stana::rendering::detail::GPUAccess::bytesOwner(input)
							};
						} else if constexpr (std::is_same_v<T, stana::rendering::GPUStorage>) {
							return stana::gpu::detail::Storage{.length = input.length};
						} else if constexpr (std::is_same_v<T, stana::rendering::GPUTexture>) {
							const auto &desc = input.descriptor();
							return stana::gpu::detail::Texture{
								.version = input.version(),
								.width = desc.width,
								.height = desc.height,
								.format = convertFormat(desc.format),
								.bytes = stana::rendering::detail::GPUAccess::bytesOwner(input),
								.encoding = convertEncoding(desc.encoding),
								.premultiplied = desc.premultiplied,
								.sourcePeak = desc.sourcePeak.value()
							};
						} else if constexpr (std::is_same_v<T, stana::rendering::GPUTarget>) {
							return stana::gpu::detail::Target{
								.width = input.width,
								.height = input.height,
								.format = convertFormat(input.format)
							};
						} else if constexpr (std::is_same_v<T, stana::rendering::GPUStateSlot>) {
							const auto &desc = input.descriptor();
							return stana::gpu::detail::State{
								.version = input.version(),
								.width = desc.width,
								.height = desc.height,
								.format = convertFormat(desc.format),
								.initial = stana::rendering::detail::GPUAccess::initialOwner(input),
								.resetKey = desc.resetKey
							};
						} else if constexpr (std::is_same_v<T, stana::rendering::GPUSampler>) {
							switch (input.filter) {
							case stana::rendering::GPUSampler::Filter::nearest:
								return stana::gpu::detail::Sampler{.linear = false};
							case stana::rendering::GPUSampler::Filter::linear:
								return stana::gpu::detail::Sampler{.linear = true};
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
					[](const auto &input) -> stana::gpu::detail::Pass {
						using T = std::decay_t<decltype(input)>;
						if constexpr (std::is_same_v<T, stana::rendering::GPURenderPass>) {
							return stana::gpu::detail::RenderPass{
								.vertex = input.vertex,
								.fragment = input.fragment,
								.target = input.target,
								.vertexCount = input.vertexCount,
								.instanceCount = input.instanceCount,
								.indexBuffer = input.indexBuffer,
								.bindings = convertBindings(input.bindings),
								.blends = input.blends
							};
						} else if constexpr (std::is_same_v<T, stana::rendering::GPUComputePass>) {
							return stana::gpu::detail::ComputePass{
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
		if (programs.size() >= stana::gpu::detail::descriptorLimit) {
			programs.erase(programs.begin());
		}
		programs.insert_or_assign(value.version(), result);
		return result;
	}

	[[nodiscard]] auto mesh(const stana::rendering::GPUMesh &value)
		-> std::shared_ptr<const stana::gpu::detail::Mesh> {
		if (const auto found = meshes.find(value.version()); found != meshes.end()) {
			if (auto retained = found->second.lock()) {
				return retained;
			}
		}
		auto result = std::make_shared<stana::gpu::detail::Mesh>();
		result->version = value.version();
		result->indices = stana::rendering::detail::GPUAccess::indicesOwner(value);
		result->vertices.reserve(value.vertices().size());
		for (const auto &vertex : value.vertices()) {
			const auto &color = vertex.color;
			result->vertices.push_back(
				{.position =
						{static_cast<float>(vertex.position.x.value()),
							static_cast<float>(vertex.position.y.value())},
					.color = {
						static_cast<float>(color.red().value() * color.alpha().value()),
						static_cast<float>(color.green().value() * color.alpha().value()),
						static_cast<float>(color.blue().value() * color.alpha().value()),
						static_cast<float>(color.alpha().value())
					}}
			);
		}
		if (meshes.size() >= stana::gpu::detail::descriptorLimit) {
			meshes.erase(meshes.begin());
		}
		meshes.insert_or_assign(value.version(), result);
		return result;
	}

	[[nodiscard]] static auto
	color(const stana::rendering::detail::GPUColor &value, Environment environment)
		-> stana::gpu::detail::Color {
		return std::visit(
			[&](const auto &input) -> stana::gpu::detail::Color {
				using T = std::decay_t<decltype(input)>;
				if constexpr (std::is_same_v<T, Color>) {
					const auto resolved =
						input.resolved(environment.theme, environment.highContrast);
					return {
						.rgba =
							{resolved.red.value(),
								resolved.green.value(),
								resolved.blue.value(),
								resolved.alpha.value()},
						.linear = false
					};
				} else {
					return {
						.rgba =
							{input.red().value(),
								input.green().value(),
								input.blue().value(),
								input.alpha().value()},
						.linear = true
					};
				}
			},
			value
		);
	}

	[[nodiscard]] static auto rect(Rect bounds) -> stana::gpu::detail::Rect {
		return {
			.origin = {.x = bounds.origin.x.value(), .y = bounds.origin.y.value()},
			.width = bounds.size.width.value(),
			.height = bounds.size.height.value()
		};
	}

	[[nodiscard]] auto drawing(const stana::rendering::GPUDrawing &value, Environment environment)
		-> std::shared_ptr<const stana::gpu::detail::Drawing> {
		const DrawingKey key{
			value.version(),
			environment.theme,
			environment.highContrast,
			environment.direction
		};
		if (const auto found = drawings.find(key); found != drawings.end()) {
			if (auto retained = found->second.lock()) {
				return retained;
			}
		}
		auto result = std::make_shared<stana::gpu::detail::Drawing>();
		result->version = value.version();
		result->extended = value.requiresExtended();
		for (const auto &command : stana::rendering::detail::GPUAccess::commands(value)) {
			result->commands.push_back(
				std::visit(
					[&](const auto &input) -> stana::gpu::detail::Command {
						using T = std::decay_t<decltype(input)>;
						if constexpr (std::is_same_v<T, stana::rendering::detail::GPURect>) {
							return stana::gpu::detail::Shape{
								.bounds = rect(input.bounds),
								.radius = input.radius.value(),
								.color = color(input.color, environment)
							};
						} else if constexpr (std::is_same_v<
												 T,
												 stana::rendering::detail::GPUEllipse>) {
							return stana::gpu::detail::Shape{
								.bounds = rect(input.bounds),
								.color = color(input.color, environment),
								.ellipse = true
							};
						} else if constexpr (std::is_same_v<
												 T,
												 stana::rendering::detail::GPUBoxShadows>) {
							stana::gpu::detail::BoxShadows shadowList;
							for (const auto &shadow : stana::rendering::detail::prepareBoxShadows(
									 input.bounds,
									 input.shadows,
									 input.cornerRadii,
									 input.borderWidths,
									 Color::text(),
									 environment
								 )) {
								shadowList.shadows.push_back(convertShadow(shadow));
							}
							return shadowList;
						} else if constexpr (std::is_same_v<T, stana::rendering::GPUMesh>) {
							return mesh(input);
						} else if constexpr (std::is_same_v<T, stana::rendering::GPUProgram>) {
							return program(input);
						} else if constexpr (std::is_same_v<
												 T,
												 stana::rendering::detail::GPUPolyline>) {
							stana::gpu::detail::Polyline line{
								.width = input.width.value(),
								.color = color(input.color, environment),
								.closed = input.closed
							};
							line.points.reserve(input.points.size());
							for (const auto point : input.points) {
								line.points.push_back({.x = point.x.value(), .y = point.y.value()});
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
} // namespace stana::application::detail
#endif
