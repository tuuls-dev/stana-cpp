#ifndef STANA_MACOS_GPU_RESOURCES_HPP
#define STANA_MACOS_GPU_RESOURCES_HPP
#include "../allocation.hpp"
#include "../gpu_contracts.hpp"
#import <Metal/Metal.h>
#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace stana::native::gpu {
struct Failure {
	Diagnostic::Category category = Diagnostic::Category::capability;
	std::string message;
};

template <typename T> using Result = std::expected<T, Failure>;

struct BufferOwner {
	id<MTLBuffer> buffer = nil;
	std::shared_ptr<AllocationLease> lease;
	std::size_t bytes = 0;
};

struct TextureOwner {
	id<MTLTexture> texture = nil;
	std::shared_ptr<AllocationLease> lease;
	std::size_t bytes = 0;
};

struct MeshUpload {
	std::shared_ptr<BufferOwner> vertices;
	std::shared_ptr<BufferOwner> indices;
	std::size_t indexCount = 0;
};
struct Pipeline;

/// @brief One classic queue/serial compiler admission service per physical device.
struct DeviceResources {
	id<MTLDevice> device = nil;
	id<MTLCommandQueue> queue = nil;
	id<MTLLibrary> builtins = nil;
	dispatch_queue_t compilerQueue = nullptr;
	std::shared_ptr<AllocationLedger> budget = std::make_shared<AllocationLedger>();
	std::mutex mutex;
	std::map<std::uint64_t, std::weak_ptr<MeshUpload>> meshes;
	std::map<std::uint64_t, std::shared_ptr<const Pipeline>> pipelines;
	std::vector<std::uint64_t> pipelineLRU;
	std::map<std::uint64_t, std::function<void()>> waitingCompilers;
	bool compilerActive = false;
	[[nodiscard]] static auto get(id<MTLDevice>) -> Result<std::shared_ptr<DeviceResources>>;
};

struct Resources {
	std::shared_ptr<DeviceResources> device;
	std::shared_ptr<AllocationLedger> budget = std::make_shared<AllocationLedger>();
	std::uint64_t uploadBytes = 0;
	[[nodiscard]] auto reserve(std::size_t) const -> Result<std::shared_ptr<AllocationLease>>;
	[[nodiscard]] auto
	buffer(std::size_t, std::span<const std::byte> = {}, std::shared_ptr<AllocationLease> = {})
		-> Result<std::shared_ptr<BufferOwner>>;
	[[nodiscard]] auto texture(
		std::uint32_t,
		std::uint32_t,
		Format,
		MTLTextureUsage,
		std::shared_ptr<AllocationLease> = {},
		std::uint32_t samples = 1
	) -> Result<std::shared_ptr<TextureOwner>>;
	[[nodiscard]] auto mesh(const std::shared_ptr<const Mesh> &)
		-> Result<std::shared_ptr<MeshUpload>>;
};

[[nodiscard]] auto pixelFormat(Format) -> MTLPixelFormat;
[[nodiscard]] auto pixelBytes(Format) -> std::size_t;
[[nodiscard]] auto errorMessage(NSError *, const std::string &) -> std::string;
[[nodiscard]] auto compileLibrary(id<MTLDevice>, const std::string &) -> Result<id<MTLLibrary>>;

/// @brief Dense fixed-width records, never a memcpy of public double/variant objects.
inline constexpr std::size_t vectorAlignment = 16;
inline constexpr std::size_t vertexColorOffset = 8;

struct alignas(vectorAlignment) Quad {
	std::array<float, 4> bounds{};
	std::array<float, 4> geometry{};
	std::array<float, 4> color{};
	std::array<float, 4> viewport{};
	std::array<float, 4> options{};
	std::array<std::uint32_t, 4> flags{};
};

struct alignas(vectorAlignment) Clip {
	std::array<float, 4> bounds{};
	std::array<float, 4> options{};
};

inline constexpr std::size_t quadBytes = 96;
inline constexpr std::size_t clipBytes = 32;
inline constexpr std::size_t vertexBytes = 24;
static_assert(sizeof(Quad) == quadBytes && offsetof(Quad, flags) == quadBytes - vectorAlignment);
static_assert(
	sizeof(Clip) == clipBytes && sizeof(Vertex) == vertexBytes &&
	offsetof(Vertex, color) == vertexColorOffset
);
} // namespace stana::native::gpu
#endif
