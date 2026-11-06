#ifndef STANA_MACOS_SHADOWS_HPP
#define STANA_MACOS_SHADOWS_HPP
#include "gpu_resources.hpp"
#include "renderer.hpp"
#include <array>
#include <map>
#include <memory>
#include <vector>

namespace stana::platform::detail::gpu {
struct alignas(vectorAlignment) PackedContour {
	std::array<float, 4> bounds{};
	std::array<float, 4> top{};
	std::array<float, 4> bottom{};
};

struct alignas(vectorAlignment) ShadowUniform {
	PackedContour caster{};
	PackedContour clip{};
	std::array<float, 4> region{};
	std::array<float, 4> options{};
	std::array<std::uint32_t, 4> blur{};
};

struct ShadowKey {
	stana::gpu::detail::BoxContour caster{};
	std::array<double, 4> region{};
	double scale = 1;
	double blurRadius = 0;
	bool inset = false;
	auto operator<=>(const ShadowKey &) const = default;
};

struct ShadowMask {
	std::shared_ptr<TextureOwner> texture;
};

struct ShadowWork {
	std::uint64_t maskBuilds = 0;
	std::uint64_t blurPasses = 0;
	std::uint64_t cacheHits = 0;
	std::uint64_t maskPixels = 0;
};

struct PreparedShadow {
	std::shared_ptr<ShadowMask> mask;
	std::vector<std::shared_ptr<TextureOwner>> scratch;
	ShadowUniform uniform{};
	LogicalRect bounds{};
};

/// @brief Window-confined GPU mask preparation. Cache tint/opacity/placement independently.
/// All textures use the shared allocation ledger; packets retain in-flight/retired owners.
class Shadows {
  public:
	[[nodiscard]] auto initialize(const Resources &) -> Result<void>;
	[[nodiscard]] auto prepare(
		Resources &,
		id<MTLCommandBuffer>,
		const stana::gpu::detail::Shadow &,
		double scale,
		LogicalRect viewport
	) -> Result<PreparedShadow>;

	void reset() { cache_.clear(); }

	[[nodiscard]] auto statistics() const noexcept -> const ShadowWork & { return work_; }

  private:
	id<MTLComputePipelineState> mask_ = nil;
	id<MTLComputePipelineState> blur_ = nil;
	std::map<ShadowKey, std::weak_ptr<ShadowMask>> cache_;
	ShadowWork work_{};
};

[[nodiscard]] auto packContour(const stana::gpu::detail::BoxContour &) -> PackedContour;
} // namespace stana::platform::detail::gpu
#endif
