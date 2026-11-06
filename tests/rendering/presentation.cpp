#include <array>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

import stana;

namespace {
void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

/// @brief Verifies completion ordering, stale epochs and finite presentation-history admission.
void evidenceMustJoin() {

	stana::rendering::PresentationTracker tracker;
	const auto first = tracker.begin(1, 1);
	require(first.has_value(), "first attempt");
	require(
		!tracker.reportGPU(*first, true) && !tracker.confirmed(),
		"GPU completion is not display"
	);
	require(!tracker.reportPresented(*first, 0) && !tracker.confirmed(), "zero is unknown");
	require(
		tracker.reportPresented(*first, 1) && tracker.confirmed() == first,
		"positive native evidence joins GPU success"
	);
	const auto second = tracker.begin(1, 2);
	require(second.has_value(), "second attempt");
	require(!tracker.reportPresented(*second, 1), "presentation callback may precede GPU");
	require(tracker.reportGPU(*second, true), "reverse callback order");
	tracker.reset(2);
	require(
		!tracker.reportGPU(*second, true) && !tracker.confirmed(),
		"stale surface callback ignored"
	);
	constexpr int maximumHistory = 64;
	for (int index = 0; index < maximumHistory; ++index) {
		require(tracker.begin(2, 1).has_value(), "bounded pending admission");
	}
	require(!tracker.begin(2, 1), "full history rejects more work");
}

/// @brief Failed/expired work frees bounded admission and cannot be revived by late callbacks.
void terminalAttemptsAndEpochs() {

	stana::rendering::PresentationTracker tracker;
	constexpr int beyondHistoryLimit = 128;
	for (int index = 0; index < beyondHistoryLimit; ++index) {
		const auto attempt = tracker.begin(1, 1);
		require(attempt.has_value(), "failure releases admission capacity");
		require(!tracker.reportPresented(*attempt, 1), "presentation alone cannot confirm");
		require(!tracker.reportGPU(*attempt, false), "failed GPU cannot confirm");
		require(!tracker.contains(*attempt) && tracker.pendingCount() == 0, "failure retires");
		require(!tracker.reportGPU(*attempt, true), "late success cannot revive failed work");
	}
	const auto expired = tracker.begin(1, 1);
	require(expired.has_value() && tracker.retire(*expired), "explicit deadline retirement");
	require(!tracker.retire(*expired), "retirement is idempotent");
	require(!tracker.reportPresented(*expired, 1), "late timestamp after deadline ignored");
	const auto valid = tracker.begin(1, 1);
	require(valid.has_value(), "new attempt after retirement");
	require(!tracker.reportPresented(*valid, 1), "positive timestamp retained until GPU");
	require(!tracker.reportPresented(*valid, 0), "unknown duplicate is not confirmation");
	require(tracker.reportGPU(*valid, true), "unknown duplicate cannot erase positive evidence");
	tracker.reset(2);
	for (const auto invalidEpoch : {0, 1, 2}) {
		bool rejected = false;
		try {
			tracker.reset(static_cast<unsigned int>(invalidEpoch));
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		require(rejected, "surface epochs cannot be reused or move backwards");
	}
	require(!tracker.begin(1, 1), "old-epoch submission rejected");
}

/// @brief Guards pointer routing against unconfirmed or explicitly unknown layout revisions.
void inputUsesConfirmedGeometry() {

	stana::testing::TestHost host;
	host.enablePresentationTesting();
	int activations = 0;
	const stana::reactive::Signal padding{stana::Insets::all(0)};
	host.mount(
		stana::ui::VStack{
			.layout = {.padding = padding},
			.children = stana::ui::children(
				stana::ui::Button{
					.text = "Activate",
					.events = {stana::ui::on(stana::ui::Event::click, [&] -> void {
						++activations;
					})}
				}
			)
		}
	);
	const stana::Point oldPoint{.x = 1, .y = 1};
	host.click(oldPoint);
	require(activations == 0, "initial geometry is not yet displayed");
	const auto initial = host.render(false);
	host.reportGPUCompletion(initial, true);
	host.click(oldPoint);
	require(activations == 0, "GPU-only input remains gated");
	host.reportPresentation(initial, 1);
	host.click(oldPoint);
	require(activations == 1, "confirmed first click");
	constexpr double shiftedPadding = 100;
	padding.set(stana::Insets::all(shiftedPadding));
	host.click(oldPoint);
	require(activations == 2, "pending frames keep last-confirmed location");
	const auto shifted = host.render(false);
	host.reportPresentation(shifted, 0);
	host.click(oldPoint);
	require(activations == 2, "unknown changed region blocks actions");
	host.reportGPUCompletion(shifted, true);
	host.reportPresentation(shifted, 2);
	host.click({.x = shiftedPadding + 1, .y = shiftedPadding + 1});
	constexpr int expectedActivations = 3;
	require(activations == expectedActivations, "newly confirmed location accepts input");
}

/// @brief Replaced surfaces and forged/late negative feedback must not poison current input.
void staleFeedbackAndRecovery() {

	stana::testing::TestHost host;
	host.enablePresentationTesting();
	int activations = 0;
	const stana::reactive::Signal padding{stana::Insets::all(0)};
	host.mount(
		stana::ui::VStack{
			.layout = {.padding = padding},
			.children = stana::ui::children(
				stana::ui::Button{
					.text = "Activate",
					.events = {stana::ui::on(stana::ui::Event::click, [&] -> void {
						++activations;
					})}
				}
			)
		}
	);
	static_cast<void>(host.render());
	constexpr double shiftedPadding = 100;
	const stana::Point oldPoint{.x = 1, .y = 1};
	const stana::Point newPoint{.x = shiftedPadding + 1, .y = shiftedPadding + 1};
	padding.set(stana::Insets::all(shiftedPadding));
	const auto replaced = host.render(false);
	host.replaceSurface();
	host.reportGPUCompletion(replaced, false);
	host.reportPresentation(replaced, 0);
	host.abandonPresentation(replaced);
	host.click(oldPoint);
	require(activations == 1, "stale negative evidence cannot poison last-known geometry");
	host.reportPresentation(replaced, 1);
	host.reportGPUCompletion(replaced, true);
	host.click(newPoint);
	require(activations == 1, "stale positive evidence cannot publish replacement geometry");
	const auto pending = host.render(false);
	auto forged = pending;
	++forged.sequence;
	host.reportGPUCompletion(forged, false);
	host.reportPresentation(forged, 0);
	host.click(oldPoint);
	require(activations == 2, "unregistered retry identity cannot change input gates");
	host.reportPresentation(pending, std::numeric_limits<double>::quiet_NaN());
	host.click(oldPoint);
	require(activations == 2, "non-finite presentation evidence gates changed regions");
	host.reportGPUCompletion(pending, false);
	host.reportGPUCompletion(pending, true);
	host.reportPresentation(pending, 1);
	host.click(newPoint);
	require(activations == 2, "failed attempt cannot recover through late success");
	const auto retry = host.render(false);
	host.reportPresentation(retry, 1);
	host.reportGPUCompletion(retry, true);
	host.click(newPoint);
	require(activations == 3, "fresh retry recovers current geometry in reverse callback order");
	constexpr int beyondHistoryLimit = 128;
	for (int index = 0; index < beyondHistoryLimit; ++index) {
		host.abandonPresentation(host.render(false));
	}
	static_cast<void>(host.render());
	host.click(newPoint);
	require(activations == 4, "expired attempts do not exhaust future admission");
	host.close();
	host.reportGPUCompletion(retry, false);
	host.reportPresentation(retry, 0);
	require(host.nodes().empty(), "feedback after closure cannot resurrect content");
}

/// @brief P01-P09: exact bounded expected sets and independent observation ordering.
void compositeEvidence() {

	stana::rendering::PresentationTracker tracker;
	constexpr std::array tiles{
		stana::rendering::TileId{0},
		stana::rendering::TileId{2},
		stana::rendering::TileId{3}
	};
	const auto first = tracker.begin(1, 1, tiles);
	require(first.has_value(), "composite admission");
	require(!tracker.reportGPU(*first, true), "GPU alone cannot confirm composite");
	require(
		!tracker.reportPresented(
			*first,
			stana::rendering::TilePresentation{.tile = tiles.front(), .timestamp = 1}
		),
		"first tile alone"
	);
	require(
		!tracker.reportPresented(
			*first,
			stana::rendering::TilePresentation{.tile = stana::rendering::TileId{1}, .timestamp = 1}
		),
		"unknown tile ignored"
	);
	require(
		!tracker.reportPresented(
			*first,
			stana::rendering::TilePresentation{.tile = tiles.at(1), .timestamp = 1}
		),
		"missing last tile"
	);
	require(
		!tracker.reportPresented(
			*first,
			stana::rendering::TilePresentation{.tile = tiles.front(), .timestamp = 0}
		),
		"invalid duplicate retains positive"
	);
	require(
		tracker.reportPresented(
			*first,
			stana::rendering::TilePresentation{.tile = tiles.back(), .timestamp = 1}
		),
		"all required times join"
	);
	const auto invalidTimes = tracker.begin(1, 2, tiles);
	require(invalidTimes.has_value(), "invalid tile times admission");
	require(!tracker.reportGPU(*invalidTimes, true), "GPU alone remains unconfirmed");
	for (const auto time :
		{0.0,
			-1.0,
			std::numeric_limits<double>::quiet_NaN(),
			std::numeric_limits<double>::infinity()}) {
		require(
			!tracker.reportPresented(
				*invalidTimes,
				stana::rendering::TilePresentation{.tile = tiles.front(), .timestamp = time}
			),
			"invalid typed time is never evidence"
		);
	}
	require(tracker.retire(*invalidTimes), "retire invalid-time attempt");
	const auto reverse = tracker.begin(1, 2, tiles);
	require(reverse.has_value(), "reverse composite");
	for (const auto tile : tiles) {
		require(
			!tracker.reportPresented(
				*reverse,
				stana::rendering::TilePresentation{.tile = tile, .timestamp = 1}
			),
			"times before GPU"
		);
	}
	require(tracker.reportGPU(*reverse, true), "GPU joins complete tile set");
	constexpr std::array duplicate{stana::rendering::TileId{0}, stana::rendering::TileId{0}};
	constexpr std::size_t oversizedCount = 1025;
	const std::vector<stana::rendering::TileId> oversized(
		oversizedCount,
		stana::rendering::TileId{0}
	);
	require(
		!tracker.begin(1, 3, oversized),
		"oversized expected set rejects before slot admission"
	);
	constexpr std::array outOfRange{stana::rendering::TileId{1024}};
	constexpr std::uint64_t rejectedEpoch = 100;
	require(
		!tracker.begin(rejectedEpoch, 3, std::span<const stana::rendering::TileId>{}) &&
			!tracker.begin(rejectedEpoch, 3, duplicate) &&
			!tracker.begin(rejectedEpoch, 3, outOfRange),
		"P04 invalid sets reject before epoch mutation"
	);
	require(tracker.begin(1, 3, tiles).has_value(), "invalid set did not advance epoch");
	tracker.reset(2);
	require(
		!tracker.reportPresented(
			*reverse,
			stana::rendering::TilePresentation{.tile = tiles.back(), .timestamp = 1}
		),
		"stale tile evidence ignored"
	);
}

/// @brief P10-P12/P24: composite pins survive >64 commits and pending geometry is gated.
void compositePins() {

	stana::testing::TestHost host;
	int clicks = 0;
	const stana::reactive::Signal padding{stana::Insets::all(0)};
	const stana::reactive::Signal color{stana::Color::black()};
	host.mount(
		stana::ui::VStack{
			.layout = {.padding = padding},
			.children = stana::ui::children(
				stana::ui::Button{
					.text = "activate",
					.textStyle = {.foreground = color},
					.events = {stana::ui::on(stana::ui::Event::click, [&] -> void {
						++clicks;
					})}
				}
			)
		}
	);
	static_cast<void>(host.render());
	constexpr double shifted = 100;
	padding.set(stana::Insets::all(shifted));
	constexpr std::array tiles{stana::rendering::TileId{0}, stana::rendering::TileId{1}};
	const auto attempt = host.renderComposite(tiles);
	host.click({.x = 1, .y = 1});
	host.click({.x = shifted + 1, .y = shifted + 1});
	require(clicks == 0, "P10 old/new gated before GPU exposes pixels");
	constexpr unsigned publications = 128;
	for (unsigned index = 0; index < publications; ++index) {
		color.set(index % 2 == 0 ? stana::Color::accent() : stana::Color::black());
	}
	host.reportTilePresentation(attempt, {.tile = stana::rendering::TileId{0}, .timestamp = 1});
	host.reportGPUCompletion(attempt, true);
	host.click({.x = shifted + 1, .y = shifted + 1});
	require(clicks == 0, "P03 missing one tile still gates");
	host.reportTilePresentation(attempt, {.tile = stana::rendering::TileId{1}, .timestamp = 1});
	host.click({.x = shifted + 1, .y = shifted + 1});
	require(clicks == 1, "P12 exact pinned geometry survives publication churn");
	for (unsigned index = 0; index < publications; ++index) {
		const auto next = host.renderComposite(tiles);
		host.abandonPresentation(next);
	}
	const auto recovered = host.renderComposite(tiles);
	host.reportGPUCompletion(recovered, true);
	for (const auto tile : tiles) {
		host.reportTilePresentation(recovered, {.tile = tile, .timestamp = 1});
	}
	host.click({.x = shifted + 1, .y = shifted + 1});
	require(clicks == 2, "P24 pin retirement stays bounded");
}

/// @brief P11: an older target can install its pin but cannot clear newer uncertainty.
void olderConfirmationKeepsNewerGate() {

	const stana::reactive::Signal padding{stana::Insets::all(0)};
	stana::testing::TestHost host;
	int clicks = 0;
	host.mount(
		stana::ui::VStack{
			.layout = {.padding = padding},
			.children = stana::ui::children(
				stana::ui::Button{
					.text = "route",
					.events = {stana::ui::on(stana::ui::Event::click, [&] -> void {
						++clicks;
					})}
				}
			)
		}
	);
	static_cast<void>(host.render());
	constexpr double firstShift = 50;
	constexpr double secondShift = 100;
	constexpr std::array tile{stana::rendering::TileId{0}};
	padding.set(stana::Insets::all(firstShift));
	const auto older = host.renderComposite(tile);
	padding.set(stana::Insets::all(secondShift));
	const auto newer = host.renderComposite(tile);
	host.abandonPresentation(newer);
	host.reportGPUCompletion(older, true);
	host.reportTilePresentation(older, {.tile = tile.front(), .timestamp = 1});
	host.click({.x = firstShift + 1, .y = firstShift + 1});
	require(clicks == 0, "older confirmation cannot clear newer gate");
	const auto current = host.renderComposite(tile);
	host.reportGPUCompletion(current, true);
	host.reportTilePresentation(current, {.tile = tile.front(), .timestamp = 1});
	host.click({.x = secondShift + 1, .y = secondShift + 1});
	require(clicks == 1, "newest confirmation clears its covered uncertainty");
}

} // namespace

auto main() -> int {
	try {
		olderConfirmationKeepsNewerGate();
		compositeEvidence();
		compositePins();
		evidenceMustJoin();
		terminalAttemptsAndEpochs();
		inputUsesConfirmedGeometry();
		staleFeedbackAndRecovery();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
