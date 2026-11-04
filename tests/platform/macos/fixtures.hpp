#ifndef STANA_RASTER_FIXTURES_HPP
#define STANA_RASTER_FIXTURES_HPP
#include "../../../src/platform/macos/renderer.hpp"
#include <stdexcept>
#include <string>
#include <vector>

namespace stana::native::fixtures {
inline void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

inline constexpr double edge = 510.25;
inline constexpr double markerSize = 20;
inline constexpr double translucent = 0.5;
inline constexpr double radius = 6;
inline constexpr double longLineX = 5.25;
inline constexpr double longLineY = 10.25;
inline constexpr double longLineWidth = 1000.25;
inline constexpr double longLineHeight = 700.25;
inline constexpr double fixtureWidth = 1025;
inline constexpr double fixtureHeight = 769;

[[nodiscard]] inline auto scene() -> std::shared_ptr<const Scene> {
	auto source = std::make_shared<Scene>();
	source->revision = 1;
	source->size = {.width = fixtureWidth, .height = fixtureHeight};
	source->background = {.red = 1, .green = 1, .blue = 1};
	auto paint = std::make_shared<PaintSegments>();
	paint->identity = 1;
	paint->prefix = {
		{.operation = PaintOperation::fill,
			.x = 0,
			.y = 0,
			.width = markerSize,
			.height = markerSize,
			.red = 1},
		{.operation = PaintOperation::fill,
			.x = fixtureWidth - markerSize,
			.y = fixtureHeight - markerSize,
			.width = markerSize,
			.height = markerSize,
			.blue = 1},
		{.operation = PaintOperation::fill,
			.x = edge,
			.y = edge,
			.width = markerSize,
			.height = markerSize,
			.green = 1,
			.alpha = translucent},
		{.operation = PaintOperation::stroke,
			.x = edge,
			.y = 0,
			.width = markerSize,
			.height = markerSize,
			.red = 1,
			.radius = radius,
			.lineWidth = 0},
		{.operation = PaintOperation::line,
			.x = edge + markerSize,
			.y = edge,
			.width = -markerSize,
			.height = markerSize,
			.blue = 1,
			.lineWidth = 2},
		{.operation = PaintOperation::text,
			.x = edge,
			.y = edge,
			.text = "Italic phase ffi",
			.fontSize = markerSize},
		{.operation = PaintOperation::pushClip,
			.x = edge,
			.y = markerSize,
			.width = markerSize,
			.height = markerSize,
			.radius = radius},
		{.operation = PaintOperation::fill,
			.x = edge - markerSize,
			.y = markerSize,
			.width = markerSize * 3,
			.height = markerSize * 2,
			.blue = 1,
			.alpha = translucent},
		{.operation = PaintOperation::popClip},
		{.operation = PaintOperation::line,
			.x = longLineX,
			.y = longLineY,
			.width = longLineWidth,
			.height = longLineHeight,
			.red = 1,
			.alpha = translucent}
	};
	auto root = std::make_shared<SceneBranch>();
	root->id = {.document = 1, .serial = 1};
	root->generation = 1;
	root->paint = paint;
	source->root = root;
	return source;
}

/// @brief Ordinary bitmap only for explicit CPU reference fixtures, never a host fallback.
struct Bitmap {
	PixelRect pixels;
	std::size_t bytesPerRow;
	std::vector<std::byte> bytes;
	CoreOwner<CGContextRef> context;

	explicit Bitmap(PixelRect rect)
		: pixels(rect),
		  bytesPerRow(alignedRasterRow(rect.width)),
		  bytes(bytesPerRow * rect.height),
		  context(makeContext(bytes.data(), rect, bytesPerRow)) {}

	[[nodiscard]] static auto makeContext(void *storage, PixelRect rect, std::size_t row)
		-> CGContextRef {
		const CoreOwner color{CGColorSpaceCreateWithName(kCGColorSpaceSRGB)};
		constexpr std::size_t componentBits = 8;
		const auto info = static_cast<CGBitmapInfo>(
			static_cast<std::uint32_t>(kCGImageAlphaPremultipliedFirst) |
			static_cast<std::uint32_t>(kCGBitmapByteOrder32Little)
		);
		auto *const result = CGBitmapContextCreate(
			storage,
			rect.width,
			rect.height,
			componentBits,
			row,
			color.get(),
			info
		);
		if (result == nullptr) {
			throw std::runtime_error("Fixture bitmap failed");
		}
		return result;
	}
};
} // namespace stana::native::fixtures
#endif
