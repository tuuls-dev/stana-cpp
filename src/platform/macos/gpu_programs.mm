#include "gpu_programs.hpp"
#include "../../graphics/color.hpp"
#include "../../graphics/limits.hpp"
#import <Foundation/Foundation.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <span>
#include <type_traits>
#include <utility>

namespace stana::native::gpu {
namespace {
[[nodiscard]] auto failure(Diagnostic::Category category, std::string message)
	-> std::unexpected<Failure> {
	return std::unexpected(Failure{.category = category, .message = std::move(message)});
}

[[nodiscard]] auto compilerFailure(const shader_detail::Error &error) -> std::unexpected<Failure> {
	Diagnostic::Category category{};
	switch (error.category) {
	case shader_detail::Error::Category::compiler:
		category = Diagnostic::Category::compiler;
		break;
	case shader_detail::Error::Category::binding:
		category = Diagnostic::Category::binding;
		break;
	case shader_detail::Error::Category::capability:
		category = Diagnostic::Category::capability;
		break;
	}
	return failure(category, error.message);
}

[[nodiscard]] auto resourceBinding(const Binding &binding, const Resource &resource)
	-> shader_detail::Binding {
	return std::visit(
		[&](const auto &value) -> shader_detail::Binding {
			using T = std::decay_t<decltype(value)>;
			shader_detail::Binding result{
				.group = binding.group,
				.binding = binding.binding,
				.writable = binding.write
			};
			if constexpr (std::is_same_v<T, Buffer>) {
				result.kind = shader_detail::Kind::buffer;
				result.byteLength = value.bytes->size();
			} else if constexpr (std::is_same_v<T, Storage>) {
				result.kind = shader_detail::Kind::storage;
				result.byteLength = value.length;
			} else if constexpr (std::is_same_v<T, Texture>) {
				result.kind = shader_detail::Kind::texture;
				result.format = value.format;
			} else if constexpr (std::is_same_v<T, Target>) {
				result.kind = shader_detail::Kind::target;
				result.format = value.format;
			} else if constexpr (std::is_same_v<T, State>) {
				result.kind = shader_detail::Kind::state;
				result.format = value.format;
			} else if constexpr (std::is_same_v<T, Sampler>) {
				result.kind = shader_detail::Kind::sampler;
			} else {
				static_assert(!sizeof(T), "Unhandled GPU resource");
			}
			return result;
		},
		resource
	);
}

[[nodiscard]] auto
bindingsFor(const Program &program, Stage stage, const std::vector<Binding> &bindings)
	-> std::vector<shader_detail::Binding> {
	std::vector<shader_detail::Binding> result;
	result.reserve(bindings.size());
	for (const auto &binding : bindings) {
		if (binding.stage == stage) {
			result.push_back(resourceBinding(binding, program.resources.at(binding.resource)));
		}
	}
	return result;
}

[[nodiscard]] auto request(
	const Program &program,
	const std::string &name,
	Stage stage,
	const std::vector<Binding> &bindings
) -> shader_detail::Request {
	shader_detail::Request result{
		.source = *program.source,
		.entry = name,
		.stage = stage,
		.constants = program.constants
	};
	result.bindings = bindingsFor(program, stage, bindings);
	return result;
}

[[nodiscard]] auto function(id<MTLDevice> device, const shader_detail::Entry &entry)
	-> Result<id<MTLFunction>> {
	const auto library = compileLibrary(device, entry.source);
	if (!library) {
		return std::unexpected(library.error());
	}
	NSString *const name = [[NSString alloc] initWithBytes:entry.function.data()
													length:entry.function.size()
												  encoding:NSUTF8StringEncoding];
	id<MTLFunction> const result = [*library newFunctionWithName:name];
	if (result == nil) {
		return failure(Diagnostic::Category::compiler, "Lowered WGSL entry point is unavailable");
	}
	return result;
}

[[nodiscard]] auto reflected(shader_detail::Entry &entry, NSArray<id<MTLBinding>> *bindings)
	-> Result<void> {
	for (NSUInteger index = 0; index < bindings.count; ++index) {
		id<MTLBinding> const binding = [bindings objectAtIndex:index];
		if (binding.type == MTLBindingTypeBuffer &&
			binding.index == shader_detail::immediateBufferSlot) {
			const auto buffer = static_cast<id<MTLBufferBinding>>(binding);
			if (buffer.bufferDataSize > entry.immediateWords.size() * sizeof(std::uint32_t)) {
				return failure(
					Diagnostic::Category::binding,
					"Compiler helper layout disagrees with native reflection"
				);
			}
			continue;
		}
		const auto found = std::ranges::find_if(entry.bindings, [&](const auto &value) -> bool {
			const auto type = value.kind == shader_detail::SlotKind::buffer
				? MTLBindingTypeBuffer
				: (value.kind == shader_detail::SlotKind::texture ? MTLBindingTypeTexture
																  : MTLBindingTypeSampler);
			return value.slot == binding.index && binding.type == type;
		});
		if (found == entry.bindings.end()) {
			return failure(
				Diagnostic::Category::binding,
				"Native reflection disagrees with logical resource mapping"
			);
		}
		if (found->kind == shader_detail::SlotKind::buffer) {
			const auto buffer = static_cast<id<MTLBufferBinding>>(binding);
			found->minimumBytes =
				std::max(found->minimumBytes, static_cast<std::uint64_t>(buffer.bufferDataSize));
			if (buffer.bufferDataSize > found->logical.byteLength) {
				return failure(
					Diagnostic::Category::binding,
					"WGSL buffer is smaller than its reflected native layout"
				);
			}
		} else if (found->kind == shader_detail::SlotKind::texture) {
			const auto texture = static_cast<id<MTLTextureBinding>>(binding);
			if (texture.textureType != MTLTextureType2D ||
				texture.textureDataType != MTLDataTypeFloat) {
				return failure(
					Diagnostic::Category::binding,
					"Native texture type is outside the portable profile"
				);
			}
		}
		if (binding.access != MTLBindingAccessReadOnly && !found->logical.writable) {
			return failure(
				Diagnostic::Category::binding,
				"Native binding requires undeclared write access"
			);
		}
	}
	return {};
}

void launchNext(const std::shared_ptr<DeviceResources> &device) {
	std::function<void()> next;
	{
		const std::scoped_lock lock{device->mutex};
		if (device->compilerActive || device->waitingCompilers.empty()) {
			return;
		}
		const auto item = device->waitingCompilers.begin();
		next = std::move(item->second);
		device->waitingCompilers.erase(item);
		device->compilerActive = true;
	}
	dispatch_async(device->compilerQueue, ^{
	  @autoreleasepool {
		  next();
		  dispatch_async(dispatch_get_main_queue(), ^{
			{
				const std::scoped_lock lock{device->mutex};
				device->compilerActive = false;
			}
			launchNext(device);
		  });
	  }
	});
}

[[nodiscard]] auto validateReplacement(const Pipeline &pipeline, const Program &program)
	-> Result<void> {
	if (pipeline.passes.size() != program.passes.size()) {
		return failure(Diagnostic::Category::binding, "Pipeline/pass schema mismatch");
	}
	for (std::size_t index = 0; index < pipeline.passes.size(); ++index) {
		const auto checked = std::visit(
			[&](const auto &pass) -> Result<void> {
				using T = std::decay_t<decltype(pass)>;
				if constexpr (std::is_same_v<T, RenderPipeline>) {
					const auto &bindings = std::get<RenderPass>(program.passes.at(index)).bindings;
					for (const auto *entry : {&pass.vertex, &pass.fragment}) {
						const auto input = bindingsFor(program, entry->stage, bindings);
						const auto result = shader_detail::validateBindings(*entry, input);
						if (!result) {
							return compilerFailure(result.error());
						}
					}
				} else {
					const auto input = bindingsFor(
						program,
						Stage::compute,
						std::get<ComputePass>(program.passes.at(index)).bindings
					);
					const auto result = shader_detail::validateBindings(pass.entry, input);
					if (!result) {
						return compilerFailure(result.error());
					}
				}
				return {};
			},
			pipeline.passes.at(index)
		);
		if (!checked) {
			return checked;
		}
	}
	return {};
}
} // namespace

auto compileProgram(const std::shared_ptr<DeviceResources> &device, const Program &program)
	-> Result<std::shared_ptr<const Pipeline>> {
	{
		const std::scoped_lock lock{device->mutex};
		if (const auto found = device->pipelines.find(program.pipelineVersion);
			found != device->pipelines.end()) {
			const auto checked = validateReplacement(*found->second, program);
			if (!checked) {
				return std::unexpected(checked.error());
			}
			std::erase(device->pipelineLRU, program.pipelineVersion);
			device->pipelineLRU.push_back(program.pipelineVersion);
			return found->second;
		}
	}
	auto pipeline = std::make_shared<Pipeline>();
	pipeline->version = program.pipelineVersion;
	for (const auto &pass : program.passes) {
		const auto compiled = std::visit(
			[&](const auto &input) -> Result<std::variant<RenderPipeline, ComputePipeline>> {
				using T = std::decay_t<decltype(input)>;
				if constexpr (std::is_same_v<T, RenderPass>) {
					auto vertex = shader_detail::compile(
						request(program, input.vertex, Stage::vertex, input.bindings)
					);
					if (!vertex) {
						return compilerFailure(vertex.error());
					}
					auto fragment = shader_detail::compile(
						request(program, input.fragment, Stage::fragment, input.bindings)
					);
					if (!fragment) {
						return compilerFailure(fragment.error());
					}
					const auto vs = function(device->device, *vertex);
					if (!vs) {
						return std::unexpected(vs.error());
					}
					const auto fs = function(device->device, *fragment);
					if (!fs) {
						return std::unexpected(fs.error());
					}
					MTLRenderPipelineDescriptor *const descriptor =
						[[MTLRenderPipelineDescriptor alloc] init];
					descriptor.vertexFunction = *vs;
					descriptor.fragmentFunction = *fs;
					const auto format = std::visit(
						[](const auto &resource) -> Format {
							using R = std::decay_t<decltype(resource)>;
							if constexpr (std::is_same_v<R, Target> || std::is_same_v<R, State>) {
								return resource.format;
							} else {
								throw std::logic_error("Invalid validated render attachment");
							}
						},
						program.resources.at(input.target)
					);
					MTLRenderPipelineColorAttachmentDescriptor *const color =
						[descriptor.colorAttachments objectAtIndexedSubscript:0];
					color.pixelFormat = pixelFormat(format);
					color.blendingEnabled = input.blends;
					color.sourceRGBBlendFactor = MTLBlendFactorOne;
					color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
					color.sourceAlphaBlendFactor = MTLBlendFactorOne;
					color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
					NSError *error = nil;
					MTLRenderPipelineReflection *reflection = nil;
					id<MTLRenderPipelineState> const state = [device->device
						newRenderPipelineStateWithDescriptor:descriptor
													 options:MTLPipelineOptionBindingInfo |
						MTLPipelineOptionBufferTypeInfo
												  reflection:&reflection
													   error:&error];
					if (state == nil) {
						return failure(
							Diagnostic::Category::compiler,
							errorMessage(error, "Render pipeline creation failed")
						);
					}
					const auto vertexChecked = reflected(*vertex, reflection.vertexBindings);
					if (!vertexChecked) {
						return std::unexpected(vertexChecked.error());
					}
					const auto fragmentChecked = reflected(*fragment, reflection.fragmentBindings);
					if (!fragmentChecked) {
						return std::unexpected(fragmentChecked.error());
					}
					return RenderPipeline{
						.vertex = std::move(*vertex),
						.fragment = std::move(*fragment),
						.state = state
					};
				} else {
					auto entry = shader_detail::compile(
						request(program, input.function, Stage::compute, input.bindings)
					);
					if (!entry) {
						return compilerFailure(entry.error());
					}
					const auto shader = function(device->device, *entry);
					if (!shader) {
						return std::unexpected(shader.error());
					}
					NSError *error = nil;
					MTLComputePipelineReflection *reflection = nil;
					id<MTLComputePipelineState> const state = [device->device
						newComputePipelineStateWithFunction:*shader
													options:MTLPipelineOptionBindingInfo |
						MTLPipelineOptionBufferTypeInfo
												 reflection:&reflection
													  error:&error];
					if (state == nil) {
						return failure(
							Diagnostic::Category::compiler,
							errorMessage(error, "Compute pipeline creation failed")
						);
					}
					const auto checked = reflected(*entry, reflection.bindings);
					if (!checked) {
						return std::unexpected(checked.error());
					}
					return ComputePipeline{.entry = std::move(*entry), .state = state};
				}
			},
			pass
		);
		if (!compiled) {
			return std::unexpected(compiled.error());
		}
		pipeline->passes.push_back(*compiled);
	}
	{
		const std::scoped_lock lock{device->mutex};
		constexpr std::size_t pipelineLimit = 128;
		if (device->pipelines.size() >= pipelineLimit) {
			device->pipelines.erase(device->pipelineLRU.front());
			device->pipelineLRU.erase(device->pipelineLRU.begin());
		}
		device->pipelineLRU.push_back(pipeline->version);
		device->pipelines.insert_or_assign(pipeline->version, pipeline);
	}
	return pipeline;
}

void requestCompilation(
	const std::shared_ptr<DeviceResources> &device,
	std::uint64_t window,
	std::shared_ptr<const Program> program,
	std::function<void(Result<std::shared_ptr<const Pipeline>>, bool)> callback
) {
	{
		const std::scoped_lock lock{device->mutex};
		constexpr std::size_t waitingLimit = 16;
		if (device->waitingCompilers.size() >= waitingLimit &&
			!device->waitingCompilers.contains(window)) {
			dispatch_async(dispatch_get_main_queue(), ^{
			  callback(
				  failure(Diagnostic::Category::capability, "Compiler admission capacity exceeded"),
				  false
			  );
			});
			return;
		}
		device->waitingCompilers.insert_or_assign(
			window,
			[device, jobProgram = std::move(program), completion = std::move(callback)] -> void {
				try {
					bool cached = false;
					{
						const std::scoped_lock cacheLock{device->mutex};
						cached = device->pipelines.contains(jobProgram->pipelineVersion);
					}
					const auto result = compileProgram(device, *jobProgram);
					dispatch_async(dispatch_get_main_queue(), ^{
					  completion(result, !cached);
					});
				} catch (const std::exception &error) {
					const auto message = std::string{error.what()};
					dispatch_async(dispatch_get_main_queue(), ^{
					  completion(failure(Diagnostic::Category::compiler, message), false);
					});
				}
			}
		);
	}
	launchNext(device);
}

void cancelCompilation(const std::shared_ptr<DeviceResources> &device, std::uint64_t window) {
	const std::scoped_lock lock{device->mutex};
	device->waitingCompilers.erase(window);
}
} // namespace stana::native::gpu

namespace stana::native::gpu {
namespace {
inline constexpr std::size_t transferAlignment = 256;
inline constexpr double byteMaximum = 255;

struct ImageInput {
	Bytes bytes{};
	Format format = Format::rgba16;
};

[[nodiscard]] auto ingress(const Texture &texture, Encoding destination) -> Result<ImageInput> {
	if (texture.format == Format::r8 ||
		(texture.encoding == destination && texture.premultiplied)) {
		return ImageInput{.bytes = *texture.bytes, .format = texture.format};
	}
	ImageInput result{.format = destination == Encoding::linear ? Format::rgba16 : Format::bgra8};
	const auto pixelCount = static_cast<std::size_t>(texture.width) * texture.height;
	const auto stride = pixelBytes(result.format);
	if (pixelCount > graphics_detail::payloadLimit / stride) {
		return failure(
			Diagnostic::Category::capability,
			"Converted texture exceeds the canvas payload profile"
		);
	}
	result.bytes.reserve(pixelCount * stride);
	const auto input = std::span<const std::byte>{*texture.bytes};
	for (std::size_t index = 0; index < pixelCount; ++index) {
		std::array<double, 4> color{};
		if (texture.format == Format::bgra8) {
			const auto offset = index * 4;
			color = {
				std::to_integer<unsigned>(graphics_detail::byteAt(input, offset + 2)) / byteMaximum,
				std::to_integer<unsigned>(graphics_detail::byteAt(input, offset + 1)) / byteMaximum,
				std::to_integer<unsigned>(graphics_detail::byteAt(input, offset)) / byteMaximum,
				std::to_integer<unsigned>(graphics_detail::byteAt(input, offset + 3)) / byteMaximum
			};
		} else {
			constexpr std::size_t halfPixelStride = 8;
			for (std::size_t channel = 0; channel < color.size(); ++channel) {
				color.at(channel) =
					graphics_detail::halfAt(input, index * halfPixelStride + channel * 2);
			}
		}
		const double alpha = color.back();
		for (std::size_t channel = 0; channel < 3; ++channel) {
			double value = alpha > 0
				? (texture.premultiplied ? color.at(channel) / alpha : color.at(channel))
				: 0;
			if (texture.encoding != destination) {
				value = destination == Encoding::linear ? graphics_detail::decodeSRGB(value)
														: graphics_detail::encodeSRGB(value);
			}
			color.at(channel) = value * alpha;
		}
		if (result.format == Format::rgba16) {
			for (const auto value : color) {
				const auto half = graphics_detail::toHalf(value);
				if (!half) {
					return failure(
						Diagnostic::Category::capability,
						"Texture transfer exceeds finite half-float working range"
					);
				}
				constexpr unsigned lowByteMask = 255;
				result.bytes.push_back(static_cast<std::byte>(*half & lowByteMask));
				result.bytes.push_back(static_cast<std::byte>(*half >> graphics_detail::byteBits));
			}
		} else {
			for (const auto channel : {2U, 1U, 0U, 3U}) {
				result.bytes.push_back(
					static_cast<std::byte>(static_cast<unsigned>(
						std::round(std::clamp(color.at(channel), 0.0, 1.0) * byteMaximum)
					))
				);
			}
		}
	}
	return result;
}

[[nodiscard]] auto clear(id<MTLCommandBuffer> commands, id<MTLTexture> texture) -> Result<void> {
	MTLRenderPassDescriptor *const descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
	MTLRenderPassColorAttachmentDescriptor *const color =
		[descriptor.colorAttachments objectAtIndexedSubscript:0];
	color.texture = texture;
	color.loadAction = MTLLoadActionClear;
	color.storeAction = MTLStoreActionStore;
	color.clearColor = MTLClearColorMake(0, 0, 0, 0);
	id<MTLRenderCommandEncoder> const encoder =
		[commands renderCommandEncoderWithDescriptor:descriptor];
	if (encoder == nil) {
		return failure(Diagnostic::Category::execution, "Cannot initialize transient texture");
	}
	[encoder endEncoding];
	return {};
}

[[nodiscard]] auto upload(
	Resources &resources,
	id<MTLCommandBuffer> commands,
	const std::shared_ptr<TextureOwner> &texture,
	std::span<const std::byte> bytes,
	const std::shared_ptr<AllocationLease> &lease,
	ProgramCandidate &candidate
) -> Result<void> {
	if (bytes.empty()) {
		return clear(commands, texture->texture);
	}
	const auto width = texture->texture.width;
	const auto height = texture->texture.height;
	const auto bpp = texture->texture.pixelFormat == MTLPixelFormatRGBA16Float
		? pixelBytes(Format::rgba16)
		: (texture->texture.pixelFormat == MTLPixelFormatR8Unorm ? 1 : 4);
	const auto row = width * bpp;
	const auto pitch = ((row + transferAlignment - 1) / transferAlignment) * transferAlignment;
	if (pitch > graphics_detail::payloadLimit / height) {
		return failure(Diagnostic::Category::capability, "Texture staging exceeds payload budget");
	}
	Bytes padded(pitch * height);
	for (std::size_t y = 0; y < height; ++y) {
		std::ranges::copy(
			bytes.subspan(y * row, row),
			padded.begin() + static_cast<std::ptrdiff_t>(y * pitch)
		);
	}
	auto buffer = resources.buffer(padded.size(), padded, lease);
	if (!buffer) {
		return std::unexpected(buffer.error());
	}
	id<MTLBlitCommandEncoder> const encoder = [commands blitCommandEncoder];
	if (encoder == nil) {
		return failure(Diagnostic::Category::execution, "Cannot encode texture upload");
	}
	[encoder copyFromBuffer:(*buffer)->buffer
			   sourceOffset:0
		  sourceBytesPerRow:pitch
		sourceBytesPerImage:padded.size()
				 sourceSize:MTLSizeMake(width, height, 1)
				  toTexture:texture->texture
		   destinationSlice:0
		   destinationLevel:0
		  destinationOrigin:MTLOriginMake(0, 0, 0)];
	[encoder endEncoding];
	candidate.retainedBuffers.push_back(std::move(*buffer));
	return {};
}

[[nodiscard]] auto
textureExtent(const Resource &resource, std::uint32_t width, std::uint32_t height)
	-> std::array<std::uint32_t, 2> {
	return std::visit(
		[&](const auto &value) -> std::array<std::uint32_t, 2> {
			using T = std::decay_t<decltype(value)>;
			if constexpr (std::is_same_v<T, Target>) {
				return {
					value.width == 0 ? width : value.width,
					value.height == 0 ? height : value.height
				};
			} else if constexpr (std::is_same_v<T, Texture> || std::is_same_v<T, State>) {
				return {value.width, value.height};
			} else {
				return {};
			}
		},
		resource
	);
}

struct BindingEncoder {
	id<MTLRenderCommandEncoder> render = nil;
	id<MTLComputeCommandEncoder> compute = nil;
	Stage stage = Stage::compute;

	void buffer(id<MTLBuffer> value, std::size_t index) const {
		if (stage == Stage::vertex) {
			[render setVertexBuffer:value offset:0 atIndex:index];
		} else if (stage == Stage::fragment) {
			[render setFragmentBuffer:value offset:0 atIndex:index];
		} else {
			[compute setBuffer:value offset:0 atIndex:index];
		}
	}

	void texture(id<MTLTexture> value, std::size_t index) const {
		if (stage == Stage::vertex) {
			[render setVertexTexture:value atIndex:index];
		} else if (stage == Stage::fragment) {
			[render setFragmentTexture:value atIndex:index];
		} else {
			[compute setTexture:value atIndex:index];
		}
	}

	void sampler(id<MTLSamplerState> value, std::size_t index) const {
		if (stage == Stage::vertex) {
			[render setVertexSamplerState:value atIndex:index];
		} else if (stage == Stage::fragment) {
			[render setFragmentSamplerState:value atIndex:index];
		} else {
			[compute setSamplerState:value atIndex:index];
		}
	}

	void immediates(const std::vector<std::uint32_t> &words) const {
		const auto length = words.size() * sizeof(std::uint32_t);
		if (stage == Stage::vertex) {
			[render setVertexBytes:words.data()
							length:length
						   atIndex:shader_detail::immediateBufferSlot];
		} else if (stage == Stage::fragment) {
			[render setFragmentBytes:words.data()
							  length:length
							 atIndex:shader_detail::immediateBufferSlot];
		} else {
			[compute setBytes:words.data()
					   length:length
					  atIndex:shader_detail::immediateBufferSlot];
		}
	}
};

[[nodiscard]] auto bind(
	const shader_detail::Entry &entry,
	const std::vector<Binding> &bindings,
	const ProgramBundle &bundle,
	const std::map<std::string, std::shared_ptr<TextureOwner>, std::less<>> &previous,
	const BindingEncoder &encoder
) -> Result<void> {
	auto words = entry.immediateWords;
	for (const auto &required : entry.bindings) {
		const auto found = std::ranges::find_if(bindings, [&](const auto &value) -> bool {
			return value.stage == entry.stage && value.group == required.logical.group &&
				value.binding == required.logical.binding;
		});
		if (found == bindings.end()) {
			return failure(Diagnostic::Category::binding, "Missing validated logical binding");
		}
		switch (required.kind) {
		case shader_detail::SlotKind::buffer: {
			const auto resource = bundle.buffers.find(found->resource);
			if (resource == bundle.buffers.end() ||
				resource->second->bytes < required.minimumBytes) {
				return failure(Diagnostic::Category::binding, "Undersized runtime buffer");
			}
			encoder.buffer(resource->second->buffer, required.slot);
			if (required.sizeWord) {
				words.at(*required.sizeWord) = static_cast<std::uint32_t>(resource->second->bytes);
			}
			break;
		}
		case shader_detail::SlotKind::texture: {
			const auto &textures = found->previous ? previous : bundle.textures;
			const auto resource = textures.find(found->resource);
			if (resource == textures.end()) {
				return failure(Diagnostic::Category::binding, "Missing runtime texture");
			}
			encoder.texture(resource->second->texture, required.slot);
			break;
		}
		case shader_detail::SlotKind::sampler:
			encoder.sampler(bundle.samplers.at(found->resource), required.slot);
			break;
		}
	}
	encoder.immediates(words);
	return {};
}
} // namespace

auto encodeProgram(
	Resources &resources,
	id<MTLCommandBuffer> commands,
	std::shared_ptr<const Program> program,
	std::shared_ptr<const Pipeline> pipeline,
	std::uint32_t width,
	std::uint32_t height,
	const std::shared_ptr<const ProgramBundle> &previous
) -> Result<ProgramCandidate> {
	if (width == 0 || height == 0) {
		return failure(
			Diagnostic::Category::capability,
			"Zero backing extent suspends program execution"
		);
	}
	const auto validated = validateReplacement(*pipeline, *program);
	if (!validated) {
		return std::unexpected(validated.error());
	}
	const bool sameState = previous && previous->source->stateVersion == program->stateVersion &&
		previous->width == width && previous->height == height;
	std::set<std::string> writers;
	for (const auto &pass : program->passes) {
		std::visit(
			[&](const auto &value) -> void {
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, RenderPass>) {
					writers.insert(value.target);
				}
				for (const auto &binding : value.bindings) {
					if (binding.write) {
						writers.insert(binding.resource);
					}
				}
			},
			pass
		);
	}
	const auto reusable = [&](const std::string &name, const Resource &resource) -> bool {
		if (!previous) {
			return false;
		}
		const auto old = previous->source->resources.find(name);
		if (old == previous->source->resources.end() || old->second.index() != resource.index()) {
			return false;
		}
		if (const auto *buffer = std::get_if<Buffer>(&resource)) {
			return previous->buffers.contains(name) &&
				buffer->version == std::get<Buffer>(old->second).version;
		}
		if (const auto *texture = std::get_if<Texture>(&resource)) {
			return previous->textures.contains(name) &&
				previous->source->outputColor == program->outputColor &&
				texture->version == std::get<Texture>(old->second).version;
		}
		return false;
	};

	std::map<std::string, ImageInput, std::less<>> inputs;
	std::size_t total = 0;
	const auto add = [&](std::size_t bytes) -> bool {
		if (bytes > windowAllocationLimit - total) {
			return false;
		}
		total += bytes;
		return true;
	};
	for (const auto &[name, resource] : program->resources) {
		if (reusable(name, resource)) {
			continue;
		}
		const auto cost = std::visit(
			[&](const auto &value) -> Result<std::size_t> {
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, Buffer>) {
					return std::max(std::size_t{1}, value.bytes->size());
				} else if constexpr (std::is_same_v<T, Storage>) {
					return value.length;
				} else if constexpr (std::is_same_v<T, Sampler>) {
					return 0;
				} else {
					const auto extent = textureExtent(resource, width, height);
					const auto input = [&]() -> Result<std::pair<Format, std::size_t>> {
						if constexpr (std::is_same_v<T, Texture>) {
							auto converted = ingress(value, program->outputColor);
							if (!converted) {
								return std::unexpected(converted.error());
							}
							const auto result =
								std::pair{converted->format, converted->bytes.size()};
							inputs.emplace(name, std::move(*converted));
							return result;
						} else if constexpr (std::is_same_v<T, State>) {
							return std::pair{
								value.format,
								sameState ? std::size_t{0} : value.initial->size()
							};
						} else {
							return std::pair{value.format, std::size_t{0}};
						}
					}();
					if (!input) {
						return std::unexpected(input.error());
					}
					const auto [format, uploadSize] = *input;
					const auto bytes = graphics_detail::textureBytes(
						extent.at(0),
						extent.at(1),
						pixelBytes(format)
					);
					if (!bytes) {
						return failure(Diagnostic::Category::capability, bytes.error());
					}
					const auto count = [&]() -> std::size_t {
						if constexpr (std::is_same_v<T, State>) {
							return sameState ? (writers.contains(name) ? 1 : 0)
											 : (writers.contains(name) ? 2 : 1);
						} else {
							return 1;
						}
					}();
					if (*bytes > windowAllocationLimit / std::max(std::size_t{1}, count)) {
						return failure(
							Diagnostic::Category::capability,
							"State pair exceeds allocation budget"
						);
					}
					const auto row = static_cast<std::size_t>(extent.at(0)) * pixelBytes(format);
					const auto pitch =
						((row + transferAlignment - 1) / transferAlignment) * transferAlignment;
					if (uploadSize != 0 && pitch > windowAllocationLimit / extent.at(1)) {
						return failure(
							Diagnostic::Category::capability,
							"Staging allocation exceeds budget"
						);
					}
					const auto staging = uploadSize == 0 ? 0 : pitch * extent.at(1);
					if (staging > windowAllocationLimit - *bytes * count) {
						return failure(
							Diagnostic::Category::capability,
							"Peak texture demand exceeds budget"
						);
					}
					return *bytes * count + staging;
				}
			},
			resource
		);
		if (!cost) {
			return std::unexpected(cost.error());
		}
		if (!add(*cost)) {
			return failure(
				Diagnostic::Category::capability,
				"Complete program candidate exceeds allocation budget"
			);
		}
	}
	auto reservation = resources.reserve(total);
	if (!reservation) {
		return std::unexpected(reservation.error());
	}
	const auto lifetime = std::make_shared<ProgramCandidate>();
	lifetime->next = std::make_shared<ProgramBundle>();
	[commands addCompletedHandler:^(id<MTLCommandBuffer> completed) {
	  static_cast<void>(completed);
	  static_cast<void>(lifetime);
	}];
	auto &candidate = *lifetime;
	auto &bundle = *candidate.next;
	bundle.source = program;
	bundle.pipeline = pipeline;
	bundle.width = width;
	bundle.height = height;
	std::map<std::string, std::shared_ptr<TextureOwner>, std::less<>> oldState;
	for (const auto &[name, resource] : program->resources) {
		if (reusable(name, resource)) {
			if (std::holds_alternative<Buffer>(resource)) {
				bundle.buffers.emplace(name, previous->buffers.at(name));
			} else {
				bundle.textures.emplace(name, previous->textures.at(name));
			}
			continue;
		}
		const auto created = std::visit(
			[&](const auto &value) -> Result<void> {
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, Buffer> || std::is_same_v<T, Storage>) {
					const auto input = [&]() -> std::span<const std::byte> {
						if constexpr (std::is_same_v<T, Buffer>) {
							return *value.bytes;
						} else {
							return {};
						}
					}();
					const auto length = [&]() -> std::size_t {
						if constexpr (std::is_same_v<T, Buffer>) {
							return std::max(std::size_t{1}, value.bytes->size());
						} else {
							return value.length;
						}
					}();
					auto buffer = resources.buffer(length, input, *reservation);
					if (!buffer) {
						return std::unexpected(buffer.error());
					}
					bundle.buffers.emplace(name, std::move(*buffer));
				} else if constexpr (std::is_same_v<T, Sampler>) {
					MTLSamplerDescriptor *const descriptor = [[MTLSamplerDescriptor alloc] init];
					descriptor.minFilter =
						value.linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
					descriptor.magFilter = descriptor.minFilter;
					descriptor.sAddressMode = MTLSamplerAddressModeClampToZero;
					descriptor.tAddressMode = MTLSamplerAddressModeClampToZero;
					id<MTLSamplerState> const sampler =
						[resources.device->device newSamplerStateWithDescriptor:descriptor];
					if (sampler == nil) {
						return failure(
							Diagnostic::Category::capability,
							"Sampler allocation failed"
						);
					}
					bundle.samplers.emplace(name, sampler);
				} else {
					const auto extent = textureExtent(resource, width, height);
					const auto format = [&]() -> Format {
						if constexpr (std::is_same_v<T, Texture>) {
							return inputs.at(name).format;
						} else {
							return value.format;
						}
					}();
					const auto usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite |
						MTLTextureUsageRenderTarget;
					std::shared_ptr<TextureOwner> current;
					if constexpr (std::is_same_v<T, State>) {
						std::shared_ptr<TextureOwner> old;
						if (sameState && previous->states.contains(name)) {
							old = previous->states.at(name);
						} else {
							auto texture = resources.texture(
								extent.at(0),
								extent.at(1),
								format,
								usage,
								*reservation
							);
							if (!texture) {
								return std::unexpected(texture.error());
							}
							old = std::move(*texture);
							const auto initialized = upload(
								resources,
								commands,
								old,
								*value.initial,
								*reservation,
								candidate
							);
							if (!initialized) {
								return initialized;
							}
						}
						oldState.emplace(name, old);
						candidate.retainedTextures.push_back(old);
						if (!writers.contains(name)) {
							current = old;
						}
					}
					if (!current) {
						auto texture = resources.texture(
							extent.at(0),
							extent.at(1),
							format,
							usage,
							*reservation
						);
						if (!texture) {
							return std::unexpected(texture.error());
						}
						current = std::move(*texture);
						const auto initialized = [&]() -> Result<void> {
							if constexpr (std::is_same_v<T, Texture>) {
								return upload(
									resources,
									commands,
									current,
									inputs.at(name).bytes,
									*reservation,
									candidate
								);
							} else {
								return clear(commands, current->texture);
							}
						}();
						if (!initialized) {
							return initialized;
						}
					}
					bundle.textures.emplace(name, current);
					if constexpr (std::is_same_v<T, State>) {
						bundle.states.emplace(name, current);
					}
				}
				return {};
			},
			resource
		);
		if (!created) {
			return std::unexpected(created.error());
		}
	}
	for (const auto index : program->order) {
		const auto encoded = std::visit(
			[&](const auto &pass) -> Result<void> {
				using T = std::decay_t<decltype(pass)>;
				if constexpr (std::is_same_v<T, RenderPass>) {
					const auto &compiled = std::get<RenderPipeline>(pipeline->passes.at(index));
					MTLRenderPassDescriptor *const descriptor =
						[MTLRenderPassDescriptor renderPassDescriptor];
					MTLRenderPassColorAttachmentDescriptor *const color =
						[descriptor.colorAttachments objectAtIndexedSubscript:0];
					color.texture = bundle.textures.at(pass.target)->texture;
					color.loadAction = MTLLoadActionLoad;
					color.storeAction = MTLStoreActionStore;
					id<MTLRenderCommandEncoder> const encoder =
						[commands renderCommandEncoderWithDescriptor:descriptor];
					if (encoder == nil) {
						return failure(
							Diagnostic::Category::execution,
							"Render encoder allocation failed"
						);
					}
					[encoder setRenderPipelineState:compiled.state];
					[encoder setCullMode:MTLCullModeNone];
					const auto vertex = bind(
						compiled.vertex,
						pass.bindings,
						bundle,
						oldState,
						{.render = encoder, .stage = Stage::vertex}
					);
					const auto fragment = bind(
						compiled.fragment,
						pass.bindings,
						bundle,
						oldState,
						{.render = encoder, .stage = Stage::fragment}
					);
					if (!vertex || !fragment) {
						[encoder endEncoding];
						return !vertex ? vertex : fragment;
					}
					if (pass.indexBuffer) {
						[encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
											indexCount:pass.vertexCount
											 indexType:MTLIndexTypeUInt32
										   indexBuffer:bundle.buffers.at(*pass.indexBuffer)->buffer
									 indexBufferOffset:0
										 instanceCount:pass.instanceCount];
					} else {
						[encoder drawPrimitives:MTLPrimitiveTypeTriangle
									vertexStart:0
									vertexCount:pass.vertexCount
								  instanceCount:pass.instanceCount];
					}
					[encoder endEncoding];
				} else {
					const auto &compiled = std::get<ComputePipeline>(pipeline->passes.at(index));
					const auto &size = compiled.entry.workgroupSize;
					const auto maximum = resources.device->device.maxThreadsPerThreadgroup;
					std::uint64_t invocations = 1;
					for (const auto axis : size) {
						if (axis == 0 ||
							axis > compiled.state.maxTotalThreadsPerThreadgroup / invocations) {
							return failure(
								Diagnostic::Category::capability,
								"Workgroup exceeds native device thread limit"
							);
						}
						invocations *= axis;
					}
					if (size.at(0) > maximum.width || size.at(1) > maximum.height ||
						size.at(2) > maximum.depth ||
						compiled.entry.workgroupBytes >
							resources.device->device.maxThreadgroupMemoryLength) {
						return failure(
							Diagnostic::Category::capability,
							"Workgroup exceeds native device dimensions/memory"
						);
					}
					for (const auto axis : pass.workgroupCount) {
						if (axis == 0 || axis > graphics_detail::invocationLimit / invocations) {
							return failure(
								Diagnostic::Category::capability,
								"Dispatch invocation product exceeds portable limit"
							);
						}
						invocations *= axis;
					}
					id<MTLComputeCommandEncoder> const encoder = [commands computeCommandEncoder];
					if (encoder == nil) {
						return failure(
							Diagnostic::Category::execution,
							"Compute encoder allocation failed"
						);
					}
					[encoder setComputePipelineState:compiled.state];
					const auto bound =
						bind(compiled.entry, pass.bindings, bundle, oldState, {.compute = encoder});
					if (!bound) {
						[encoder endEncoding];
						return bound;
					}
					for (std::size_t slot = 0; slot < compiled.entry.dynamicWorkgroupBytes.size();
						++slot) {
						[encoder
							setThreadgroupMemoryLength:compiled.entry.dynamicWorkgroupBytes.at(slot)
											   atIndex:slot];
					}
					[encoder dispatchThreadgroups:MTLSizeMake(
													  pass.workgroupCount.at(0),
													  pass.workgroupCount.at(1),
													  pass.workgroupCount.at(2)
												  )
							threadsPerThreadgroup:MTLSizeMake(size.at(0), size.at(1), size.at(2))];
					[encoder endEncoding];
				}
				return {};
			},
			program->passes.at(index)
		);
		if (!encoded) {
			return std::unexpected(encoded.error());
		}
	}
	bundle.output = bundle.textures.at(program->output);
	return candidate;
}
} // namespace stana::native::gpu
