module;

#include <algorithm>
#include <cmath>
#include <compare>
#include <numbers>
#include <optional>
#include <stdexcept>

module stana;
import :symbol_playback;

namespace stana::symbol {
namespace {
constexpr double cycleDuration = 1;
constexpr double bounceDuration = 0.6;
constexpr double settledPhase = 1;
constexpr double restScale = 1;
constexpr double pulseAmplitude = 0.08;
constexpr double bounceAmplitude = 0.12;

[[nodiscard]] auto duration(Effect effect) noexcept -> double {
	return effect == Effect::bounce ? bounceDuration : cycleDuration;
}
} // namespace

void PlaybackState::start(Effect effect, bool looping) {
	active_ = effect;
	looping_ = looping;
	elapsed_ = 0;
	resetClock_ = true;
}

void PlaybackState::stop() {
	active_.reset();
	elapsed_ = 0;
	resetClock_ = true;
}

void PlaybackState::reconcile(
	bool assetChanged,
	Playback playback,
	bool reducedMotion,
	bool visible,
	bool available
) {
	const bool initial = !initialized_;
	bool newRequest = false;
	if (playback.mode == Playback::Mode::once) {
		newRequest = lastRequest_ != playback.request;
		lastRequest_ = playback.request;
	}
	if (assetChanged || initial) {
		stop();
	}
	switch (playback.mode) {
	case Playback::Mode::none:
		stop();
		break;
	case Playback::Mode::loop:
		if (initial || assetChanged || playback != previous_ || (reduced_ && !reducedMotion) ||
			(!available_ && available)) {
			start(playback.effect, true);
		}
		break;
	case Playback::Mode::once:
		if (newRequest) {
			start(playback.effect, false);
		} else if (playback != previous_ || assetChanged) {
			stop();
		}
		break;
	}
	if (reducedMotion || !available) {
		stop();
	}
	const bool demand = active_.has_value() && visible && !reducedMotion && available;
	if (demand != needsFrame_ || visible != visible_) {
		resetClock_ = true;
	}
	needsFrame_ = demand;
	previous_ = playback;
	visible_ = visible;
	reduced_ = reducedMotion;
	available_ = available;
	initialized_ = true;
}

void PlaybackState::advance(double delta) {
	if (!needsFrame_ || !active_) {
		return;
	}
	const double length = duration(*active_);
	if (resetClock_) {
		resetClock_ = false;
	} else if (looping_) {
		const double wrapped = std::fmod(std::max(0.0, delta), length);
		elapsed_ = std::fmod(elapsed_ + wrapped, length);
	} else if (delta >= length - elapsed_) {
		stop();
		needsFrame_ = false;
	} else {
		elapsed_ += std::max(0.0, delta);
	}
}

auto PlaybackState::sample() const -> EffectSample {
	if (!active_) {
		return {};
	}
	const double length = duration(*active_);
	const double phase =
		looping_ ? std::fmod(elapsed_, length) / length : std::min(elapsed_ / length, settledPhase);
	return sample(*active_, phase);
}

auto PlaybackState::sample(Effect effect, double phase) -> EffectSample {
	const bool settled = std::is_eq(phase <=> 0.0) || std::is_eq(phase <=> settledPhase);
	const double wave = settled ? 0.0 : std::pow(std::sin(std::numbers::pi * phase), 2);
	switch (effect) {
	case Effect::rotate:
		return {
			.rotation = std::is_eq(phase <=> settledPhase) ? 0.0 : 2 * std::numbers::pi * phase
		};
	case Effect::pulse:
		return {.scale = restScale + pulseAmplitude * wave};
	case Effect::bounce:
		return {.verticalFraction = -bounceAmplitude * wave};
	}
	throw std::logic_error("Unknown symbol effect");
}
} // namespace stana::symbol
