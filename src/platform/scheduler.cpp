#include "scheduler.hpp"
#include <limits>
#include <stdexcept>

namespace stana::platform::detail {
auto FrameScheduler::matches(FrameIdentity frame) const -> bool {
	return active_ && active_->epoch == frame.epoch && active_->sequence == frame.sequence &&
		active_->sceneRevision == frame.sceneRevision;
}

auto FrameScheduler::canBuild(bool visible) -> bool {
	if (phase_ != FramePhase::idle && phase_ != FramePhase::occluded) {
		return false;
	}
	if (!visible) {
		phase_ = FramePhase::occluded;
		retryAt_.reset();
		retriedAcquisition_ = false;
		return false;
	}
	if (retryAt_) {
		return false;
	}
	phase_ = FramePhase::building;
	return true;
}

auto FrameScheduler::acquisitionFailed(bool visible, Time now) -> FrameAction {
	if (phase_ != FramePhase::building) {
		return FrameAction::none;
	}
	if (!visible) {
		phase_ = FramePhase::occluded;
		retryAt_.reset();
		retriedAcquisition_ = false;
		return FrameAction::none;
	}
	if (!retriedAcquisition_) {
		retriedAcquisition_ = true;
		retryAt_ = now + drawableRetryDelay;
		phase_ = FramePhase::idle;
		return FrameAction::retry;
	}
	return recover(now);
}

void FrameScheduler::submitted(FrameIdentity frame, Time now) {
	if (phase_ != FramePhase::building || active_) {
		throw std::logic_error("Composite admission violates single-batch backpressure");
	}
	active_ = frame;
	gpuFinished_ = false;
	retriedAcquisition_ = false;
	retryAt_.reset();
	deadline_ = now + confirmationDeadline;
	phase_ = FramePhase::awaitingEvidence;
}

auto FrameScheduler::recover(Time now) -> FrameAction {
	retryAt_.reset();
	retriedAcquisition_ = false;
	if (recoveryConsumed_) {
		phase_ = FramePhase::failed;
		deadline_.reset();
		return FrameAction::failure;
	}
	recoveryConsumed_ = true;
	if (active_ && !gpuFinished_) {
		phase_ = FramePhase::draining;
		deadline_ = now + recoveryDrainDeadline;
		return FrameAction::none;
	}
	phase_ = FramePhase::recovering;
	deadline_.reset();
	return FrameAction::replaceGrid;
}

auto FrameScheduler::gpuFinished(FrameIdentity frame, bool success, Time now) -> FrameAction {
	if (!matches(frame) || phase_ == FramePhase::closed || phase_ == FramePhase::failed) {
		return FrameAction::none;
	}
	gpuFinished_ = true;
	if (phase_ == FramePhase::draining) {
		phase_ = FramePhase::recovering;
		deadline_.reset();
		return FrameAction::replaceGrid;
	}
	if (!success) {
		return recover(now);
	}
	return FrameAction::none;
}

auto FrameScheduler::confirm(FrameIdentity frame) -> bool {
	if (!matches(frame) || phase_ != FramePhase::awaitingEvidence || !gpuFinished_) {
		return false;
	}
	active_.reset();
	deadline_.reset();
	retryAt_.reset();
	phase_ = FramePhase::idle;
	recoveryConsumed_ = false;
	unknownPresentationRetries_ = 0;
	return true;
}

auto FrameScheduler::unknownPresentation(Time now) -> FrameAction {
	if (phase_ != FramePhase::awaitingEvidence || !active_ || !gpuFinished_) {
		return FrameAction::none;
	}
	active_.reset();
	deadline_.reset();
	gpuFinished_ = false;
	retriedAcquisition_ = false;
	phase_ = FramePhase::idle;
	const auto delay = unknownPresentationRetries_ < fastUnknownPresentationRetries
		? drawableRetryDelay
		: confirmationDeadline;
	if (unknownPresentationRetries_ < std::numeric_limits<unsigned>::max()) {
		++unknownPresentationRetries_;
	}
	retryAt_ = now + delay;
	return FrameAction::retry;
}

auto FrameScheduler::expire(Time now) -> FrameAction {
	if (phase_ == FramePhase::closed || phase_ == FramePhase::failed) {
		return FrameAction::none;
	}
	if (retryAt_ && now >= *retryAt_) {
		retryAt_.reset();
		return FrameAction::retry;
	}
	if (!deadline_ || now < *deadline_) {
		return FrameAction::none;
	}
	if (phase_ == FramePhase::draining) {
		phase_ = FramePhase::failed;
		deadline_.reset();
		return FrameAction::failure;
	}
	return recover(now);
}

auto FrameScheduler::invalidateSurface(Time now) -> FrameAction {
	if (phase_ == FramePhase::draining) {
		return FrameAction::none;
	}
	if (phase_ == FramePhase::closed || phase_ == FramePhase::failed) {
		return FrameAction::none;
	}
	retryAt_.reset();
	retriedAcquisition_ = false;
	if (active_ && !gpuFinished_) {
		phase_ = FramePhase::draining;
		deadline_ = now + recoveryDrainDeadline;
		return FrameAction::none;
	}
	phase_ = FramePhase::recovering;
	deadline_.reset();
	return FrameAction::replaceGrid;
}

auto FrameScheduler::wakeDelay(Time current) const -> std::optional<Time> {
	const auto wake = retryAt_ ? retryAt_ : deadline_;
	if (!wake) {
		return std::nullopt;
	}
	return *wake > current ? *wake - current : Time{0};
}

void FrameScheduler::gridInstalled() {
	if (phase_ == FramePhase::closed || phase_ == FramePhase::failed) {
		return;
	}
	if (phase_ == FramePhase::draining) {
		throw std::logic_error("Cannot replace grid during GPU reads");
	}
	active_.reset();
	deadline_.reset();
	retryAt_.reset();
	gpuFinished_ = false;
	retriedAcquisition_ = false;
	unknownPresentationRetries_ = 0;
	phase_ = FramePhase::idle;
}

void FrameScheduler::noDemand() {
	if (phase_ == FramePhase::building) {
		phase_ = FramePhase::idle;
	}
}

void FrameScheduler::close() {
	phase_ = FramePhase::closed;
	active_.reset();
	retryAt_.reset();
	deadline_.reset();
}
} // namespace stana::platform::detail
