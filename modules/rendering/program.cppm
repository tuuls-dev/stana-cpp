module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>
export module stana:rendering_program;
import :primitives;

export namespace stana::rendering {
namespace detail {
struct GPUAccess;
inline constexpr double gpuSourcePeak = 16;
} // namespace detail

/// @brief Owning portable descriptor or execution failure; allocation failures remain exceptions.
struct GPUError {
	enum class Category : std::uint8_t {
		invalidDescriptor,
		compiler,
		binding,
		capability,
		execution
	};
	Category category = Category::invalidDescriptor;
	std::string message;
};

/// @brief Delivered on the owning UI executor, never from a compiler/GPU callback.
struct GPUDiagnostic {
	enum class Category : std::uint8_t { compiler, binding, capability, execution };
	Category category = Category::compiler;
	std::string message;
	NodeId instance{};
	std::uint64_t programVersion = 0;
};
enum class GPUColorEncoding : std::uint8_t { encodedSRGB, extendedLinear };
enum class GPUTextureFormat : std::uint8_t { bgra8Unorm, rgba16Float, r8Unorm };
enum class RenderingColorMode : std::uint8_t { automatic, sdr, extendedLinear };

/// @brief Immutable owned WGSL source. No platform-selected payload or native escape hatch.
class GPUShader {
  public:
	[[nodiscard]] static auto source(std::string) -> std::expected<GPUShader, GPUError>;
	[[nodiscard]] auto source() const noexcept -> const std::string &;
	[[nodiscard]] auto version() const noexcept -> std::uint64_t;
	[[nodiscard]] auto operator==(const GPUShader &) const noexcept -> bool;

  private:
	friend struct ::stana::rendering::detail::GPUAccess;
	struct Data;
	std::shared_ptr<const Data> data_;

	explicit GPUShader(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
};

using GPUFunctionConstant = std::variant<bool, std::int32_t, float>;

class GPUBuffer {
  public:
	[[nodiscard]] static auto create(std::span<const std::byte>)
		-> std::expected<GPUBuffer, GPUError>;
	[[nodiscard]] auto bytes() const noexcept -> std::span<const std::byte>;
	[[nodiscard]] auto version() const noexcept -> std::uint64_t;
	[[nodiscard]] auto operator==(const GPUBuffer &) const noexcept -> bool;

  private:
	friend struct ::stana::rendering::detail::GPUAccess;
	struct Data;
	std::shared_ptr<const Data> data_;

	explicit GPUBuffer(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
};

struct GPUTextureDescriptor {
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	GPUTextureFormat format = GPUTextureFormat::bgra8Unorm;
	std::vector<std::byte> bytes{};
	GPUColorEncoding encoding = GPUColorEncoding::encodedSRGB;
	bool premultiplied = true;
	double sourcePeak = ::stana::rendering::detail::gpuSourcePeak;
};

class GPUTexture {
  public:
	[[nodiscard]] static auto create(GPUTextureDescriptor) -> std::expected<GPUTexture, GPUError>;
	[[nodiscard]] auto descriptor() const noexcept -> const GPUTextureDescriptor &;
	[[nodiscard]] auto bytes() const noexcept -> std::span<const std::byte>;
	[[nodiscard]] auto version() const noexcept -> std::uint64_t;
	[[nodiscard]] auto operator==(const GPUTexture &) const noexcept -> bool;

  private:
	friend struct ::stana::rendering::detail::GPUAccess;
	struct Data;
	std::shared_ptr<const Data> data_;

	explicit GPUTexture(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
};

struct GPUStateSlotDescriptor {
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	GPUTextureFormat format = GPUTextureFormat::rgba16Float;
	std::vector<std::byte> initialBytes{};
	std::string resetKey{};
};

class GPUStateSlot {
  public:
	[[nodiscard]] static auto create(GPUStateSlotDescriptor)
		-> std::expected<GPUStateSlot, GPUError>;
	[[nodiscard]] auto descriptor() const noexcept -> const GPUStateSlotDescriptor &;
	[[nodiscard]] auto version() const noexcept -> std::uint64_t;
	[[nodiscard]] auto operator==(const GPUStateSlot &) const noexcept -> bool;

  private:
	friend struct ::stana::rendering::detail::GPUAccess;
	struct Data;
	std::shared_ptr<const Data> data_;

	explicit GPUStateSlot(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
};

struct GPUSampler {
	enum class Filter : std::uint8_t { nearest, linear };
	Filter filter = Filter::linear;
	auto operator<=>(const GPUSampler &) const = default;
};

struct GPUStorage {
	std::size_t length = 0;
	auto operator<=>(const GPUStorage &) const = default;
};

struct GPUTarget {
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	GPUTextureFormat format = GPUTextureFormat::rgba16Float;
	auto operator<=>(const GPUTarget &) const = default;
};

using GPUResource =
	std::variant<GPUBuffer, GPUStorage, GPUTexture, GPUTarget, GPUStateSlot, GPUSampler>;

struct GPUBinding {
	std::string resource;
	std::uint32_t group = 0;
	std::uint32_t binding = 0;
	enum class Access : std::uint8_t { read, write } access = Access::read;
	enum class Stage : std::uint8_t { vertex, fragment, compute } stage = Stage::fragment;
	bool previous = false;
	auto operator<=>(const GPUBinding &) const = default;
};

struct GPURenderPass {
	std::string vertex;
	std::string fragment;
	std::string target = "output";
	std::uint32_t vertexCount = 3;
	std::uint32_t instanceCount = 1;
	std::optional<std::string> indexBuffer{};
	std::vector<GPUBinding> bindings{};
	bool blends = false;
	auto operator<=>(const GPURenderPass &) const = default;
};

struct GPUComputePass {
	std::string function;
	std::array<std::uint32_t, 3> workgroupCount{1, 1, 1};
	std::vector<GPUBinding> bindings{};
	auto operator<=>(const GPUComputePass &) const = default;
};

using GPUPass = std::variant<GPURenderPass, GPUComputePass>;

struct GPUProgramDescriptor {
	GPUShader shader;
	std::map<std::string, GPUResource, std::less<>> resources{{"output", GPUTarget{}}};
	std::vector<GPUPass> passes{};
	std::string output = "output";
	GPUColorEncoding outputColor = GPUColorEncoding::extendedLinear;
	double sourcePeak = ::stana::rendering::detail::gpuSourcePeak;
	std::map<std::string, GPUFunctionConstant, std::less<>> constants{};
	std::optional<double> samplingRadius{};
};

/// @brief Immutable validated dependency plan. Resource replacement never mutates this value.
class GPUProgram {
  public:
	[[nodiscard]] static auto create(GPUProgramDescriptor) -> std::expected<GPUProgram, GPUError>;
	[[nodiscard]] auto updatingResources(std::map<std::string, GPUResource, std::less<>>) const
		-> std::expected<GPUProgram, GPUError>;
	[[nodiscard]] auto descriptor() const noexcept -> const GPUProgramDescriptor &;
	[[nodiscard]] auto executionOrder() const noexcept -> std::span<const std::size_t>;
	[[nodiscard]] auto version() const noexcept -> std::uint64_t;
	[[nodiscard]] auto pipelineVersion() const noexcept -> std::uint64_t;
	[[nodiscard]] auto stateVersion() const noexcept -> std::uint64_t;
	[[nodiscard]] auto operator==(const GPUProgram &) const noexcept -> bool;

  private:
	friend struct ::stana::rendering::detail::GPUAccess;
	struct Data;
	std::shared_ptr<const Data> data_;

	explicit GPUProgram(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
};
} // namespace stana::rendering
