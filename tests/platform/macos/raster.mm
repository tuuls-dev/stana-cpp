#include "fixtures.hpp"
#import <AppKit/AppKit.h>
#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <memory>
#include <span>

namespace {
/// @brief Reference edge classification: a nonuniform 3x3 coverage neighborhood.
/// Uniform interiors and alpha have no tolerance; this is not a screenshot-wide allowance.
[[nodiscard]] auto
referenceEdge(const stana::native::fixtures::Bitmap &bitmap, std::size_t x, std::size_t y) -> bool {
	using stana::native::bytesPerPixel;
	const auto center =
		std::span{bitmap.bytes}.subspan(y * bitmap.bytesPerRow + x * bytesPerPixel, bytesPerPixel);
	const auto left = x > 0 ? x - 1 : 0;
	const auto top = y > 0 ? y - 1 : 0;
	for (auto row = top; row <= std::min(y + 1, bitmap.pixels.height - 1); ++row) {
		for (auto column = left; column <= std::min(x + 1, bitmap.pixels.width - 1); ++column) {
			if (!std::ranges::equal(
					center,
					std::span{bitmap.bytes}.subspan(
						row * bitmap.bytesPerRow + column * bytesPerPixel,
						bytesPerPixel
					)
				)) {
				return true;
			}
		}
	}
	return false;
}

/// @brief R01-R18: actual byte equality for every operation, fractional edge crossings and damage
/// reuse.
void parity(double scale) {
	using namespace stana::native;
	using namespace stana::native::fixtures;
	const auto source = scene();
	PreparationTrace trace;
	const auto prepared = prepareScene(source, scale, {}, &trace);
	require(
		trace.branches.size() == 1 && trace.paints.size() == 1,
		"actual prepared identity trace"
	);
	trace = {};
	require(
		prepareScene(source, scale, prepared, &trace) == prepared && trace.paints.empty(),
		"unchanged prepare is no-op"
	);
	const auto grid = makeTileGrid(source->size, scale);

	auto next = std::make_shared<Scene>(*source);
	next->revision = 2;
	auto root = std::make_shared<SceneBranch>(*source->root);
	root->generation = 2;
	auto paint = std::make_shared<PaintSegments>(*root->paint);
	paint->identity = 2;
	paint->prefix.front().blue = 1;
	root->paint = paint;
	next->root = root;
	const auto changed = prepareScene(next, scale, prepared);
	RenderingWork work;
	require(sceneDamage(prepared, prepared, grid, work).none(), "R18 no damage for unchanged tree");
	const auto damage = sceneDamage(prepared, changed, grid, work);
	require(damage.any(), "R01 color changes actual ink");
	require(
		sceneDamage(changed, prepared, grid, work) == damage,
		"R16 raster revert repairs old pixels"
	);
	// Derive a fresh publication; never mutate resources already handed to preparation.
	auto unusualPaint = std::make_shared<PaintSegments>(*paint);
	unusualPaint->prefix.push_back(
		{.operation = PaintOperation::text, .text = "👩🏽‍💻 é", .fontSize = markerSize}
	);
	unusualPaint->identity = 3;
	auto unusualSource = std::make_shared<Scene>(*next);
	auto unusualRoot = std::make_shared<SceneBranch>(*root);
	unusualRoot->paint = unusualPaint;
	unusualRoot->generation = 3;
	unusualSource->root = unusualRoot;
	unusualSource->revision = 3;
	const auto unusual = prepareScene(unusualSource, scale);
	require(
		sceneDamage(prepared, unusual, grid, work) == allTiles(grid) &&
			work.fullDamageFallbacks > 0,
		"R03/R17 unknown color glyphs use full damage"
	);
	std::size_t differingPixels = 0;
	unsigned maximumChannelError = 0;
	unsigned maximumAlphaError = 0;
	std::uint64_t absoluteError = 0;
	Bitmap reference{{.width = grid.viewport.width, .height = grid.viewport.height}};
	rasterScene(reference.context.get(), *prepared, reference.pixels, false);
	for (const auto &tile : grid.tiles) {
		Bitmap bitmap{tile.pixels};
		rasterScene(bitmap.context.get(), *prepared, tile.pixels);
		for (std::size_t row = 0; row < tile.pixels.height; ++row) {
			const auto count = tile.pixels.width * bytesPerPixel;
			const auto tileRow = std::span{bitmap.bytes}.subspan(row * bitmap.bytesPerRow, count);
			const auto fullRow = std::span{reference.bytes}.subspan(
				(tile.pixels.y + row) * reference.bytesPerRow + tile.pixels.x * bytesPerPixel,
				count
			);
			for (std::size_t pixel = 0; pixel < tile.pixels.width; ++pixel) {
				bool differs = false;
				for (std::size_t channel = 0; channel < bytesPerPixel; ++channel) {
					const auto actual = std::to_integer<unsigned>(
						tileRow.subspan(pixel * bytesPerPixel + channel, 1).front()
					);
					const auto expected = std::to_integer<unsigned>(
						fullRow.subspan(pixel * bytesPerPixel + channel, 1).front()
					);
					const auto error = actual > expected ? actual - expected : expected - actual;
					differs = differs || error != 0;
					absoluteError += error;
					maximumChannelError = std::max(maximumChannelError, error);
					if (channel == bytesPerPixel - 1) {
						maximumAlphaError = std::max(maximumAlphaError, error);
					}
				}
				if (differs) {
					++differingPixels;
					require(
						referenceEdge(reference, tile.pixels.x + pixel, tile.pixels.y + row),
						"solid reference regions must match exactly"
					);
				}
			}
		}
	}
	std::cout << "Pixel comparison scale=" << scale
			  << " pixels=" << grid.viewport.width * grid.viewport.height
			  << " differingPixels=" << differingPixels
			  << " maxChannelError=" << maximumChannelError
			  << " maxAlphaError=" << maximumAlphaError << " totalAbsoluteError=" << absoluteError
			  << '\n';
	// User-approved amendment: <=32/255 RGB error only at reference edges,
	// <=0.25% of fixture pixels; exact alpha and exact uniform regions.
	constexpr unsigned maximumAntialiasError = 32;
	constexpr std::size_t maximumDifferentPixelDenominator = 400;
	require(
		maximumAlphaError == 0 && maximumChannelError <= maximumAntialiasError &&
			differingPixels * maximumDifferentPixelDenominator <=
				grid.viewport.width * grid.viewport.height,
		"R11-R13 approved bounded antialiasing gate exceeded"
	);
}

/// @brief R01-R18 damage reconstruction is byte-exact against fresh target tile replay.
/// This stricter comparison is unaffected by the approved monolithic-reference AA allowance.
void incrementalReplay(double scale) {
	using namespace stana::native;
	using namespace stana::native::fixtures;
	auto source = std::make_shared<Scene>(*scene());
	auto root = std::make_shared<SceneBranch>(*source->root);
	constexpr std::uint64_t firstIdentity = 100;
	std::uint64_t identity = firstIdentity;
	auto emptyPaint = std::make_shared<PaintSegments>();
	emptyPaint->identity = ++identity;
	root->paint = emptyPaint;
	root->generation = ++identity;
	const auto commands = source->root->paint->prefix;
	constexpr std::size_t clipStart = 6;
	constexpr std::size_t clipEnd = 8;
	for (std::size_t index = 0; index < commands.size(); ++index) {
		auto paint = std::make_shared<PaintSegments>();
		paint->identity = ++identity;
		paint->prefix.push_back(commands.at(index));
		if (index == clipStart) {
			while (index < clipEnd) {
				paint->prefix.push_back(commands.at(++index));
			}
		}
		auto child = std::make_shared<SceneBranch>();
		child->id = {.document = 1, .serial = ++identity};
		child->generation = ++identity;
		child->paint = paint;
		root->children.push_back(child);
	}
	source->root = root;
	const auto grid = makeTileGrid(source->size, scale);
	std::vector<std::unique_ptr<Bitmap>> tiles;
	tiles.reserve(grid.tiles.size());
	for (const auto &layout : grid.tiles) {
		tiles.push_back(std::make_unique<Bitmap>(layout.pixels));
	}
	std::shared_ptr<const PreparedScene> rasterBaseline;
	RenderingWork work;
	auto apply = [&](std::shared_ptr<const Scene> targetSource) -> void {
		const auto target = prepareScene(std::move(targetSource), scale, rasterBaseline);
		const auto dirty = sceneDamage(rasterBaseline, target, grid, work);
		for (const auto &layout : grid.tiles) {
			auto &tile = *tiles.at(layout.ordinal);
			if (dirty.test(layout.ordinal)) {
				rasterScene(tile.context.get(), *target, layout.pixels);
			}
			Bitmap fresh{layout.pixels};
			rasterScene(fresh.context.get(), *target, layout.pixels, false);
			for (std::size_t row = 0; row < layout.pixels.height; ++row) {
				const auto bytes = layout.pixels.width * bytesPerPixel;
				require(
					std::ranges::equal(
						std::span{tile.bytes}.subspan(row * tile.bytesPerRow, bytes),
						std::span{fresh.bytes}.subspan(row * fresh.bytesPerRow, bytes)
					),
					"incremental damage left stale or missing pixels"
				);
			}
		}
		rasterBaseline = target;
	};
	apply(source);
	const std::shared_ptr<const Scene> original = source;
	enum class Edit : std::uint8_t {
		color,
		shorten,
		move,
		opacity,
		reorder,
		overflow,
		clip,
		hide,
		show,
		background,
		remove
	};
	constexpr std::array edits{
		Edit::color,
		Edit::shorten,
		Edit::move,
		Edit::opacity,
		Edit::reorder,
		Edit::overflow,
		Edit::clip,
		Edit::hide,
		Edit::show,
		Edit::background,
		Edit::remove
	};
	for (const auto edit : edits) {
		auto next = std::make_shared<Scene>(*source);
		next->revision = ++identity;
		auto nextRoot = std::make_shared<SceneBranch>(*source->root);
		nextRoot->generation = ++identity;
		next->root = nextRoot;
		auto child = std::make_shared<SceneBranch>(*nextRoot->children.front());
		child->generation = ++identity;
		auto paint = std::make_shared<PaintSegments>(*child->paint);
		paint->identity = ++identity;
		child->paint = paint;
		nextRoot->children.front() = child;
		constexpr double shifted = 550.5;
		constexpr double opacity = 0.25;
		constexpr double outside = -100;
		constexpr double clipWidth = 600;
		constexpr double clipHeight = 550;
		switch (edit) {
		case Edit::color:
			paint->prefix.front().green = 1;
			break;
		case Edit::shorten: {
			constexpr std::size_t textIndex = 5;
			auto label = std::make_shared<SceneBranch>(*nextRoot->children.at(textIndex));
			label->generation = ++identity;
			auto labelPaint = std::make_shared<PaintSegments>(*label->paint);
			labelPaint->identity = ++identity;
			labelPaint->prefix.front().text = "I";
			label->paint = labelPaint;
			nextRoot->children.at(textIndex) = label;
			break;
		}
		case Edit::move:
			paint->prefix.front().x = shifted;
			break;
		case Edit::opacity:
			paint->prefix.front().alpha = opacity;
			break;
		case Edit::reorder:
			std::swap(nextRoot->children.front(), nextRoot->children.back());
			break;
		case Edit::overflow:
			paint->prefix.front().x = outside;
			paint->prefix.front().width = shifted;
			break;
		case Edit::clip: {
			auto clipping = std::make_shared<PaintSegments>();
			clipping->identity = ++identity;
			clipping->prefix.push_back(
				{.operation = PaintOperation::pushClip,
					.width = clipWidth,
					.height = clipHeight,
					.radius = markerSize}
			);
			clipping->suffix.push_back({.operation = PaintOperation::popClip});
			clipping->childClipBounds =
				LogicalRect{.size = {.width = clipWidth, .height = clipHeight}};
			nextRoot->paint = clipping;
			break;
		}
		case Edit::hide:
			child->visible = false;
			break;
		case Edit::show:
			child->visible = true;
			break;
		case Edit::background:
			next->background = {.red = opacity, .green = opacity, .blue = opacity};
			break;
		case Edit::remove:
			nextRoot->children.pop_back();
			break;
		}
		source = next;
		apply(source);
	}
	apply(original);
	const auto confirmedA = rasterBaseline;
	auto blue = std::make_shared<Scene>(*original);
	blue->revision = ++identity;
	auto blueRoot = std::make_shared<SceneBranch>(*original->root);
	blueRoot->generation = ++identity;
	auto blueChild = std::make_shared<SceneBranch>(*blueRoot->children.front());
	blueChild->generation = ++identity;
	auto bluePaint = std::make_shared<PaintSegments>(*blueChild->paint);
	bluePaint->identity = ++identity;
	bluePaint->prefix.front().blue = 1;
	blueChild->paint = bluePaint;
	blueRoot->children.front() = blueChild;
	blue->root = blueRoot;
	apply(blue); // CPU B succeeded but no presentation was admitted.
	auto redAgain = std::make_shared<Scene>(*blue);
	redAgain->revision = ++identity;
	auto redRoot = std::make_shared<SceneBranch>(*blueRoot);
	redRoot->generation = ++identity;
	auto redChild = std::make_shared<SceneBranch>(*blueChild);
	redChild->generation = ++identity;
	auto redPaint = std::make_shared<PaintSegments>(*bluePaint);
	redPaint->identity = ++identity;
	redPaint->prefix.front().blue = 0;
	redChild->paint = redPaint;
	redRoot->children.front() = redChild;
	redAgain->root = redRoot;
	const auto restored = prepareScene(redAgain, scale, rasterBaseline);
	require(
		sceneDamage(confirmedA, restored, grid, work).none() &&
			sceneDamage(rasterBaseline, restored, grid, work).any(),
		"R16 display-equivalent fresh A still repairs raster-only B damage"
	);
	apply(redAgain);
	require(
		sceneDamage(rasterBaseline, rasterBaseline, grid, work).none(),
		"unchanged target has no raster demand"
	);
}

} // namespace

auto main() -> int {
	@autoreleasepool {
		try {
			stana::native::fixtures::require(
				NSApp == nil,
				"CPU raster must not initialize NSApplication"
			);
			bool passed = true;
			constexpr std::array scales{1.0, 2.0};
			for (const auto scale : scales) {
				try {
					parity(scale);
					incrementalReplay(scale);
				} catch (const std::exception &error) {
					std::cerr << error.what() << '\n';
					passed = false;
				}
			}
			stana::native::fixtures::require(NSApp == nil, "CPU raster initialized NSApplication");
			if (passed) {
				std::cout << "Approved edge-bounded full/tile 1x/2x parity, exact alpha/interiors, "
							 "1025x769 logical\n";
			}
			return passed ? 0 : 1;
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	}
}
