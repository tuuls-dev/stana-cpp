#ifndef STANA_MACOS_RENDERER_HPP
#define STANA_MACOS_RENDERER_HPP
#include "../allocation.hpp"
#include "../tiles.hpp"
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <Metal/Metal.h>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace stana::native {
/// @brief Balanced Create/Copy-rule ownership; native handles never enter public modules.
template <typename Reference> class CoreOwner {
  public:
	explicit CoreOwner(Reference reference) : reference_(reference) {}

	CoreOwner(const CoreOwner &) = delete;
	CoreOwner(CoreOwner &&) = delete;
	auto operator=(const CoreOwner &) -> CoreOwner & = delete;
	auto operator=(CoreOwner &&) -> CoreOwner & = delete;

	~CoreOwner() {
		if (reference_ != nullptr) {
			CFRelease(reference_);
		}
	}

	[[nodiscard]] auto get() const noexcept -> Reference { return reference_; }

  private:
	Reference reference_{};
};

/// @brief Conservative raw ink. Unknown selects viewport damage and disables unsafe culling.
struct Ink {
	std::optional<LogicalRect> bounds;
	bool unknown = false;
};

struct PreparedText {
	CoreOwner<CTLineRef> line;
	double ascent = 0;
	explicit PreparedText(const Command &);
};

struct PreparedPath {
	CoreOwner<CGPathRef> path;
	explicit PreparedPath(const Command &);
};

struct PreparedOperation {
	Command command;
	std::shared_ptr<const PreparedText> text{};
	std::shared_ptr<const PreparedPath> path{};
	Ink ink{};
};

struct PreparedPaint {
	std::uint64_t identity = 0;
	std::vector<PreparedOperation> prefix;
	std::vector<PreparedOperation> suffix;
	std::optional<LogicalRect> childClipBounds;
	double childClipRadius = 0;
	Ink prefixInk;
	Ink suffixInk;
};

struct PreparedBranch {
	std::shared_ptr<const SceneBranch> source;
	std::shared_ptr<const PreparedPaint> paint;
	std::vector<std::shared_ptr<const PreparedBranch>> children;
	Ink ink{};
};

struct PreparedScene {
	std::shared_ptr<const Scene> source;
	double scale = 1;
	std::shared_ptr<const PreparedBranch> root;
};

/// @brief Private fixture record of actual preparation, not inferred scene equality.
struct PreparationTrace {
	std::vector<NodeIdentity> branches;
	std::vector<std::uint64_t> paints;
};

[[nodiscard]] auto prepareScene(
	std::shared_ptr<const Scene>,
	double scale,
	const std::shared_ptr<const PreparedScene> &previous = {},
	PreparationTrace *trace = nullptr
) -> std::shared_ptr<const PreparedScene>;
[[nodiscard]] auto sceneDamage(
	const std::shared_ptr<const PreparedScene> &before,
	const std::shared_ptr<const PreparedScene> &after,
	const TileGrid &,
	RenderingWork &
) -> TileMask;
/// @brief Full-scene reference and production tile replay share typography and state setup.
/// CPU fixtures supply ordinary bitmap contexts; production supplies shared Metal memory.
void rasterScene(CGContextRef, const PreparedScene &, PixelRect, bool cullBranches = true);
void rasterRun(CGContextRef, double scale, PixelRect, const std::vector<PreparedOperation> &);

/// @brief Independent atomic accounting survives off-main GPU resource cleanup.
struct RasterAccounting {
	std::shared_ptr<AllocationLedger> deviceBudget = std::make_shared<AllocationLedger>();
	std::shared_ptr<AllocationLedger> windowBudget = std::make_shared<AllocationLedger>();

	RasterAccounting() { deviceBudget->limit = deviceAllocationLimit; }

	std::atomic<std::size_t> bytes{0};
	std::atomic<std::size_t> peak{0};
	std::atomic<std::uint64_t> allocations{0};
	std::atomic<std::uint64_t> managedWriteBytes{0};
};

/// @brief One reusable context backed directly by shared Metal memory; noncopyable.
struct RasterTile {
	TileLayout layout;
	id<MTLBuffer> buffer = nil;
	CGContextRef bitmapContext = nullptr;
	mutable bool cpuDirty = true;
	std::shared_ptr<RasterAccounting> accounting;
	std::shared_ptr<AllocationLease> combinedLease;
	RasterTile(TileLayout, id<MTLDevice>, std::shared_ptr<RasterAccounting>);
	RasterTile(const RasterTile &) = delete;
	RasterTile(RasterTile &&) = delete;
	auto operator=(const RasterTile &) -> RasterTile & = delete;
	auto operator=(RasterTile &&) -> RasterTile & = delete;
	~RasterTile();
};

/// @brief Validates exact texture extent/format/device before copying the complete fresh tile.
void encodeTile(id<MTLBlitCommandEncoder>, const RasterTile &, id<MTLTexture>);
} // namespace stana::native
#endif
