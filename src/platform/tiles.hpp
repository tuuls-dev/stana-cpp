#ifndef STANA_TILES_HPP
#define STANA_TILES_HPP
#include "native_contracts.hpp"
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace stana::platform::detail {
inline constexpr std::size_t tilePixels = 512;
inline constexpr std::size_t rasterRowAlignment = 256;
inline constexpr std::size_t bytesPerPixel = 4;
inline constexpr std::size_t maximumDimension = 16384;
inline constexpr std::size_t maximumTiles = 1024;
inline constexpr std::size_t maximumRasterBytes = std::size_t{256} * 1024 * 1024;
inline constexpr std::size_t tileDrawableCount = 2;

/// @brief Checked physical extents; never populated by unchecked floating conversion.
struct PixelSize {
	std::size_t width = 0;
	std::size_t height = 0;
};

struct PixelRect {
	std::size_t x = 0;
	std::size_t y = 0;
	std::size_t width = 0;
	std::size_t height = 0;
};

struct TileLayout {
	std::uint32_t ordinal = 0;
	PixelRect pixels{};
	std::size_t bytesPerRow = 0;
	std::size_t allocationBytes = 0;
};

/// @brief Row-major nonempty physical tiles, with aligned shared-buffer accounting.
struct TileGrid {
	PixelSize viewport{};
	std::size_t columns = 0;
	std::size_t rows = 0;
	std::size_t rasterBytes = 0;
	std::vector<TileLayout> tiles;
};

using TileMask = std::bitset<maximumTiles>;

struct RasterLimits {
	std::size_t ownedBytes = 0;
	std::size_t maximumBufferLength = std::numeric_limits<std::size_t>::max();
};

/// @brief Validates viewport, scale, arithmetic, device and current+retired budget before
/// allocation.
[[nodiscard]] auto makeTileGrid(LogicalSize, double scale, RasterLimits limits = {}) -> TileGrid;
/// @brief Adds outward-rounded, already ink-expanded logical damage to a bounded physical mask.
void selectTiles(const TileGrid &, double scale, LogicalRect damage, TileMask &);
[[nodiscard]] auto allTiles(const TileGrid &) -> TileMask;
[[nodiscard]] auto tileIds(const TileGrid &, const TileMask &) -> std::vector<TileId>;
/// @brief Includes checked row padding in allocation size, not copied pixel payload.
[[nodiscard]] auto alignedRasterRow(std::size_t width) -> std::size_t;
} // namespace stana::platform::detail
#endif
