module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <stdexcept>

export module stana.graphics:presentation;
import stana.primitives;

export namespace stana {
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

/// @brief Joins GPU completion with positive native presentation evidence.
///
/// Deterministic hosts inject the same two independent observations a native
/// drawable supplies. GPU completion, offscreen capture and zero timestamps alone
/// never establish displayed geometry. Late feedback from a replaced epoch is
/// ignored, and a bounded pending history prevents unbounded callback ownership.
class PresentationTracker {
  public:
	/// @brief Registers an attempt before its asynchronous completion callbacks run.
	/// @return A unique identity, or failure when the epoch is stale/history is full.
	[[nodiscard]] auto begin(std::uint64_t epoch, std::uint64_t sceneRevision)
		-> std::expected<PresentationAttempt, Error> {
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
			.sequence = detail::nextIdentity(),
			.sceneRevision = sceneRevision
		};
		pending_.emplace(attempt.sequence, State{.attempt = attempt});
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
	[[nodiscard]] auto reportPresented(PresentationAttempt attempt, double timestamp) -> bool {
		if (auto *state = find(attempt)) {
			state->hasPositiveTimestamp =
				state->hasPositiveTimestamp || (std::isfinite(timestamp) && timestamp > 0);
			return confirm(*state);
		}
		return false;
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
		bool hasPositiveTimestamp =
			false; ///< Whether native evidence proves this drawable presented.
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
		if (!state.gpuFinished || !state.hasPositiveTimestamp ||
			(confirmed_ && confirmed_->sequence >= state.attempt.sequence)) {
			return false;
		}
		const auto attempt = state.attempt;
		confirmed_ = attempt;
		pending_.erase(pending_.begin(), pending_.upper_bound(attempt.sequence));
		return true;
	}
};
} // namespace stana
