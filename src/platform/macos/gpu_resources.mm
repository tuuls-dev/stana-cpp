#include "gpu_resources.hpp"
#include "../../rendering/limits.hpp"
#include "stana_shader.hpp"
#import <Foundation/Foundation.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace stana::platform::detail::gpu {
namespace {
[[nodiscard]] auto capability(std::string message) -> std::unexpected<Failure> {
	return std::unexpected(
		Failure{.category = Diagnostic::Category::capability, .message = std::move(message)}
	);
}
} // namespace

auto pixelFormat(Format format) -> MTLPixelFormat {
	switch (format) {
	case Format::bgra8:
		return MTLPixelFormatBGRA8Unorm;
	case Format::rgba16:
		return MTLPixelFormatRGBA16Float;
	case Format::r8:
		return MTLPixelFormatR8Unorm;
	}
	throw std::logic_error("Unknown GPU format");
}

auto pixelBytes(Format format) -> std::size_t {
	switch (format) {
	case Format::bgra8:
		return 4;
	case Format::rgba16: {
		constexpr std::size_t halfPixelBytes = 8;
		return halfPixelBytes;
	}
	case Format::r8:
		return 1;
	}
	throw std::logic_error("Unknown GPU format");
}

auto errorMessage(NSError *error, const std::string &fallback) -> std::string {
	const char *const text = error.localizedDescription.UTF8String;
	return text != nullptr ? std::string{text} : fallback;
}

auto compileLibrary(id<MTLDevice> device, const std::string &source) -> Result<id<MTLLibrary>> {
	NSString *const text = [[NSString alloc] initWithBytes:source.data()
													length:source.size()
												  encoding:NSUTF8StringEncoding];
	if (text == nil) {
		return std::unexpected(
			Failure{
				.category = Diagnostic::Category::compiler,
				.message = "Shader source is not UTF-8"
			}
		);
	}
	MTLCompileOptions *const options = [[MTLCompileOptions alloc] init];
	options.mathMode = MTLMathModeSafe;
	NSError *error = nil;
	id<MTLLibrary> const library = [device newLibraryWithSource:text options:options error:&error];
	if (library == nil) {
		return std::unexpected(
			Failure{
				.category = Diagnostic::Category::compiler,
				.message = errorMessage(error, "Native shader compilation failed")
			}
		);
	}
	return library;
}

auto DeviceResources::get(id<MTLDevice> device) -> Result<std::shared_ptr<DeviceResources>> {
	static std::map<std::uint64_t, std::weak_ptr<DeviceResources>> devices;
	if (device == nil) {
		return capability("No GPU device is available");
	}
	if (const auto found = devices.find(device.registryID); found != devices.end()) {
		if (auto owner = found->second.lock()) {
			return owner;
		}
	}
	auto result = std::make_shared<DeviceResources>();
	result->device = device;
	result->queue = [device newCommandQueue];
	result->budget->limit = deviceAllocationLimit;
	result->compilerQueue =
		dispatch_queue_create("dev.stana.canvas.compiler", DISPATCH_QUEUE_SERIAL);
	const auto library = compileLibrary(device, std::string{builtinShader});
	if (!library) {
		return std::unexpected(library.error());
	}
	if (result->queue == nil || result->compilerQueue == nullptr) {
		return capability("Unable to create GPU queues");
	}
	result->builtins = *library;
	devices.insert_or_assign(device.registryID, result);
	return result;
}

auto Resources::reserve(std::size_t bytes) const -> Result<std::shared_ptr<AllocationLease>> {
	auto reserved = AllocationLease::reserve(device->budget, budget, bytes);
	if (!reserved) {
		return capability(reserved.error());
	}
	return std::move(*reserved);
}

auto Resources::buffer(
	std::size_t length,
	std::span<const std::byte> input,
	std::shared_ptr<AllocationLease> lease
) -> Result<std::shared_ptr<BufferOwner>> {
	if (length == 0 || length > stana::rendering::detail::payloadLimit ||
		length > device->device.maxBufferLength || input.size() > length) {
		return capability("GPU buffer length exceeds device/profile limits");
	}
	if (!lease) {
		auto reservation = reserve(length);
		if (!reservation) {
			return std::unexpected(reservation.error());
		}
		lease = std::move(*reservation);
	}
	const auto mode = device->device.hasUnifiedMemory ? MTLResourceStorageModeShared
													  : MTLResourceStorageModeManaged;
	std::vector<std::byte> initialized(length);
	std::ranges::copy(input, initialized.begin());
	id<MTLBuffer> const buffer = [device->device newBufferWithBytes:initialized.data()
															 length:length
															options:mode];
	if (buffer == nil || buffer.contents == nullptr) {
		return capability("GPU buffer allocation failed");
	}
	if (!device->device.hasUnifiedMemory) {
		[buffer didModifyRange:NSMakeRange(0, length)];
	}
	uploadBytes += input.size();
	return std::make_shared<BufferOwner>(
		BufferOwner{.buffer = buffer, .lease = lease->split(length), .bytes = length}
	);
}

auto Resources::texture(
	std::uint32_t width,
	std::uint32_t height,
	Format format,
	MTLTextureUsage usage,
	std::shared_ptr<AllocationLease> lease,
	std::uint32_t samples
) -> Result<std::shared_ptr<TextureOwner>> {
	const auto pixels = stana::rendering::detail::textureBytes(width, height, pixelBytes(format));
	if (!pixels) {
		return capability(pixels.error());
	}
	if ((samples != 1 && samples != 4) || ![device->device supportsTextureSampleCount:samples] ||
		*pixels > stana::rendering::detail::payloadLimit / samples) {
		return capability("Unsupported multisample allocation profile");
	}
	const auto length = *pixels * samples;
	if (!lease) {
		auto reserved = reserve(length);
		if (!reserved) {
			return std::unexpected(reserved.error());
		}
		lease = std::move(*reserved);
	}
	MTLTextureDescriptor *const descriptor =
		[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat(format)
														   width:width
														  height:height
													   mipmapped:NO];
	descriptor.storageMode = MTLStorageModePrivate;
	descriptor.sampleCount = samples;
	if (samples > 1) {
		descriptor.textureType = MTLTextureType2DMultisample;
	}
	descriptor.usage = usage;
	id<MTLTexture> const texture = [device->device newTextureWithDescriptor:descriptor];
	if (texture == nil) {
		return capability("GPU texture allocation/usage is unsupported");
	}
	return std::make_shared<TextureOwner>(
		TextureOwner{.texture = texture, .lease = lease->split(length), .bytes = length}
	);
}

auto Resources::mesh(const std::shared_ptr<const Mesh> &mesh)
	-> Result<std::shared_ptr<MeshUpload>> {
	{
		const std::scoped_lock lock{device->mutex};
		if (const auto found = device->meshes.find(mesh->version); found != device->meshes.end()) {
			if (auto retained = found->second.lock()) {
				return retained;
			}
		}
	}
	if (mesh->vertices.empty() || mesh->indices->empty()) {
		return std::make_shared<MeshUpload>();
	}
	const auto vertexSpan = std::as_bytes(std::span{mesh->vertices});
	const auto indexSpan = std::as_bytes(std::span{*mesh->indices});
	if (vertexSpan.size() > stana::rendering::detail::payloadLimit - indexSpan.size()) {
		return capability("Mesh upload exceeds payload limit");
	}
	auto reservation = reserve(vertexSpan.size() + indexSpan.size());
	if (!reservation) {
		return std::unexpected(reservation.error());
	}
	auto vertices = buffer(vertexSpan.size(), vertexSpan, *reservation);
	if (!vertices) {
		return std::unexpected(vertices.error());
	}
	auto indices = buffer(indexSpan.size(), indexSpan, *reservation);
	if (!indices) {
		return std::unexpected(indices.error());
	}
	auto result = std::make_shared<MeshUpload>(MeshUpload{
		.vertices = std::move(*vertices),
		.indices = std::move(*indices),
		.indexCount = mesh->indices->size()
	});
	{
		const std::scoped_lock lock{device->mutex};
		constexpr std::size_t meshCacheLimit = 128;
		if (device->meshes.size() >= meshCacheLimit) {
			device->meshes.erase(device->meshes.begin());
		}
		device->meshes.insert_or_assign(mesh->version, result);
	}
	return result;
}
} // namespace stana::platform::detail::gpu
