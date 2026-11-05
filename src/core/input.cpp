module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module stana.core;
import stana.primitives;
import stana.reactive;
import stana.graphics;

namespace stana::detail {
auto Document::isEligible(NodeId identity) const -> bool {
	const auto *node = find(identity);
	while (node != nullptr) {
		if (!node->scope.alive() || node->visibility != Visibility::visible || !node->isEnabled ||
			node->inert) {
			return false;
		}
		if (node->parent.serial == 0) {
			return true;
		}
		node = find(node->parent);
	}
	return false;
}

auto Document::activate(NodeId identity) -> std::expected<void, Error> {
	auto *node = find(identity);
	if (node == nullptr || !node->scope.alive()) {
		return std::unexpected(Error::staleHandle);
	}
	if (!isEligible(identity)) {
		return std::unexpected(Error::notFocusable);
	}
	const auto callback = node->onClick;
	if (callback) {
		Transaction::perform(callback);
	}
	return {};
}

auto Document::focus(NodeId identity) -> std::expected<void, Error> {
	const auto *node = find(identity);
	if (!isEligible(identity) || node == nullptr ||
		(node->kind != NodeKind::button && node->kind != NodeKind::toggle)) {
		return std::unexpected(Error::notFocusable);
	}
	if (focused_ == identity) {
		return {};
	}
	if (focused_) {
		invalidate(*focused_, Dirty::paint | Dirty::snapshot);
	}
	focused_ = identity;
	invalidate(identity, Dirty::paint | Dirty::snapshot);
	commit();
	return {};
}

void Document::tab(bool backwards) {
	std::vector<NodeId> candidates;
	for (const auto &snapshot : snapshots()) {
		const auto *node = find(snapshot.id);
		if (node != nullptr && isEligible(node->id) &&
			(node->kind == NodeKind::button || node->kind == NodeKind::toggle)) {
			candidates.push_back(node->id);
		}
	}
	if (backwards) {
		std::ranges::reverse(candidates);
	}
	if (candidates.empty()) {
		return;
	}
	const auto previous = std::ranges::find(candidates, focused_.value_or(NodeId{}));
	const auto next = previous == candidates.end() ? candidates.begin() : std::next(previous);
	if (const auto result = focus(next == candidates.end() ? candidates.front() : *next); !result) {
		report(result.error());
	}
}

auto Document::hitTestNode(NodeId identity, Point point) const -> std::optional<NodeId> {
	const auto *node = find(identity);
	if (node == nullptr || !isEligible(identity)) {
		return std::nullopt;
	}
	const auto bounds = interactionBounds(identity);
	if (!bounds) {
		return std::nullopt;
	}
	const auto geometry = geometryFind(
		usesPresentationGeometry_ ? confirmedGeometry_
								  : (render_ ? render_->geometry : GeometryRoot{}),
		identity
	);
	if (!geometry || !geometry->rectangularClip.contains(point)) {
		return std::nullopt;
	}

	if (geometry->children) {
		for (const auto child : std::views::reverse(*geometry->children)) {
			if (const auto target = hitTestNode(child, point)) {
				return target;
			}
		}
	}
	return bounds->contains(point) ? std::optional{identity} : std::nullopt;
}

auto Document::hitTest(Point point) const -> std::optional<NodeId> {
	if (usesPresentationGeometry_ && (confirmedRevision_ == 0 || surfaceGated_)) {
		return std::nullopt;
	}
	if (std::ranges::any_of(ambiguousRegions_, [point](Rect region) -> bool {
			return region.contains(point);
		})) {
		return std::nullopt;
	}
	return hitTestNode(root_, point);
}

void Document::pointerDown(Point point) {
	commit();
	const auto target = hitTest(point);
	if (!target) {
		return;
	}
	std::vector<NodeId> route;
	for (auto *node = find(*target); node != nullptr; node = find(node->parent)) {
		route.push_back(node->id);
	}
	PointerEvent event{.position = point};
	for (const auto identity : std::views::reverse(route)) {
		if (const auto *node = find(identity); node != nullptr && node->onPointerDownCapture) {
			const auto action = node->onPointerDownCapture;
			action(event);
		}
	}
	for (const auto identity : route) {
		if (const auto *node = find(identity); node != nullptr && node->onPointerDown) {
			const auto action = node->onPointerDown;
			action(event);
		}
	}
	if (!event.defaultPrevented) {
		for (const auto identity : route) {
			if (const auto *node = find(identity); node != nullptr &&
				(node->kind == NodeKind::button || node->kind == NodeKind::toggle)) {
				pressed_ = identity;
				if (const auto result = focus(identity); !result) {
					report(result.error());
				}
				break;
			}
		}
	}
}

void Document::pointerUp(Point point) {
	const auto pressed = std::exchange(pressed_, std::nullopt);
	if (pressed) {
		if (const auto bounds = interactionBounds(*pressed);
			bounds && bounds->contains(point) && hitTest(point)) {
			if (const auto result = activate(*pressed); !result) {
				report(result.error());
			}
		}
	}
}

void Document::cancelPointer() {
	pressed_.reset();
}

void Document::scroll(Point point, double delta) {
	requireFinite(delta);
	for (auto target = hitTest(point); target;) {
		auto *node = find(*target);
		if (node == nullptr) {
			return;
		}
		if (node->kind == NodeKind::scroll) {
			if (delta <= 0 && delta >= 0) {
				return;
			}
			node->scrollOffset += delta;
			invalidate(node->id, Dirty::arrange);
			commit();
			return;
		}
		target = node->parent.serial == 0 ? std::nullopt : std::optional{node->parent};
	}
}

void Document::enablePresentationTracking() {
	usesPresentationGeometry_ = true;
	rememberGeometry();
}

auto Document::beginPresentation(std::uint64_t epoch, std::uint64_t revision)
	-> std::expected<PresentationAttempt, Error> {
	constexpr std::array tiles{TileId{0}};
	return beginPresentation(epoch, revision, tiles);
}

auto Document::beginPresentation(
	std::uint64_t epoch,
	std::uint64_t revision,
	std::span<const TileId> tiles
) -> std::expected<PresentationAttempt, Error> {
	if (!usesPresentationGeometry_) {
		enablePresentationTracking();
	}
	const auto found = geometryHistory_.find(revision);
	const auto geometry = found != geometryHistory_.end()
		? found->second
		: (preparedGeometry_ && preparedGeometry_->first == revision ? preparedGeometry_->second
																	 : GeometryRoot{});
	if (isClosed_ || !geometry) {
		return std::unexpected(Error::invalidValue);
	}
	const auto attempt = presentation_.begin(epoch, revision, tiles);
	if (attempt) {
		if (epoch > presentationEpoch_) {
			pinnedGeometry_.clear();
			presentationEpoch_ = epoch;
		}
		pinnedGeometry_.emplace(
			attempt->sequence,
			PinnedGeometry{.attempt = *attempt, .root = geometry}
		);
	}
	return attempt;
}

void Document::acceptPresentation(PresentationAttempt attempt) {
	const auto pinned = pinnedGeometry_.find(attempt.sequence);
	if (pinned == pinnedGeometry_.end()) {
		return;
	}
	if (attempt.sceneRevision >= confirmedRevision_) {
		confirmedGeometry_ = pinned->second.root;
		confirmedRevision_ = attempt.sceneRevision;
		if (attempt.epoch == presentationEpoch_) {
			surfaceGated_ = false;
		}
		if (attempt.sceneRevision >= unknownRevision_ && !surfaceGated_) {
			ambiguousRegions_.clear();
			unknownRevision_ = 0;
		}
	}
	pinnedGeometry_.erase(pinnedGeometry_.begin(), pinnedGeometry_.upper_bound(attempt.sequence));
}

auto Document::reportGPUCompletion(PresentationAttempt attempt, bool success) -> bool {
	if (isClosed_ || !presentation_.contains(attempt)) {
		return false;
	}
	if (presentation_.reportGPU(attempt, success)) {
		acceptPresentation(attempt);
		return true;
	}
	if (!success) {
		gatePendingPresentation(attempt);
		pinnedGeometry_.erase(attempt.sequence);
	}
	return false;
}

auto Document::reportPresentation(PresentationAttempt attempt, double timestamp) -> bool {
	return reportPresentation(attempt, TilePresentation{.tile = TileId{0}, .timestamp = timestamp});
}

auto Document::reportPresentation(PresentationAttempt attempt, TilePresentation observation)
	-> bool {
	if (isClosed_ || !presentation_.contains(attempt, observation.tile)) {
		return false;
	}
	if (presentation_.reportPresented(attempt, observation)) {
		acceptPresentation(attempt);
		return true;
	}
	if (!std::isfinite(observation.timestamp) || observation.timestamp <= 0) {
		gatePendingPresentation(attempt);
	}
	return false;
}

void Document::gatePendingPresentation(PresentationAttempt attempt) {
	const auto found = pinnedGeometry_.find(attempt.sequence);
	if (isClosed_ || found == pinnedGeometry_.end() || found->second.attempt != attempt) {
		return;
	}
	gateGeometry(attempt.sceneRevision, found->second.root);
}

void Document::gateSurface() {
	if (isClosed_) {
		return;
	}
	surfaceGated_ = true;
	ambiguousRegions_ = {Rect{.size = viewport_}};
}

void Document::abandonPresentation(PresentationAttempt attempt) {
	if (!isClosed_ && presentation_.retire(attempt)) {
		gatePendingPresentation(attempt);
		pinnedGeometry_.erase(attempt.sequence);
	}
}

void Document::holdPreparedGeometry(std::uint64_t revision) {
	if (const auto found = geometryHistory_.find(revision); found != geometryHistory_.end()) {
		preparedGeometry_ = std::pair{revision, found->second};
	}
}

void Document::resetPresentation(std::uint64_t epoch) {
	preparedGeometry_.reset();
	presentation_.reset(epoch);
	presentationEpoch_ = epoch;
	pinnedGeometry_.clear();
}

void Document::rememberGeometry() {
	if (!usesPresentationGeometry_ || !render_) {
		return;
	}
	geometryHistory_.insert_or_assign(render_->revision, render_->geometry);
	constexpr std::size_t historyLimit = 64;
	while (geometryHistory_.size() > historyLimit) {
		geometryHistory_.erase(geometryHistory_.begin());
	}
}

void Document::gateGeometry(std::uint64_t revision, const GeometryRoot &root) {
	if (revision < confirmedRevision_) {
		return;
	}
	unknownRevision_ = std::max(unknownRevision_, revision);
	geometryChanges(confirmedGeometry_, root, ambiguousRegions_);
	constexpr std::size_t maximumGateRegions = 64;
	if (ambiguousRegions_.size() > maximumGateRegions) {
		ambiguousRegions_ = {Rect{.size = viewport_}};
	}
}

auto Document::interactionBounds(NodeId identity) const -> std::optional<Rect> {
	const auto record = geometryFind(
		usesPresentationGeometry_ ? confirmedGeometry_
								  : (render_ ? render_->geometry : GeometryRoot{}),
		identity
	);
	return record && record->eligible ? std::optional{record->bounds} : std::nullopt;
}

void Document::keyActivate() {
	if (focused_) {
		if (const auto result = activate(*focused_); !result) {
			report(result.error());
		}
	}
}
} // namespace stana::detail
