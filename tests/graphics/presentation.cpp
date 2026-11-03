#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

import stana;
import stana.testing;

namespace {
void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

/// @brief Verifies completion ordering, stale epochs and finite presentation-history admission.
void evidenceMustJoin() {
	using namespace stana;
	PresentationTracker tracker;
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
	using namespace stana;
	PresentationTracker tracker;
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
	using namespace stana;
	TestHost host;
	host.enablePresentationTesting();
	int activations = 0;
	const Signal padding{Insets::all(0)};
	host.mount(
		VStack{
			.layout = {.padding = padding},
			.children = children(Button{.text = "Activate", .onClick = [&] -> void {
											++activations;
										}})
		}
	);
	const Point oldPoint{.x = 1, .y = 1};
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
	padding.set(Insets::all(shiftedPadding));
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
	using namespace stana;
	TestHost host;
	host.enablePresentationTesting();
	int activations = 0;
	const Signal padding{Insets::all(0)};
	host.mount(
		VStack{
			.layout = {.padding = padding},
			.children = children(Button{.text = "Activate", .onClick = [&] -> void {
											++activations;
										}})
		}
	);
	static_cast<void>(host.render());
	constexpr double shiftedPadding = 100;
	const Point oldPoint{.x = 1, .y = 1};
	const Point newPoint{.x = shiftedPadding + 1, .y = shiftedPadding + 1};
	padding.set(Insets::all(shiftedPadding));
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
} // namespace

auto main() -> int {
	try {
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
