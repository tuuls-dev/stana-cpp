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
#include <variant>
#include <vector>

module stana;
import :document;
import :primitives;
import :reactive;
import :rendering;

namespace stana::document::detail {
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

auto Document::ancestorRoute(NodeId identity) const -> std::vector<NodeId> {
	std::vector<NodeId> route;
	for (const auto *node = find(identity); node != nullptr; node = find(node->parent)) {
		route.push_back(node->id);
	}
	return route;
}

auto Document::keyboardTarget() const -> NodeId {
	if (focused_ && isEligible(*focused_)) {
		return *focused_;
	}
	return root_;
}

template <typename Payload>
auto Document::dispatch(NodeId target, EventKind kind, Payload event, bool includeAncestors)
	-> Payload {
	auto route = includeAncestors ? ancestorRoute(target) : std::vector<NodeId>{};
	if (!includeAncestors && find(target) != nullptr) {
		route.push_back(target);
	}
	if (route.empty()) {
		return event;
	}
	event.target = target;
	// Copy callables first. Disposal during a handler releases the occurrence's
	// registrations before that handler returns.
	const auto invoke = [&](NodeId identity, Phase phase) -> void {
		const auto *node = find(identity);
		if (node == nullptr || !node->scope.alive()) {
			return;
		}
		std::vector<std::function<void(Payload &)>> handlers;
		handlers.reserve(node->events.size());
		for (const auto &registration : node->events) {
			if (registration.kind != kind || registration.phase != phase) {
				continue;
			}
			const auto *function =
				std::get_if<std::function<void(Payload &)>>(&registration.handler);
			if (function != nullptr && *function) {
				handlers.push_back(*function);
			}
		}
		event.phase = phase;
		event.current = identity;
		for (const auto &handler : handlers) {
			const auto *current = find(identity);
			if (current == nullptr || !current->scope.alive()) {
				break;
			}
			handler(event);
		}
	};
	if (includeAncestors) {
		for (const auto identity : std::views::reverse(route)) {
			invoke(identity, Phase::capture);
		}
		for (const auto identity : route) {
			invoke(identity, Phase::bubble);
		}
	} else {
		invoke(route.front(), Phase::capture);
		invoke(route.front(), Phase::bubble);
	}
	return event;
}

void Document::updateHover(std::optional<NodeId> next, Point point) {
	if (next == hovered_ || updatingHover_) {
		return;
	}
	updatingHover_ = true;

	struct HoverGuard {
		bool *flag = nullptr;

		explicit HoverGuard(bool *value) : flag(value) {}

		HoverGuard(const HoverGuard &) = delete;
		HoverGuard(HoverGuard &&) = delete;
		auto operator=(const HoverGuard &) -> HoverGuard & = delete;
		auto operator=(HoverGuard &&) -> HoverGuard & = delete;

		~HoverGuard() { *flag = false; }
	};

	const HoverGuard guard{&updatingHover_};

	const auto previous = std::exchange(hovered_, next);
	const auto previousRoute = previous ? ancestorRoute(*previous) : std::vector<NodeId>{};
	const auto nextRoute = next ? ancestorRoute(*next) : std::vector<NodeId>{};
	const auto listed = [](const std::vector<NodeId> &route, NodeId identity) -> bool {
		return std::ranges::find(route, identity) != route.end();
	};
	for (const auto identity : previousRoute) {
		if (!listed(nextRoute, identity)) {
			static_cast<void>(dispatch<PointerEvent>(
				identity,
				EventKind::pointerLeave,
				PointerEvent{.position = point},
				false
			));
		}
	}
	for (const auto identity : std::views::reverse(nextRoute)) {
		if (!listed(previousRoute, identity)) {
			static_cast<void>(dispatch<PointerEvent>(
				identity,
				EventKind::pointerEnter,
				PointerEvent{.position = point},
				false
			));
		}
	}
}

auto Document::activate(NodeId identity) -> std::expected<void, Error> {
	auto *node = find(identity);
	if (node == nullptr || !node->scope.alive()) {
		return std::unexpected(Error::staleHandle);
	}
	if (!isEligible(identity)) {
		return std::unexpected(Error::notFocusable);
	}
	const auto fallback = node->defaultActivate;
	stana::reactive::Transaction::perform([&] -> void {
		const auto event = dispatch<ClickEvent>(identity, EventKind::click, ClickEvent{}, true);
		if (!event.defaultPrevented && fallback && find(identity) != nullptr &&
			isEligible(identity)) {
			fallback();
		}
	});
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
	const auto previous = focused_;
	if (previous) {
		invalidate(*previous, Dirty::paint | Dirty::snapshot);
	}
	focused_ = identity;
	invalidate(identity, Dirty::paint | Dirty::snapshot);
	commit();
	if (previous && find(*previous) != nullptr) {
		static_cast<void>(dispatch<FocusEvent>(*previous, EventKind::blur, FocusEvent{}, true));
	}
	if (focused_ == identity && find(identity) != nullptr) {
		static_cast<void>(dispatch<FocusEvent>(identity, EventKind::focus, FocusEvent{}, true));
	}
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
	const auto geometry = stana::rendering::detail::geometryFind(
		usesPresentationGeometry_
			? confirmedGeometry_
			: (render_ ? render_->geometry : stana::rendering::detail::GeometryRoot{}),
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
	pointerPosition_ = point;
	const auto target = hitTest(point);
	updateHover(target, point);
	if (!target) {
		return;
	}
	const auto event = dispatch<PointerEvent>(
		*target,
		EventKind::pointerDown,
		PointerEvent{.position = point},
		true
	);
	if (event.defaultPrevented) {
		return;
	}
	for (const auto identity : ancestorRoute(*target)) {
		const auto *node = find(identity);
		if (node != nullptr && (node->kind == NodeKind::button || node->kind == NodeKind::toggle)) {
			pressed_ = identity;
			if (const auto result = focus(identity); !result) {
				report(result.error());
			}
			break;
		}
	}
}

void Document::pointerUp(Point point) {
	pointerPosition_ = point;
	const auto pressed = std::exchange(pressed_, std::nullopt);
	const auto bounds = pressed ? interactionBounds(*pressed) : std::nullopt;
	const bool releasedInside =
		bounds.has_value() && bounds->contains(point) && hitTest(point).has_value();
	const auto target = hitTest(point);
	updateHover(target, point);
	if (target) {
		static_cast<void>(dispatch<PointerEvent>(
			*target,
			EventKind::pointerUp,
			PointerEvent{.position = point},
			true
		));
	}
	if (pressed && releasedInside) {
		if (const auto result = activate(*pressed); !result) {
			report(result.error());
		}
	}
}

void Document::pointerMove(Point point) {
	pointerPosition_ = point;
	const auto target = hitTest(point);
	updateHover(target, point);
	if (!target) {
		return;
	}
	static_cast<void>(dispatch<PointerEvent>(
		*target,
		EventKind::pointerMove,
		PointerEvent{.position = point},
		true
	));
}

void Document::pointerLeave() {
	updateHover(std::nullopt, pointerPosition_);
}

void Document::cancelPointer() {
	pressed_.reset();
}

void Document::scroll(Point point, double delta) {
	stana::detail::requireFinite(delta);
	pointerPosition_ = point;
	const auto target = hitTest(point);
	if (!target) {
		return;
	}
	const auto event = dispatch<ScrollEvent>(
		*target,
		EventKind::scroll,
		ScrollEvent{.position = point, .delta = delta},
		true
	);
	if (event.defaultPrevented) {
		return;
	}
	for (auto current = target; current;) {
		auto *node = find(*current);
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
		current = node->parent.serial == 0 ? std::nullopt : std::optional{node->parent};
	}
}

void Document::enablePresentationTracking() {
	usesPresentationGeometry_ = true;
	rememberGeometry();
}

auto Document::beginPresentation(std::uint64_t epoch, std::uint64_t revision)
	-> std::expected<stana::rendering::PresentationAttempt, Error> {
	constexpr std::array tiles{stana::rendering::TileId{0}};
	return beginPresentation(epoch, revision, tiles);
}

auto Document::beginPresentation(
	std::uint64_t epoch,
	std::uint64_t revision,
	std::span<const stana::rendering::TileId> tiles
) -> std::expected<stana::rendering::PresentationAttempt, Error> {
	if (!usesPresentationGeometry_) {
		enablePresentationTracking();
	}
	const auto found = geometryHistory_.find(revision);
	const auto geometry = found != geometryHistory_.end()
		? found->second
		: (preparedGeometry_ && preparedGeometry_->first == revision
				  ? preparedGeometry_->second
				  : stana::rendering::detail::GeometryRoot{});
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

void Document::acceptPresentation(stana::rendering::PresentationAttempt attempt) {
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

auto Document::reportGPUCompletion(stana::rendering::PresentationAttempt attempt, bool success)
	-> bool {
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

auto Document::reportPresentation(stana::rendering::PresentationAttempt attempt, double timestamp)
	-> bool {
	return reportPresentation(
		attempt,
		stana::rendering::TilePresentation{
			.tile = stana::rendering::TileId{0},
			.timestamp = timestamp
		}
	);
}

auto Document::reportPresentation(
	stana::rendering::PresentationAttempt attempt,
	stana::rendering::TilePresentation observation
) -> bool {
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

void Document::gatePendingPresentation(stana::rendering::PresentationAttempt attempt) {
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

void Document::abandonPresentation(stana::rendering::PresentationAttempt attempt) {
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
	bool adopted = false;
	for (const auto &[serial, node] : nodes_) {
		static_cast<void>(serial);
		if (node->kind != NodeKind::symbol || !node->symbol || node->symbol->epoch == epoch) {
			continue;
		}
		auto &symbol = *node->symbol;
		symbol.epoch = epoch;
		symbol.available = true;
		symbol.failed.clear();
		symbol.generation = stana::detail::nextIdentity();
		invalidate(node->id, Dirty::paint);
		adopted = true;
	}
	if (adopted && !isCommitting_) {
		commit();
	}
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

void Document::gateGeometry(
	std::uint64_t revision,
	const stana::rendering::detail::GeometryRoot &root
) {
	if (revision < confirmedRevision_) {
		return;
	}
	unknownRevision_ = std::max(unknownRevision_, revision);
	stana::rendering::detail::geometryChanges(confirmedGeometry_, root, ambiguousRegions_);
	constexpr std::size_t maximumGateRegions = 64;
	if (ambiguousRegions_.size() > maximumGateRegions) {
		ambiguousRegions_ = {Rect{.size = viewport_}};
	}
}

auto Document::interactionBounds(NodeId identity) const -> std::optional<Rect> {
	const auto record = stana::rendering::detail::geometryFind(
		usesPresentationGeometry_
			? confirmedGeometry_
			: (render_ ? render_->geometry : stana::rendering::detail::GeometryRoot{}),
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

void Document::keyDown(Key key, bool backwards) {
	const auto event = dispatch<KeyEvent>(
		keyboardTarget(),
		EventKind::keyDown,
		KeyEvent{.key = key, .backwards = backwards},
		true
	);
	if (event.defaultPrevented || isClosed_) {
		return;
	}
	switch (key) {
	case Key::tab:
		tab(backwards);
		break;
	case Key::space:
	case Key::enter:
		keyActivate();
		break;
	}
}

void Document::keyUp(Key key, bool backwards) {
	static_cast<void>(dispatch<KeyEvent>(
		keyboardTarget(),
		EventKind::keyUp,
		KeyEvent{.key = key, .backwards = backwards},
		true
	));
}
} // namespace stana::document::detail
