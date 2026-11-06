#include <compare>
#include <iostream>
#include <stdexcept>
#include <string>

import stana;

namespace {
constexpr double firstTimestamp = 1;
constexpr double firstTarget = 1.6;
constexpr double secondTimestamp = 1.5;
constexpr double halfSecond = 0.5;
constexpr int oneSample = 1;
constexpr int twoSamples = 2;

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] auto same(double left, double right) -> bool {
	return std::is_eq(left <=> right);
}

struct FrameProbe {
	stana::reactive::Signal<double> elapsed;
	stana::reactive::Signal<int> samples;
	stana::reactive::Signal<bool> active;

	[[nodiscard]] auto render(stana::ui::ComponentContext &context) const -> stana::ui::Empty {
		static_cast<void>(context.onFrame(
			[recorded = elapsed, counted = samples](stana::ui::FrameTime frame) -> void {
				counted.set(counted.get() + 1);
				recorded.set(frame.elapsed);
			},
			active
		));
		return {};
	}
};

void firstSampleReportsZeroElapsed() {
	const stana::reactive::Signal elapsed{0.0};
	const stana::reactive::Signal samples{0};
	const stana::reactive::Signal active{true};
	stana::testing::TestHost host;
	host.mount(FrameProbe{.elapsed = elapsed, .samples = samples, .active = active});
	host.tickFrames({.timestamp = firstTimestamp, .targetTimestamp = firstTarget});
	require(samples.get() == oneSample && same(elapsed.get(), 0.0), "first elapsed is zero");
	host.tickFrames({.timestamp = secondTimestamp, .targetTimestamp = secondTimestamp});
	require(
		samples.get() == twoSamples && same(elapsed.get(), halfSecond),
		"later samples report the timestamp delta"
	);
}

void inactiveSkipsAndReactivationResets() {
	const stana::reactive::Signal elapsed{0.0};
	const stana::reactive::Signal samples{0};
	const stana::reactive::Signal active{true};
	stana::testing::TestHost host;
	host.mount(FrameProbe{.elapsed = elapsed, .samples = samples, .active = active});
	host.tickFrames({.timestamp = firstTimestamp, .targetTimestamp = firstTarget});
	active.set(false);
	host.flush();
	host.tickFrames({.timestamp = secondTimestamp, .targetTimestamp = secondTimestamp});
	require(samples.get() == oneSample, "inactive registration does not sample");
	active.set(true);
	host.flush();
	host.tickFrames({.timestamp = firstTimestamp + halfSecond, .targetTimestamp = firstTarget});
	require(
		samples.get() == twoSamples && same(elapsed.get(), 0.0),
		"reactivation starts a new interval"
	);
}

void clocksAdvanceIndependently() {
	const stana::reactive::Signal firstElapsed{0.0};
	const stana::reactive::Signal firstSamples{0};
	const stana::reactive::Signal firstActive{true};
	const stana::reactive::Signal secondElapsed{0.0};
	const stana::reactive::Signal secondSamples{0};
	const stana::reactive::Signal secondActive{true};
	stana::testing::TestHost host;
	host.mount(
		stana::ui::VStack{
			.children = stana::ui::children(
				FrameProbe{.elapsed = firstElapsed, .samples = firstSamples, .active = firstActive},
				FrameProbe{
					.elapsed = secondElapsed,
					.samples = secondSamples,
					.active = secondActive
				}
			)
		}
	);
	host.tickFrames({.timestamp = firstTimestamp, .targetTimestamp = firstTarget});
	host.tickFrames({.timestamp = secondTimestamp, .targetTimestamp = secondTimestamp});
	secondActive.set(false);
	host.flush();
	host.tickFrames(
		{.timestamp = secondTimestamp + halfSecond, .targetTimestamp = secondTimestamp}
	);
	require(
		firstSamples.get() == twoSamples + 1 && same(firstElapsed.get(), halfSecond),
		"one clock keeps its own interval"
	);
	require(secondSamples.get() == twoSamples, "the other clock stays paused");
}
} // namespace

auto main() -> int {
	try {
		firstSampleReportsZeroElapsed();
		inactiveSkipsAndReactivationResets();
		clocksAdvanceIndependently();
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
