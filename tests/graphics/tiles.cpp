#include "../../src/platform/tiles.hpp"
#include "../../src/platform/scheduler.hpp"
#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const std::string &message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

template <typename F> void rejects(F operation) {
	bool rejected = false;
	try {
		operation();
	} catch (const std::exception &) {
		rejected = true;
	}
	require(rejected, "invalid arithmetic accepted");
}

void grids() {
	using namespace stana::native;
	constexpr std::array sizes{
		PixelSize{.width = 512, .height = 512},
		PixelSize{.width = 513, .height = 512},
		PixelSize{.width = 512, .height = 513},
		PixelSize{.width = 1025, .height = 769},
		PixelSize{.width = 3840, .height = 2160},
		PixelSize{.width = 7680, .height = 4320}
	};
	constexpr std::array<std::size_t, 6> counts{1, 2, 2, 6, 40, 135};
	for (std::size_t index = 0; index < sizes.size(); ++index) {
		const auto size = sizes.at(index);
		const auto grid = makeTileGrid(
			{.width = static_cast<double>(size.width), .height = static_cast<double>(size.height)},
			1
		);
		require(grid.tiles.size() == counts.at(index), "tile count");
		std::size_t payload = 0;
		std::size_t allocated = 0;
		for (const auto &tile : grid.tiles) {
			require(
				tile.pixels.width > 0 && tile.pixels.height > 0 &&
					tile.bytesPerRow % rasterRowAlignment == 0,
				"nonempty aligned tile"
			);
			payload += tile.pixels.width * tile.pixels.height;
			allocated += tile.allocationBytes;
		}
		require(
			payload == size.width * size.height && allocated == grid.rasterBytes,
			"exact viewport and budget coverage"
		);
		rejects([&] -> void {
			static_cast<void>(makeTileGrid(
				{.width = static_cast<double>(size.width),
					.height = static_cast<double>(size.height)},
				1,
				{.ownedBytes = maximumRasterBytes}
			));
		});
	}
	constexpr double budgetSide = 8192;
	require(
		makeTileGrid({.width = budgetSide, .height = budgetSide}, 1).rasterBytes ==
			maximumRasterBytes,
		"exact 256 MiB arithmetic boundary"
	);
	require(
		makeTileGrid(
			{.width = 1, .height = 1},
			1,
			{.ownedBytes = maximumRasterBytes - rasterRowAlignment}
		)
				.rasterBytes == rasterRowAlignment,
		"current+retired plus one aligned edge row fits exactly"
	);
	rejects([] -> void {
		static_cast<void>(makeTileGrid(
			{.width = 1, .height = 1},
			1,
			{.ownedBytes = maximumRasterBytes - rasterRowAlignment + 1}
		));
	});

	require(makeTileGrid({.width = 0, .height = 1}, 1).tiles.empty(), "zero viewport");
	for (const auto invalid :
		{-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
		rejects([&] -> void {
			static_cast<void>(makeTileGrid({.width = invalid, .height = 1}, 1));
		});
	}
	rejects([] -> void {
		static_cast<void>(makeTileGrid({.width = 1, .height = 1}, 0));
	});
	rejects([] -> void {
		static_cast<void>(
			makeTileGrid({.width = static_cast<double>(maximumDimension) + 1, .height = 1}, 1)
		);
	});
	rejects([] -> void {
		static_cast<void>(makeTileGrid({.width = 1, .height = 1}, 1, {.maximumBufferLength = 1}));
	});
	rejects([] -> void {
		static_cast<void>(alignedRasterRow(std::numeric_limits<std::size_t>::max()));
	});
	const auto grid = makeTileGrid({.width = 1025, .height = 769}, 1);
	TileMask mask;
	selectTiles(
		grid,
		1,
		{.size =
				{.width = static_cast<double>(tilePixels),
					.height = static_cast<double>(tilePixels)}},
		mask
	);
	require(mask.count() == 1, "exact boundary excludes next tile");
	selectTiles(
		grid,
		1,
		{.origin = {.x = static_cast<double>(tilePixels), .y = static_cast<double>(tilePixels)},
			.size = {.width = 1, .height = 1}},
		mask
	);
	require(mask.count() == 2 && mask.test(4), "disjoint damage union");
}

/// @brief P14: the exact production acquisition helper releases a partial draft before admission.
void allOrNoneOwners() {
	using namespace stana::native;

	struct Drawable {
		std::shared_ptr<unsigned> live;

		explicit Drawable(std::shared_ptr<unsigned> count) : live(std::move(count)) { ++*live; }

		Drawable(const Drawable &) = delete;
		Drawable(Drawable &&) = delete;
		auto operator=(const Drawable &) -> Drawable & = delete;
		auto operator=(Drawable &&) -> Drawable & = delete;

		~Drawable() { --*live; }
	};

	const auto live = std::make_shared<unsigned>(0);
	constexpr std::array expected{TileId{0}, TileId{1}, TileId{2}, TileId{3}};
	unsigned calls = 0;
	const auto partial = acquireAll(expected, [&](TileId tile) -> std::shared_ptr<Drawable> {
		++calls;
		if (tile.value == 2) {
			return {};
		}
		return std::make_shared<Drawable>(live);
	});
	require(
		!partial && *live == 0 && calls == 3,
		"partial draft owners released, later tiles never acquired"
	);
	auto complete = acquireAll(expected, [&](TileId) -> std::shared_ptr<Drawable> {
		return std::make_shared<Drawable>(live);
	});
	require(
		complete.has_value() && *live == expected.size(),
		"complete draft owns every expected tile"
	);
	complete.reset();
	require(*live == 0, "dropping unsubmitted complete draft releases all owners");
}

/// @brief P13-P19/P21-P23 production state transitions with an injected clock, no sleeps.
void scheduler() {
	using namespace stana::native;
	using namespace std::chrono_literals;
	FrameScheduler schedule;
	const FrameIdentity first{.epoch = 1, .sequence = 1, .sceneRevision = 1};
	require(schedule.canBuild(true), "initial build");
	schedule.submitted(first, 0ms);
	require(!schedule.canBuild(true), "one active batch backpressure");
	require(
		schedule.expire(FrameScheduler::confirmationDeadline) == FrameAction::none &&
			schedule.phase() == FramePhase::draining,
		"P18 timeout drains GPU owner"
	);
	require(
		schedule.gpuFinished(first, true, FrameScheduler::confirmationDeadline + 1ms) ==
			FrameAction::replaceGrid,
		"stale display completion still releases drain"
	);
	schedule.gridInstalled();
	require(schedule.canBuild(true), "recovery build");
	require(
		schedule.acquisitionFailed(true, FrameScheduler::confirmationDeadline + 2ms) ==
				FrameAction::retry &&
			!schedule.canBuild(true),
		"one delayed nil retry"
	);
	require(
		schedule.expire(
			FrameScheduler::confirmationDeadline + 2ms + FrameScheduler::drawableRetryDelay
		) == FrameAction::retry &&
			schedule.canBuild(true),
		"retry deadline"
	);
	require(
		schedule.acquisitionFailed(
			true,
			FrameScheduler::confirmationDeadline + 2ms + FrameScheduler::drawableRetryDelay
		) == FrameAction::failure,
		"P16 recovery nil becomes terminal"
	);
	FrameScheduler occluded;
	require(!occluded.canBuild(false) && !occluded.recoveryConsumed(), "occlusion no recovery");
	require(
		occluded.canBuild(true) && occluded.acquisitionFailed(false, 0ms) == FrameAction::none &&
			!occluded.recoveryConsumed(),
		"P15 visibility changed while acquiring"
	);
	require(occluded.canBuild(true), "visibility wake");
	occluded.submitted(first, 0ms);
	require(
		occluded.gpuFinished(first, true, 1ms) == FrameAction::none && occluded.confirm(first),
		"joined confirmation resets allowance"
	);
	require(occluded.canBuild(true), "next target");
	occluded.submitted(first, 2ms);
	require(
		occluded.invalidateSurface(3ms) == FrameAction::none && !occluded.confirm(first),
		"P23 no stale transaction after surface replacement"
	);
	require(
		occluded.invalidateSurface(FrameScheduler::recoveryDrainDeadline / 2) == FrameAction::none,
		"repeated resize does not restart GPU drain"
	);
	require(
		occluded.expire(FrameScheduler::recoveryDrainDeadline + 3ms) == FrameAction::failure,
		"bounded drain failure"
	);
	occluded.close();
	require(!occluded.canBuild(true) && !occluded.matches(first), "P21 closed cannot present");

	FrameScheduler unknown;
	require(unknown.canBuild(true), "unknown-timestamp build");
	unknown.submitted(first, 0ms);
	require(unknown.unknownPresentation(1ms) == FrameAction::none, "zero time waits for GPU");
	require(
		unknown.gpuFinished(first, true, 1ms) == FrameAction::none,
		"GPU success still needs display"
	);
	require(
		unknown.unknownPresentation(2ms) == FrameAction::retry &&
			unknown.phase() == FramePhase::idle && !unknown.hasActive() &&
			!unknown.recoveryConsumed(),
		"unknown timestamp retries the same grid"
	);
	require(!unknown.canBuild(true), "fast unknown retry is delayed");
	require(
		unknown.expire(2ms + FrameScheduler::drawableRetryDelay) == FrameAction::retry &&
			unknown.canBuild(true),
		"unknown retry wake"
	);
	const auto afterRetry = 2ms + FrameScheduler::drawableRetryDelay;
	unknown.submitted(first, afterRetry);
	require(unknown.gpuFinished(first, true, afterRetry) == FrameAction::none, "retry GPU");
	require(
		unknown.confirm(first) && unknown.phase() == FramePhase::idle &&
			!unknown.recoveryConsumed(),
		"positive evidence still confirms and resets unknown backoff"
	);
	auto submittedAt = afterRetry;
	for (unsigned index = 0; index < FrameScheduler::fastUnknownPresentationRetries; ++index) {
		require(unknown.canBuild(true), "fast unknown attempts remain buildable");
		submittedAt += FrameScheduler::drawableRetryDelay;
		unknown.submitted(first, submittedAt);
		require(
			unknown.gpuFinished(first, true, submittedAt) == FrameAction::none,
			"fast unknown GPU"
		);
		require(
			unknown.unknownPresentation(submittedAt) == FrameAction::retry &&
				unknown.phase() != FramePhase::failed,
			"fast unknown retries never fail the host"
		);
		require(
			unknown.expire(submittedAt + FrameScheduler::drawableRetryDelay) == FrameAction::retry,
			"fast unknown wake"
		);
	}
	require(unknown.canBuild(true), "backoff attempt can build");
	submittedAt += FrameScheduler::confirmationDeadline;
	unknown.submitted(first, submittedAt);
	require(unknown.gpuFinished(first, true, submittedAt) == FrameAction::none, "backoff GPU");
	require(
		unknown.unknownPresentation(submittedAt) == FrameAction::retry && !unknown.canBuild(true) &&
			unknown.phase() != FramePhase::failed,
		"further zeros back off without replacing the grid"
	);
	require(
		unknown.expire(submittedAt + FrameScheduler::confirmationDeadline) == FrameAction::retry &&
			unknown.canBuild(true),
		"backoff wake still retries"
	);
}
} // namespace

auto main() -> int {
	try {
		grids();
		allOrNoneOwners();
		scheduler();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
