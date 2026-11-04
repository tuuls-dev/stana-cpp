#ifndef STANA_SCHEDULER_HPP
#define STANA_SCHEDULER_HPP
#include "tiles.hpp"
#include <chrono>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace stana::native {
/// @brief Production all-or-none acquisition. Failure releases every draft owner before admission.
/// Tests use counted owners; native code uses the same function with CAMetalDrawable owners.
template <typename Acquire>
[[nodiscard]] auto acquireAll(std::span<const TileId> expected, Acquire acquire)
	-> std::optional<std::vector<std::invoke_result_t<Acquire, TileId>>> {
	using Drawable = std::invoke_result_t<Acquire, TileId>;
	if (expected.empty() || expected.size() > maximumTiles) {
		return std::nullopt;
	}
	std::vector<Drawable> owners;
	owners.reserve(expected.size());
	for (const auto tile : expected) {
		auto drawable = acquire(tile);
		if (drawable == nullptr) {
			return std::nullopt;
		}
		owners.push_back(std::move(drawable));
	}
	return owners;
}
enum class FramePhase : std::uint8_t {
	idle,
	building,
	awaitingEvidence,
	occluded,
	draining,
	recovering,
	closed,
	failed
};
enum class FrameAction : std::uint8_t { none, retry, replaceGrid, failure };

/// @brief Production single-batch state machine with injected monotonic time.
/// Owns decisions only; the adapter owns scenes, layers, timers and GPU-read resources.
class FrameScheduler {
  public:
	using Time = std::chrono::milliseconds;
	static constexpr Time drawableRetryDelay{16};
	static constexpr Time confirmationDeadline{1000};
	static constexpr Time recoveryDrainDeadline{1000};
	/// @brief Same-grid retries at drawableRetryDelay before backing off to confirmationDeadline.
	static constexpr unsigned fastUnknownPresentationRetries = 8;

	[[nodiscard]] auto phase() const -> FramePhase { return phase_; }

	[[nodiscard]] auto active() const -> std::optional<FrameIdentity> { return active_; }

	[[nodiscard]] auto hasActive() const -> bool { return active_.has_value(); }

	[[nodiscard]] auto recoveryConsumed() const -> bool { return recoveryConsumed_; }

	[[nodiscard]] auto canBuild(bool visible) -> bool;
	[[nodiscard]] auto acquisitionFailed(bool visible, Time now) -> FrameAction;
	void submitted(FrameIdentity, Time now);
	[[nodiscard]] auto gpuFinished(FrameIdentity, bool success, Time now) -> FrameAction;
	[[nodiscard]] auto confirm(FrameIdentity) -> bool;
	/// @brief GPU succeeded but the proof tile's timestamp is unknown/zero: retry the same grid.
	/// Does not consume surface recovery or fail the host; Core Animation often reports 0 on the
	/// first present after attach.
	[[nodiscard]] auto unknownPresentation(Time now) -> FrameAction;
	[[nodiscard]] auto expire(Time now) -> FrameAction;
	[[nodiscard]] auto invalidateSurface(Time now) -> FrameAction;
	[[nodiscard]] auto wakeDelay(Time now) const -> std::optional<Time>;
	void gridInstalled();
	void noDemand();
	void close();
	[[nodiscard]] auto matches(FrameIdentity) const -> bool;

  private:
	FramePhase phase_ = FramePhase::idle;
	std::optional<FrameIdentity> active_;
	std::optional<Time> deadline_;
	std::optional<Time> retryAt_;
	bool gpuFinished_ = false;
	bool recoveryConsumed_ = false;
	bool retriedAcquisition_ = false;
	unsigned unknownPresentationRetries_ = 0;
	[[nodiscard]] auto recover(Time now) -> FrameAction;
};
} // namespace stana::native
#endif
