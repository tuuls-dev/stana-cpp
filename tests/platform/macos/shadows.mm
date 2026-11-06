#include "../../../src/platform/macos/compositor.hpp"
#include "shadow_fixture.hpp"
#import <Foundation/Foundation.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstddef>
#include <exception>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace native = stana::platform::detail;
namespace gpu = stana::platform::detail::gpu;

constexpr std::chrono::seconds completionDeadline{15};
constexpr double runLoopSlice = 0.01;
constexpr std::size_t removalIndex = 8;
constexpr double referenceWidth = 120;
constexpr double referenceHeight = 90;
constexpr double blurTolerance = 0.05;

void require(bool value, const std::string &message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

void pump(const std::function<bool()> &condition) {
	const auto start = std::chrono::steady_clock::now();
	while (!condition() && std::chrono::steady_clock::now() - start < completionDeadline) {
		static_cast<void>(
			CFRunLoopRunInMode(kCFRunLoopDefaultMode, runLoopSlice, static_cast<Boolean>(0))
		);
	}
	require(condition(), "GPU shadow completion timed out");
}

[[nodiscard]] auto compositor(const std::shared_ptr<gpu::DeviceResources> &device)
	-> std::shared_ptr<gpu::Compositor> {
	const auto result =
		gpu::Compositor::create(device, std::make_shared<native::RasterAccounting>());
	require(result.has_value(), result ? "" : result.error().message);
	return *result;
}

void render(
	const std::shared_ptr<gpu::Compositor> &renderer,
	const std::shared_ptr<const native::PreparedScene> &scene,
	const native::TileGrid &grid,
	std::uint64_t epoch = 1
) {
	const auto encoded = renderer->prepare(scene, grid, epoch, stana::gpu::detail::ColorMode::sdr);
	require(encoded.has_value(), encoded ? "" : encoded.error().message);
	pump([&] -> bool {
		return !renderer->isInFlight() && renderer->readyPacket() &&
			renderer->readyPacket()->scene == scene;
	});
}

[[nodiscard]] auto read(
	const std::shared_ptr<gpu::DeviceResources> &device,
	const std::shared_ptr<gpu::Compositor> &renderer,
	const native::TileGrid &grid
) -> gpu::Capture {
	const auto promise = std::make_shared<std::promise<gpu::Result<gpu::Capture>>>();
	auto future = promise->get_future();
	gpu::capture(
		device,
		renderer->readyPacket(),
		grid,
		[promise](gpu::Result<gpu::Capture> result) -> void {
			promise->set_value(std::move(result));
		}
	);
	require(future.wait_for(completionDeadline) == std::future_status::ready, "capture timeout");
	auto result = future.get();
	require(result.has_value(), result ? "" : result.error().message);
	return std::move(*result);
}

void incremental(const std::shared_ptr<gpu::DeviceResources> &device) {
	constexpr double fractionalScale = 1.25;
	constexpr double doubleScale = 2;
	using stana::testing::fixtures::platform::macos::ShadowSceneKind;
	for (const auto kind :
		{ShadowSceneKind::retained, ShadowSceneKind::explicitDrawing, ShadowSceneKind::mixed}) {
		const auto sources = stana::testing::fixtures::platform::macos::shadowScenes(kind);
		if (kind == ShadowSceneKind::retained) {
			const auto containsGPU =
				[&](const auto &self,
					const std::shared_ptr<const native::SceneBranch> &branch) -> bool {
				return branch &&
					(branch->paint->gpu ||
						std::ranges::any_of(branch->children, [&](const auto &child) -> bool {
							return self(self, child);
						}));
			};
			require(
				!containsGPU(containsGPU, sources.front()->root),
				"ordinary shadows must not mount a GPUCanvas"
			);
		}
		for (const double scale : {1.0, fractionalScale, doubleScale}) {
			const auto grid = native::makeTileGrid(sources.front()->size, scale);
			const auto renderer = compositor(device);
			std::shared_ptr<const native::PreparedScene> previous;
			std::uint64_t initialMasks = 0;
			std::uint64_t initialBlurs = 0;
			for (std::size_t index = 0; index < sources.size(); ++index) {
				const auto scene = native::prepareScene(sources.at(index), scale, previous);
				require(
					gpu::Compositor::requiresHybrid(
						sources.at(index),
						stana::gpu::detail::ColorMode::sdr
					) || index == removalIndex,
					"retained shadows alone select hybrid GPU path"
				);
				render(renderer, scene, grid);
				const auto pixels = read(device, renderer, grid);
				const auto fresh = compositor(device);
				render(fresh, native::prepareScene(sources.at(index), scale), grid);
				const auto reference = read(device, fresh, grid);
				require(
					pixels.bytes == reference.bytes,
					"incremental shadows differ from fresh full render"
				);
				if (index == 0) {
					initialMasks = renderer->statistics().shadows.maskBuilds;
					initialBlurs = renderer->statistics().shadows.blurPasses;
					require(
						initialMasks == 1 && initialBlurs == 2,
						"GPU mask and separable blur work missing"
					);
				}
				if (index == 1 || index == 2) {
					require(
						renderer->statistics().shadows.maskBuilds == initialMasks &&
							renderer->statistics().shadows.blurPasses == initialBlurs &&
							renderer->statistics().shadows.cacheHits >= index,
						"tint/offset edit rebuilt immutable blur mask"
					);
				}
				const auto before = renderer->statistics();
				render(renderer, scene, grid);
				require(
					renderer->statistics().shadows.maskBuilds == before.shadows.maskBuilds &&
						renderer->statistics().shadows.cacheHits == before.shadows.cacheHits &&
						renderer->statistics().compositedTiles == before.compositedTiles,
					"unchanged scene performed shadow work"
				);
				previous = scene;
				fresh->close();
			}
			const auto builds = renderer->statistics().shadows.maskBuilds;
			render(renderer, previous, grid, 2);
			require(
				renderer->statistics().shadows.maskBuilds > builds,
				"surface epoch did not retire shadow cache"
			);
			const auto ledger = renderer->resources().budget;
			renderer->close();
			pump([&] -> bool {
				const std::scoped_lock lock{ledger->mutex};
				return ledger->bytes == 0;
			});
			const std::scoped_lock lock{ledger->mutex};
			require(ledger->peak <= ledger->limit, "retired shadow ownership/budget leaked");
		}
	}
}

[[nodiscard]] auto referenceScene(bool inset, double blurRadius = 8)
	-> std::shared_ptr<const native::Scene> {
	const stana::gpu::detail::BoxContour contour{.bounds = {20, 20, 64, 40}};
	const stana::gpu::detail::Shadow shadow{
		.clip = contour,
		.caster = contour,
		.offset = {12, 6},
		.color = {0, 0, 0, 1},
		.blurRadius = blurRadius,
		.isInset = inset,
		.ink = inset ? contour.bounds : std::array<double, 4>{20, 14, 88, 64}
	};
	auto paint = std::make_shared<native::PaintSegments>();
	static std::uint64_t sequence = 0;
	paint->identity = ++sequence;
	paint->prefix.push_back(
		{.operation = native::PaintOperation::shadow,
			.x = shadow.ink.at(0),
			.y = shadow.ink.at(1),
			.width = shadow.ink.at(2),
			.height = shadow.ink.at(3),
			.shadow = shadow}
	);
	auto root = std::make_shared<native::SceneBranch>();
	root->id = {.document = 1, .serial = 1};
	root->generation = sequence;
	root->paint = paint;
	auto source = std::make_shared<native::Scene>();
	source->revision = sequence;
	source->size = {.width = referenceWidth, .height = referenceHeight};
	source->background = {.red = 1, .green = 1, .blue = 1};
	source->root = root;
	return source;
}

void gaussian(const std::shared_ptr<gpu::DeviceResources> &device) {
	for (const bool inset : {false, true}) {
		const auto source = referenceScene(inset);
		const auto renderer = compositor(device);
		const auto grid = native::makeTileGrid(source->size, 1);
		render(renderer, native::prepareScene(source, 1), grid);
		const auto pixels = read(device, renderer, grid);
		constexpr double sigma = 4;
		const auto gaussianSpan = [](double point, double low, double high) -> double {
			return (std::erf((point - low) / (std::numbers::sqrt2 * sigma)) -
					   std::erf((point - high) / (std::numbers::sqrt2 * sigma))) /
				2;
		};
		for (std::size_t y = 0; y < pixels.height; ++y) {
			for (std::size_t x = 0; x < pixels.width; ++x) {
				const double px = static_cast<double>(x) + 0.5;
				const double py = static_cast<double>(y) + 0.5;
				const bool inside = px >= 20 && px < 84 && py >= 20 && py < 60;
				const double mask = gaussianSpan(px, 32, 96) * gaussianSpan(py, 26, 66);
				const double expected = inset ? (inside ? 1 - mask : 0) : (inside ? 0 : mask);
				const double actual = 1 -
					static_cast<double>(
						std::to_integer<unsigned>(pixels.bytes.at((y * pixels.width + x) * 4))
					) / 255;
				if (std::abs(actual - expected) > blurTolerance) {
					std::cerr << "inset=" << inset << " x=" << x << " y=" << y
							  << " actual=" << actual << " expected=" << expected << '\n';
				}
				require(
					std::abs(actual - expected) <= blurTolerance,
					"shadow pixel exceeds Gaussian 5% tolerance / knockout contract"
				);
			}
		}
		require(
			renderer->statistics().cpuRasterPixels == 0,
			"production CPU shadow rasterization occurred"
		);
		const auto ready = renderer->readyPacket();
		const auto oversized = native::prepareScene(referenceScene(inset, 10000), 1);
		const auto failed =
			renderer->prepare(oversized, grid, 1, stana::gpu::detail::ColorMode::sdr);
		require(
			!failed && renderer->readyPacket() == ready,
			"capability failure published partial frame"
		);
		render(renderer, ready->scene, grid);
		renderer->close();
	}
	const auto zero = referenceScene(false, 0);
	const auto renderer = compositor(device);
	const auto grid = native::makeTileGrid(zero->size, 1);
	render(renderer, native::prepareScene(zero, 1), grid);
	require(renderer->statistics().shadows.blurPasses == 0, "zero blur encoded filtering");
	renderer->close();
}

void preview(const std::shared_ptr<gpu::DeviceResources> &device, const std::string &path) {
	const auto source = stana::testing::fixtures::platform::macos::shadowScenes().at(4);
	const auto renderer = compositor(device);
	const auto grid = native::makeTileGrid(source->size, 2);
	render(renderer, native::prepareScene(source, 2), grid);
	const auto pixels = read(device, renderer, grid);
	std::ofstream out{path, std::ios::binary};
	out << "P6\n" << pixels.width << ' ' << pixels.height << "\n255\n";
	for (std::size_t index = 0; index < pixels.bytes.size(); index += 4) {
		for (const std::size_t component : {2U, 1U, 0U}) {
			out.put(
				static_cast<char>(
					std::to_integer<unsigned char>(pixels.bytes.at(index + component))
				)
			);
		}
	}
	require(out.good(), "cannot save shadow preview");
	renderer->close();
}
} // namespace

auto main(int argc, char **argv) -> int {
	@autoreleasepool {
		try {
			const auto device = gpu::DeviceResources::get(MTLCreateSystemDefaultDevice());
			require(device.has_value(), device ? "" : device.error().message);
			incremental(*device);
			gaussian(*device);
			if (argc > 1) {
				preview(*device, std::string{*std::next(argv)});
			}
			return 0;
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	}
}
