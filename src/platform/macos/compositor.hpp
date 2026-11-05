#ifndef STANA_MACOS_COMPOSITOR_HPP
#define STANA_MACOS_COMPOSITOR_HPP
#include "gpu_programs.hpp"
#include "renderer.hpp"
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace stana::platform::detail::gpu {
struct CPUAsset {
	std::shared_ptr<RasterTile> raster;
	std::shared_ptr<TextureOwner> texture;
	LogicalRect bounds;
};

struct CPUKey {
	double scale = 1;
	std::array<std::size_t, 2> viewport{};
	std::vector<stana::platform::detail::Command> commands{};
	auto operator<=>(const CPUKey &) const = default;
};

struct ProgramInstance {
	std::shared_ptr<const Program> desired;
	std::shared_ptr<const Pipeline> pipeline;
	std::shared_ptr<const ProgramBundle> committed;
	std::uint64_t failedVersion = 0;
	std::uint64_t requestedVersion = 0;
};

/// @brief Temporal state is per mounted occurrence and program command, not per node alone.
struct ProgramInstanceKey {
	NodeIdentity node{};
	std::size_t ordinal = 0;
	auto operator<=>(const ProgramInstanceKey &) const = default;
};

struct CompositePacket {
	std::shared_ptr<const PreparedScene> scene;
	std::shared_ptr<AllocationLedger> budget;
	std::vector<std::shared_ptr<TextureOwner>> tiles;
	std::vector<std::shared_ptr<TextureOwner>> multisample;
	std::uint32_t samples = 1;
	std::vector<std::shared_ptr<CPUAsset>> cpuAssets;
	std::vector<std::shared_ptr<MeshUpload>> meshes;
	std::vector<std::shared_ptr<BufferOwner>> buffers;
	std::vector<ProgramCandidate> candidates;
	std::vector<ProgramInstanceKey> candidateInstances;
	TileMask dirty;
	bool linear = false;
	bool outputLinear = false;
	std::uint64_t epoch = 0;
	std::uint64_t generation = 0;
};

struct CompositorWork {
	std::uint64_t completedRevision = 0;
	std::uint64_t stateSteps = 0;
	std::uint64_t pipelineBuilds = 0;
	std::uint64_t pipelineCacheHits = 0;
	std::uint64_t cpuRasterPixels = 0;
	std::uint64_t gpuLeafDraws = 0;
	std::uint64_t compositedTiles = 0;
};

/// @brief UI-confined preparation and atomic offscreen completion, separate from presentation.
class Compositor : public std::enable_shared_from_this<Compositor> {
  public:
	[[nodiscard]] static auto
		create(std::shared_ptr<DeviceResources>, std::shared_ptr<RasterAccounting>)
			-> Result<std::shared_ptr<Compositor>>;
	[[nodiscard]] static auto requiresHybrid(const std::shared_ptr<const Scene> &, ColorMode)
		-> bool;
	[[nodiscard]] static auto requiresLinear(const std::shared_ptr<const Scene> &, ColorMode)
		-> bool;

	[[nodiscard]] auto resources() noexcept -> Resources & { return resources_; }

	[[nodiscard]] auto statistics() const noexcept -> const CompositorWork & { return work_; }

	[[nodiscard]] auto readyPacket() const -> std::shared_ptr<const CompositePacket> {
		return ready_;
	}

	[[nodiscard]] auto isInFlight() const noexcept -> bool { return inFlight_; }

	[[nodiscard]] auto hasPendingWork() const noexcept -> bool {
		return inFlight_ || compilerActive_ || (ready_ && ready_->generation != generation_);
	}

	void setWake(std::function<void()> callback) { wake_ = std::move(callback); }

	void setDiagnostic(std::function<void(Diagnostic)> callback) {
		diagnostic_ = std::move(callback);
	}

	void setExecutionFailure(std::function<void(Failure)> callback) {
		executionFailure_ = std::move(callback);
	}

	[[nodiscard]] auto
	prepare(std::shared_ptr<const PreparedScene>, const TileGrid &, std::uint64_t epoch, ColorMode)
		-> Result<bool>;
	void desired(std::shared_ptr<const Scene>);
	void reset(std::uint64_t epoch);
	void close();
	void display(
		id<MTLCommandBuffer>,
		const CompositePacket &,
		std::size_t tile,
		id<MTLTexture>,
		double headroom
	) const;

  private:
	Resources resources_;
	CompositorWork work_;
	std::function<void()> wake_;
	std::function<void(Diagnostic)> diagnostic_;
	std::function<void(Failure)> executionFailure_;
	std::shared_ptr<const CompositePacket> ready_;
	bool inFlight_ = false;
	std::shared_ptr<RasterAccounting> rasterAccounting_;
	std::shared_ptr<const Scene> desired_;
	std::map<ProgramInstanceKey, ProgramInstance> instances_;
	std::map<CPUKey, std::weak_ptr<CPUAsset>> cpuCache_;
	std::map<std::pair<std::uint64_t, std::size_t>, std::weak_ptr<BufferOwner>> lineCache_;
	std::array<id<MTLRenderPipelineState>, 4> shape_{};
	std::array<id<MTLRenderPipelineState>, 4> texture_{};
	std::array<id<MTLRenderPipelineState>, 4> mesh_{};
	std::array<id<MTLRenderPipelineState>, 2> display_{};
	std::uint64_t epoch_ = 0;
	std::uint64_t generation_ = 0;
	std::uint64_t window_ = 0;
	bool compilerActive_ = false;
	bool closed_ = false;
	std::uint32_t samples_ = 4;
	std::size_t diagnosticPending_ = 0;
	std::vector<LogicalRect> changed_;
	void
	compile(const GPUCanvasLeaf &, std::size_t ordinal, const std::shared_ptr<const Program> &);
	void report(const GPUCanvasLeaf &, std::uint64_t program, const Failure &);
	[[nodiscard]] auto
	cpu(const PreparedOperation &,
		const std::vector<PreparedOperation> &,
		const PreparedScene &,
		const TileGrid &,
		id<MTLCommandBuffer>) -> Result<std::shared_ptr<CPUAsset>>;
};

struct Capture {
	std::uint64_t revision = 0;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	double scale = 1;
	Format format = Format::bgra8;
	Bytes bytes{};
};

/// @brief Asynchronous complete offscreen readback; never produces display evidence.
void capture(
	const std::shared_ptr<DeviceResources> &,
	std::shared_ptr<const CompositePacket>,
	const TileGrid &,
	std::function<void(Result<Capture>)>
);
} // namespace stana::platform::detail::gpu
#endif
