module;

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>

export module stana:rendering_presentation;
import :primitives;

export namespace stana::rendering {
/// @brief Identifies one presentation attempt for one scene on one native surface.
/// Revision alone is insufficient: retries and replacement drawable queues may
/// submit the same scene again. All feedback must match the full identity.
struct PresentationAttempt {
	std::uint64_t epoch = 0; ///< Surface generation; replacement invalidates earlier feedback.
	std::uint64_t sequence =
		0; ///< Never-reused attempt identity, including presentation-only retries.
	std::uint64_t sceneRevision = 0; ///< Logical scene submitted by this attempt.
	auto operator<=>(const PresentationAttempt &) const = default;
};

/// @brief Strong physical tile ordinal, scoped to a presentation epoch.
struct TileId {
	std::uint32_t value = 0;
	auto operator<=>(const TileId &) const = default;
};

/// @brief One exact tile's native display observation; invalid time is not proof.
/// Timestamps use PresentationClock and are not interchangeable with FrameClock.
struct TilePresentation {
	TileId tile{};
	PresentationTimePoint timestamp{};
};

/// @brief Joins GPU completion with positive native presentation evidence.
///
/// Deterministic hosts inject the same two independent observations a native
/// drawable supplies. GPU completion, offscreen capture and zero timestamps alone
/// never establish displayed geometry. Late feedback from a replaced epoch is
/// ignored, and a bounded pending history prevents unbounded callback ownership.
class PresentationTracker {
	static constexpr std::size_t maximumExpectedTiles = 1024;

  public:
	/// @brief Registers an attempt before its asynchronous completion callbacks run.
	/// @return A unique identity, or failure when the epoch is stale/history is full.
	[[nodiscard]] auto begin(std::uint64_t epoch, std::uint64_t sceneRevision)
		-> std::expected<PresentationAttempt, Error> {
		constexpr std::array tiles{TileId{0}};
		return begin(epoch, sceneRevision, tiles);
	}

	/// @brief Owns and validates the complete expected tile set before mutating admission state.
	[[nodiscard]] auto
	begin(std::uint64_t epoch, std::uint64_t sceneRevision, std::span<const TileId> tiles)
		-> std::expected<PresentationAttempt, Error> {
		std::bitset<maximumExpectedTiles> expected;
		if (tiles.empty() || tiles.size() > expected.size()) {
			return std::unexpected(Error::invalidValue);
		}
		for (const auto tile : tiles) {
			if (tile.value >= expected.size() || expected.test(tile.value)) {
				return std::unexpected(Error::invalidValue);
			}
			expected.set(tile.value);
		}

		if (epoch == 0 || epoch < epoch_) {
			return std::unexpected(Error::invalidValue);
		}
		if (epoch > epoch_) {
			reset(epoch);
		}
		if (pending_.size() >= historyLimit) {
			return std::unexpected(Error::nativeFailure);
		}
		const PresentationAttempt attempt{
			.epoch = epoch,
			.sequence = ::stana::detail::nextIdentity(),
			.sceneRevision = sceneRevision
		};
		pending_.emplace(attempt.sequence, State{.attempt = attempt, .expected = expected});
		return attempt;
	}

	/// @brief Supplies GPU success/failure for an exact registered attempt.
	/// @return True only when this observation newly confirms displayed geometry.
	[[nodiscard]] auto reportGPU(PresentationAttempt attempt, bool success) -> bool {
		if (auto *state = find(attempt)) {
			if (!success) {
				static_cast<void>(retire(attempt));
				return false;
			}
			state->gpuFinished = true;
			return confirm(*state);
		}
		return false;
	}

	/// @brief Supplies the native drawable's timestamp; zero means unconfirmed/dropped.
	[[nodiscard]] auto reportPresented(PresentationAttempt attempt, PresentationTimePoint timestamp)
		-> bool {
		return reportPresented(
			attempt,
			TilePresentation{.tile = TileId{0}, .timestamp = timestamp}
		);
	}

	/// @brief Joins a first finite positive observation for an expected tile only.
	[[nodiscard]] auto reportPresented(PresentationAttempt attempt, TilePresentation observation)
		-> bool {
		if (auto *state = find(attempt); state != nullptr && contains(attempt, observation.tile)) {
			const double seconds = observation.timestamp.time_since_epoch().count();
			if (std::isfinite(seconds) && seconds > 0) {
				state->presented.set(observation.tile.value);
			}
			return confirm(*state);
		}
		return false;
	}

	[[nodiscard]] auto contains(PresentationAttempt attempt, TileId tile) const -> bool {
		if (!contains(attempt) || tile.value >= maximumExpectedTiles) {
			return false;
		}
		return pending_.at(attempt.sequence).expected.test(tile.value);
	}

	/// @brief Tests exact pending identity before callers apply negative feedback to geometry.
	[[nodiscard]] auto contains(PresentationAttempt attempt) const -> bool {
		const auto found = pending_.find(attempt.sequence);
		return attempt.epoch == epoch_ && found != pending_.end() &&
			found->second.attempt == attempt;
	}

	/// @brief Releases a failed/expired attempt; late callbacks can never resurrect it.
	[[nodiscard]] auto retire(PresentationAttempt attempt) -> bool {
		return contains(attempt) && pending_.erase(attempt.sequence) != 0;
	}

	/// @brief Invalidates pending evidence when a native surface is replaced.
	/// @throws std::invalid_argument unless epoch is nonzero and strictly newer.
	void reset(std::uint64_t epoch) {
		if (epoch == 0 || epoch <= epoch_) {
			throw std::invalid_argument("Presentation epoch must advance");
		}
		epoch_ = epoch;
		pending_.clear();
		confirmed_.reset();
	}

	/// @brief Returns the newest attempt proven both GPU-successful and presented.
	[[nodiscard]] auto confirmed() const noexcept -> std::optional<PresentationAttempt> {
		return confirmed_;
	}

	/// @brief Returns bounded unresolved attempts, useful for scheduler admission tests.
	[[nodiscard]] auto pendingCount() const noexcept -> std::size_t { return pending_.size(); }

  private:
	/// @brief Partial asynchronous observations for one attempt; never owns render resources.
	struct State {
		PresentationAttempt attempt{}; ///< Exact identity used to reject unrelated callbacks.
		bool gpuFinished = false;	   ///< Whether GPU completion has been reported.
		std::bitset<maximumExpectedTiles> expected{}; ///< Owned bounded expected set.
		std::bitset<maximumExpectedTiles>
			presented{}; ///< First positive observations, never erased by duplicates.
	};

	static constexpr std::size_t historyLimit =
		64;					  ///< Maximum unresolved attempts before admission fails.
	std::uint64_t epoch_ = 0; ///< Active surface generation.
	std::map<std::uint64_t, State> pending_{}; ///< Attempt observations keyed by unique sequence.
	std::optional<PresentationAttempt> confirmed_{}; ///< Most recent fully confirmed identity.

	[[nodiscard]] auto find(PresentationAttempt attempt) -> State * {
		const auto found = pending_.find(attempt.sequence);
		return attempt.epoch == epoch_ && found != pending_.end() &&
				found->second.attempt == attempt
			? &found->second
			: nullptr;
	}

	[[nodiscard]] auto confirm(const State &state) -> bool {
		if (!state.gpuFinished || (state.presented & state.expected) != state.expected ||
			(confirmed_ && confirmed_->sequence >= state.attempt.sequence)) {
			return false;
		}
		const auto attempt = state.attempt;
		confirmed_ = attempt;
		pending_.erase(pending_.begin(), pending_.upper_bound(attempt.sequence));
		return true;
	}
};
} // namespace stana::rendering
