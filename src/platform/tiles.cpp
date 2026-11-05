#include "tiles.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stana::platform::detail {
namespace {
[[nodiscard]] auto multiply(std::size_t left, std::size_t right) -> std::size_t {
	if (right != 0 && left > std::numeric_limits<std::size_t>::max() / right) {
		throw std::overflow_error("Raster multiplication overflow");
	}
	return left * right;
}

[[nodiscard]] auto add(std::size_t left, std::size_t right) -> std::size_t {
	if (left > std::numeric_limits<std::size_t>::max() - right) {
		throw std::overflow_error("Raster addition overflow");
	}
	return left + right;
}

[[nodiscard]] auto ceilDivide(std::size_t value, std::size_t divisor) -> std::size_t {
	return value / divisor + (value % divisor != 0 ? 1 : 0);
}

[[nodiscard]] auto extent(double logical, double scale) -> std::size_t {
	if (!std::isfinite(logical) || logical < 0) {
		throw std::invalid_argument("Invalid logical viewport");
	}
	const double pixels = std::ceil(logical * scale);
	if (!std::isfinite(pixels) || pixels > static_cast<double>(maximumDimension)) {
		throw std::invalid_argument("Viewport exceeds 16384 physical pixels");
	}
	return static_cast<std::size_t>(pixels);
}
} // namespace

auto alignedRasterRow(std::size_t width) -> std::size_t {
	return multiply(
		ceilDivide(multiply(width, bytesPerPixel), rasterRowAlignment),
		rasterRowAlignment
	);
}

auto makeTileGrid(LogicalSize size, double scale, RasterLimits limits) -> TileGrid {
	if (!std::isfinite(scale) || scale <= 0) {
		throw std::invalid_argument("Invalid backing scale");
	}
	TileGrid grid;
	grid.viewport = {.width = extent(size.width, scale), .height = extent(size.height, scale)};
	if (grid.viewport.width == 0 || grid.viewport.height == 0) {
		return grid;
	}
	grid.columns = ceilDivide(grid.viewport.width, tilePixels);
	grid.rows = ceilDivide(grid.viewport.height, tilePixels);
	if (multiply(grid.columns, grid.rows) > maximumTiles) {
		throw std::invalid_argument("Tile count exceeds 1024");
	}
	for (std::size_t row = 0; row < grid.rows; ++row) {
		for (std::size_t column = 0; column < grid.columns; ++column) {
			const auto x = column * tilePixels;
			const auto y = row * tilePixels;
			TileLayout tile{
				.ordinal = static_cast<std::uint32_t>(grid.tiles.size()),
				.pixels = {
					.x = x,
					.y = y,
					.width = std::min(tilePixels, grid.viewport.width - x),
					.height = std::min(tilePixels, grid.viewport.height - y)
				}
			};
			tile.bytesPerRow = alignedRasterRow(tile.pixels.width);
			tile.allocationBytes = multiply(tile.bytesPerRow, tile.pixels.height);
			if (tile.allocationBytes > limits.maximumBufferLength) {
				throw std::invalid_argument("Tile exceeds Metal maxBufferLength");
			}
			grid.rasterBytes = add(grid.rasterBytes, tile.allocationBytes);
			grid.tiles.push_back(tile);
		}
	}
	if (add(limits.ownedBytes, grid.rasterBytes) > maximumRasterBytes) {
		throw std::invalid_argument("Current plus retired raster storage exceeds 256 MiB");
	}
	return grid;
}

auto allTiles(const TileGrid &grid) -> TileMask {
	TileMask result;
	for (const auto &tile : grid.tiles) {
		result.set(tile.ordinal);
	}
	return result;
}

void selectTiles(const TileGrid &grid, double scale, LogicalRect damage, TileMask &mask) {
	if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(damage.origin.x) ||
		!std::isfinite(damage.origin.y) || !std::isfinite(damage.size.width) ||
		!std::isfinite(damage.size.height) || damage.size.width < 0 || damage.size.height < 0) {
		throw std::invalid_argument("Invalid damage geometry");
	}
	if (damage.size.width <= 0 || damage.size.height <= 0 || grid.tiles.empty()) {
		return;
	}
	const double left = std::max(0.0, std::floor(damage.origin.x * scale));
	const double top = std::max(0.0, std::floor(damage.origin.y * scale));
	const double right = std::min(
		static_cast<double>(grid.viewport.width),
		std::ceil((damage.origin.x + damage.size.width) * scale)
	);
	const double bottom = std::min(
		static_cast<double>(grid.viewport.height),
		std::ceil((damage.origin.y + damage.size.height) * scale)
	);
	if (right <= left || bottom <= top) {
		return;
	}
	const auto firstColumn = static_cast<std::size_t>(left) / tilePixels;
	const auto firstRow = static_cast<std::size_t>(top) / tilePixels;
	const auto lastColumn = (static_cast<std::size_t>(right) - 1) / tilePixels;
	const auto lastRow = (static_cast<std::size_t>(bottom) - 1) / tilePixels;
	for (auto row = firstRow; row <= lastRow; ++row) {
		for (auto column = firstColumn; column <= lastColumn; ++column) {
			mask.set(row * grid.columns + column);
		}
	}
}

auto tileIds(const TileGrid &grid, const TileMask &mask) -> std::vector<TileId> {
	std::vector<TileId> result;
	for (const auto &tile : grid.tiles) {
		if (mask.test(tile.ordinal)) {
			result.push_back(TileId{tile.ordinal});
		}
	}
	return result;
}
} // namespace stana::platform::detail
