module;

#include <algorithm>
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
	focused_ = identity;
	invalidate();
	commit();
	return {};
}

void Document::tab(bool backwards) {
	std::vector<NodeId> candidates;
	for (const auto &snapshot : snapshots_) {
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
	if ((node->clip || node->kind == NodeKind::scroll) && !bounds->contains(point)) {
		return std::nullopt;
	}
	for (const auto child : std::views::reverse(node->children)) {
		if (const auto target = hitTestNode(child, point)) {
			return target;
		}
	}
	return bounds->contains(point) ? std::optional{identity} : std::nullopt;
}

auto Document::hitTest(Point point) const -> std::optional<NodeId> {
	if (usesPresentationGeometry_ && confirmedRevision_ == 0) {
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
			node->scrollOffset += delta;
			invalidate();
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

auto Document::beginPresentation(std::uint64_t epoch, std::uint64_t sceneRevision)
	-> std::expected<PresentationAttempt, Error> {
	if (!usesPresentationGeometry_) {
		enablePresentationTracking();
	}
	if (isClosed_ || !geometryHistory_.contains(sceneRevision)) {
		return std::unexpected(Error::invalidValue);
	}
	return presentation_.begin(epoch, sceneRevision);
}

auto Document::reportGPUCompletion(PresentationAttempt attempt, bool success) -> bool {
	if (isClosed_ || !presentation_.contains(attempt)) {
		return false;
	}
	if (presentation_.reportGPU(attempt, success)) {
		confirmPresentation(attempt.sceneRevision);
		return true;
	}
	if (!success) {
		markPresentationUnknown(attempt.sceneRevision);
	}
	return false;
}

auto Document::reportPresentation(PresentationAttempt attempt, double timestamp) -> bool {
	if (isClosed_ || !presentation_.contains(attempt)) {
		return false;
	}
	if (presentation_.reportPresented(attempt, timestamp)) {
		confirmPresentation(attempt.sceneRevision);
		return true;
	}
	if (!std::isfinite(timestamp) || timestamp <= 0) {
		markPresentationUnknown(attempt.sceneRevision);
	}
	return false;
}

void Document::abandonPresentation(PresentationAttempt attempt) {
	if (!isClosed_ && presentation_.retire(attempt)) {
		markPresentationUnknown(attempt.sceneRevision);
	}
}

void Document::resetPresentation(std::uint64_t epoch) {
	presentation_.reset(epoch);
}

void Document::rememberGeometry() {
	if (!usesPresentationGeometry_ || !scene_) {
		return;
	}
	std::map<NodeId, Rect> bounds;
	for (const auto &node : snapshots_) {
		bounds.emplace(node.id, node.bounds);
	}
	geometryHistory_.insert_or_assign(scene_->revision, std::move(bounds));
	constexpr std::size_t historyLimit = 64;
	while (geometryHistory_.size() > historyLimit) {
		markPresentationUnknown(geometryHistory_.begin()->first);
		geometryHistory_.erase(geometryHistory_.begin());
	}
}

void Document::confirmPresentation(std::uint64_t revision) {
	const auto found = geometryHistory_.find(revision);
	if (found == geometryHistory_.end() || revision < confirmedRevision_) {
		return;
	}
	confirmedBounds_ = found->second;
	confirmedRevision_ = revision;
	if (revision >= unknownRevision_) {
		ambiguousRegions_.clear();
		unknownRevision_ = 0;
	}
	geometryHistory_.erase(geometryHistory_.begin(), found);
}

void Document::markPresentationUnknown(std::uint64_t revision) {
	if (revision < confirmedRevision_) {
		return;
	}
	const auto found = geometryHistory_.find(revision);
	if (found == geometryHistory_.end()) {
		return;
	}
	unknownRevision_ = std::max(unknownRevision_, revision);
	for (const auto &[id, bounds] : found->second) {
		const auto previous = confirmedBounds_.find(id);
		if (previous == confirmedBounds_.end() || previous->second != bounds) {
			ambiguousRegions_.push_back(bounds);
			if (previous != confirmedBounds_.end()) {
				ambiguousRegions_.push_back(previous->second);
			}
		}
	}
	for (const auto &[id, bounds] : confirmedBounds_) {
		if (!found->second.contains(id)) {
			ambiguousRegions_.push_back(bounds);
		}
	}
	constexpr std::size_t maximumAmbiguousRegions = 64;
	if (ambiguousRegions_.size() > maximumAmbiguousRegions) {
		ambiguousRegions_ = {Rect{.size = viewport_}};
	}
}

auto Document::interactionBounds(NodeId identity) const -> std::optional<Rect> {
	if (usesPresentationGeometry_) {
		const auto found = confirmedBounds_.find(identity);
		return found == confirmedBounds_.end() ? std::nullopt : std::optional{found->second};
	}
	const auto *node = find(identity);
	return node == nullptr ? std::nullopt : std::optional{node->bounds};
}

void Document::keyActivate() {
	if (focused_) {
		if (const auto result = activate(*focused_); !result) {
			report(result.error());
		}
	}
}
} // namespace stana::detail
