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

namespace {
[[nodiscard]] auto listed(const std::vector<NodeId> &route, NodeId identity) -> bool {
	return std::ranges::find(route, identity) != route.end();
}

[[nodiscard]] auto slotHas(const VisualStateSlot &slot, VisualField field) -> bool {
	switch (field) {
	case VisualField::background:
		return slot.background.has_value();
	case VisualField::opacity:
		return slot.opacity.has_value();
	case VisualField::borderColor:
		return slot.borderColor.has_value();
	case VisualField::borderWidth:
		return slot.borderWidth.has_value();
	case VisualField::cornerRadius:
		return slot.cornerRadius.has_value();
	case VisualField::cornerRadii:
		return slot.cornerRadii.has_value();
	case VisualField::borderWidths:
		return slot.borderWidths.has_value();
	case VisualField::boxShadows:
		return slot.boxShadows.has_value();
	case VisualField::foreground:
		return slot.foreground.has_value();
	}
	return false;
}

[[nodiscard]] auto slotFor(const Node &node, VisualLayer layer) -> const VisualStateSlot & {
	switch (layer) {
	case VisualLayer::hovered:
		return node.hoveredStyle;
	case VisualLayer::pressed:
		return node.pressedStyle;
	case VisualLayer::focused:
		return node.focusedStyle;
	case VisualLayer::disabled:
		return node.disabledStyle;
	}
	throw std::logic_error("Unknown visual state");
}
} // namespace

auto Document::effectivelyDisabled(NodeId identity) const -> bool {
	for (const auto *node = find(identity); node != nullptr; node = find(node->parent)) {
		if (!node->isEnabled) {
			return true;
		}
		if (node->parent.serial == 0) {
			return false;
		}
	}
	return false;
}

auto Document::feedbackBlocked(const Node &node) const -> bool {
	for (const auto *current = &node; current != nullptr; current = find(current->parent)) {
		if (!current->isEnabled || current->inert || current->visibility != Visibility::visible) {
			return true;
		}
		if (current->parent.serial == 0) {
			return false;
		}
	}
	return true;
}

auto Document::layerActive(const Node &node, VisualLayer layer) const -> bool {
	switch (layer) {
	case VisualLayer::disabled:
		return effectivelyDisabled(node.id);
	case VisualLayer::hovered:
		return !feedbackBlocked(node) && listed(hoverRoute_, node.id);
	case VisualLayer::pressed:
		return !feedbackBlocked(node) &&
			((pressed_ == node.id && pointerPressInside_) || spaceTarget_ == node.id ||
				enterTarget_ == node.id);
	case VisualLayer::focused:
		return !feedbackBlocked(node) && focused_ == node.id;
	}
	return false;
}

auto Document::paintDomainsForBase(const Node &node, VisualField field) const -> Dirty {
	using enum VisualLayer;
	for (const auto layer : {hovered, pressed, focused, disabled}) {
		if (layerActive(node, layer) && slotHas(slotFor(node, layer), field)) {
			return Dirty::none;
		}
	}
	return Dirty::paint;
}

auto Document::paintDomainsForOverride(const Node &node, VisualLayer layer, VisualField field) const
	-> Dirty {
	if (!layerActive(node, layer)) {
		return Dirty::none;
	}
	const auto maskedBy = [&](VisualLayer candidate) -> bool {
		return layerActive(node, candidate) && slotHas(slotFor(node, candidate), field);
	};
	switch (layer) {
	case VisualLayer::hovered:
		if (maskedBy(VisualLayer::pressed) || maskedBy(VisualLayer::focused) ||
			maskedBy(VisualLayer::disabled)) {
			return Dirty::none;
		}
		break;
	case VisualLayer::pressed:
		if (maskedBy(VisualLayer::focused) || maskedBy(VisualLayer::disabled)) {
			return Dirty::none;
		}
		break;
	case VisualLayer::focused:
		if (maskedBy(VisualLayer::disabled)) {
			return Dirty::none;
		}
		break;
	case VisualLayer::disabled:
		break;
	}
	return Dirty::paint;
}

void Document::notePointerReconcile() {
	pointerReconcileNeeded_ = true;
	// Commit returns before hit testing when nothing else is dirty.
	isDirty_ = true;
	if (updatingHover_) {
		hoverFollowUp_ = true;
	}
}

void Document::invalidateDescendants(NodeId identity, Dirty domains) {
	const auto *node = find(identity);
	if (node == nullptr) {
		return;
	}
	const auto children = node->children;
	for (const auto child : children) {
		invalidate(child, domains);
		invalidateDescendants(child, domains);
	}
}

void Document::refreshPressedVisual() {
	bool inside = false;
	if (pressed_ && pointerInside_) {
		if (const auto hit = hitTest(pointerPosition_)) {
			inside = listed(ancestorRoute(*hit), *pressed_);
		}
	}
	if (inside == pointerPressInside_) {
		return;
	}
	pointerPressInside_ = inside;
	if (pressed_) {
		invalidate(*pressed_, Dirty::paint | Dirty::snapshot);
	}
}

void Document::clearPointerPress() {
	pointerPressInside_ = false;
	if (!pressed_) {
		return;
	}
	const auto identity = *pressed_;
	pressed_.reset();
	if (find(identity) != nullptr) {
		invalidate(identity, Dirty::paint | Dirty::snapshot);
	}
}

void Document::clearKeyLatch(std::optional<NodeId> &slot, std::optional<NodeId> retain) {
	if (!slot || slot == retain) {
		return;
	}
	const auto identity = *slot;
	slot.reset();
	if (find(identity) != nullptr) {
		invalidate(identity, Dirty::paint | Dirty::snapshot);
	}
}

void Document::clearIneligiblePress() {
	if (pressed_ && (find(*pressed_) == nullptr || !isEligible(*pressed_))) {
		clearPointerPress();
	}
	const auto clear = [&](std::optional<NodeId> &slot) -> void {
		if (slot && (find(*slot) == nullptr || !isEligible(*slot) || focused_ != slot)) {
			clearKeyLatch(slot, std::nullopt);
		}
	};
	clear(spaceTarget_);
	clear(enterTarget_);
}

void Document::latchKey(Key key) {
	if (key != Key::space && key != Key::enter) {
		return;
	}
	if (!focused_) {
		return;
	}
	const auto *node = find(*focused_);
	if (node == nullptr || (node->kind != NodeKind::button && node->kind != NodeKind::toggle) ||
		!isEligible(*focused_)) {
		return;
	}
	auto &slot = key == Key::space ? spaceTarget_ : enterTarget_;
	if (slot == focused_) {
		return;
	}
	clearKeyLatch(slot, std::nullopt);
	slot = focused_;
	invalidate(*focused_, Dirty::paint | Dirty::snapshot);
}

void Document::releaseTransientInput() {
	pointerInside_ = false;
	clearPointerPress();
	clearKeyLatch(spaceTarget_, std::nullopt);
	clearKeyLatch(enterTarget_, std::nullopt);
	updateHover(std::nullopt, pointerPosition_);
	if (!isCommitting_) {
		commit();
	}
}

void Document::syncPointerDuringCommit() {
	pointerReconcileNeeded_ = false;
	const auto next = pointerInside_ ? hitTest(pointerPosition_) : std::optional<NodeId>{};
	updateHover(next, pointerPosition_);
	refreshPressedVisual();
	clearIneligiblePress();
}

namespace {
void invalidateHover(Document &document, NodeId identity) {
	const auto *node = document.find(identity);
	auto domains = Dirty::snapshot;
	if (node != nullptr && node->hoveredStyle.any()) {
		domains = domains | Dirty::paint;
	}
	document.invalidate(identity, domains);
}
} // namespace

void Document::updateHover(std::optional<NodeId> next, Point point) {
	if (updatingHover_) {
		hoverFollowUp_ = true;
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
	constexpr std::size_t passLimit = 8;
	for (std::size_t pass = 0; pass < passLimit; ++pass) {
		hoverFollowUp_ = false;
		if (next && find(*next) == nullptr) {
			next.reset();
		}
		const auto nextRoute = next ? ancestorRoute(*next) : std::vector<NodeId>{};
		if (next != hovered_ || nextRoute != hoverRoute_) {
			const auto previousRoute = hoverRoute_;
			hovered_ = next;
			hoverRoute_ = nextRoute;
			for (const auto identity : previousRoute) {
				if (!listed(nextRoute, identity)) {
					invalidateHover(*this, identity);
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
					invalidateHover(*this, identity);
					static_cast<void>(dispatch<PointerEvent>(
						identity,
						EventKind::pointerEnter,
						PointerEvent{.position = point},
						false
					));
				}
			}
		}
		if (!hoverFollowUp_ && !pointerReconcileNeeded_) {
			break;
		}
		pointerReconcileNeeded_ = false;
		hoverFollowUp_ = false;
		point = pointerPosition_;
		next = pointerInside_ ? hitTest(pointerPosition_) : std::optional<NodeId>{};
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

auto Document::focus(NodeId identity, FocusReason reason) -> std::expected<void, Error> {
	const auto *requested = find(identity);
	if (identity.serial != 0 &&
		(!isEligible(identity) || requested == nullptr ||
			(requested->kind != NodeKind::button && requested->kind != NodeKind::toggle &&
				!requested->editor))) {
		return std::unexpected(Error::notFocusable);
	}
	pendingFocus_ = identity.serial == 0 ? std::optional<NodeId>{} : std::optional{identity};
	if (changingFocus_) {
		return {};
	}
	changingFocus_ = true;

	struct FocusGuard {
		bool &changing;

		explicit FocusGuard(bool &value) : changing(value) {}

		FocusGuard(const FocusGuard &) = delete;
		FocusGuard(FocusGuard &&) = delete;
		auto operator=(const FocusGuard &) -> FocusGuard & = delete;
		auto operator=(FocusGuard &&) -> FocusGuard & = delete;

		~FocusGuard() { changing = false; }
	};

	const FocusGuard guard{changingFocus_};
	constexpr std::size_t transitionLimit = 10000;
	std::size_t transitions = 0;
	while (pendingFocus_) {
		if (++transitions > transitionLimit) {
			throw std::logic_error("Focus transitions did not settle");
		}
		auto next = *pendingFocus_;
		pendingFocus_.reset();
		if (focused_ == next) {
			continue;
		}
		const auto previous = focused_;
		std::optional<std::string> blurValue;
		if (const auto *oldNode = previous ? find(*previous) : nullptr;
			oldNode != nullptr && oldNode->editor) {
			blurValue = oldNode->editor->buffer.text();
		}
		if (const auto target = textInputTarget()) {
			const auto finish = textServices_.finishComposition;
			if (finish) {
				if (const auto result = finish(); !result) {
					report(result.error());
				}
			}
			if (auto *node = find(target->node); node != nullptr && node->editor) {
				const auto old = node->editor->buffer.text();
				if (const auto result = node->editor->buffer.commitMarkedText(); !result) {
					report(result.error());
				}
				node->editor->typingGroup = false;
				node->editor->deletionGroup.reset();
				editorChanged(target->node, old);
			}
		}
		if (const auto *oldNode = previous ? find(*previous) : nullptr;
			oldNode != nullptr && oldNode->editor) {
			blurValue = oldNode->editor->buffer.text();
		}
		focused_.reset();
		focusSession_ = stana::detail::nextIdentity();
		capturedEditor_.reset();
		clearKeyLatch(spaceTarget_, std::nullopt);
		clearKeyLatch(enterTarget_, std::nullopt);
		if (previous) {
			invalidate(*previous, Dirty::paint | Dirty::snapshot);
			static_cast<void>(dispatch<FocusEvent>(
				*previous,
				EventKind::blur,
				FocusEvent{.reason = reason, .relatedTarget = next, .value = blurValue},
				true
			));
		}
		if (pendingFocus_) {
			next = *pendingFocus_;
			pendingFocus_.reset();
		}
		if (next && find(*next) != nullptr && isEligible(*next)) {
			focused_ = next;
			focusBookmark_.clear();
			focusBookmarkTarget_.reset();
			clearKeyLatch(spaceTarget_, focused_);
			clearKeyLatch(enterTarget_, focused_);
			invalidate(*next, Dirty::paint | Dirty::snapshot);
			restartCaret(*next);
			commit();
			static_cast<void>(dispatch<FocusEvent>(
				*next,
				EventKind::focus,
				FocusEvent{.reason = reason, .relatedTarget = previous},
				true
			));
		}
	}
	commit();
	return {};
}

void Document::tab(bool backwards) {
	std::vector<NodeId> candidates;
	for (const auto &snapshot : snapshots()) {
		const auto *node = find(snapshot.id);
		if (node != nullptr && isEligible(node->id) &&
			(node->kind == NodeKind::button || node->kind == NodeKind::toggle || node->editor)) {
			candidates.push_back(node->id);
		}
	}
	if (backwards) {
		std::ranges::reverse(candidates);
	}
	if (candidates.empty()) {
		return;
	}
	if (!focused_ && focusBookmarkTarget_ && !focusBookmark_.empty()) {
		auto order = focusBookmark_;
		if (backwards) {
			std::ranges::reverse(order);
		}
		const auto lost = std::ranges::find(order, *focusBookmarkTarget_);
		const auto start = lost == order.end()
			? 0
			: static_cast<std::size_t>(std::distance(order.begin(), lost)) + 1;
		for (std::size_t offset = 0; offset < order.size(); ++offset) {
			const auto candidate = order.at((start + offset) % order.size());
			if (std::ranges::find(candidates, candidate) != candidates.end()) {
				if (const auto result = focus(candidate, FocusReason::traversal); !result) {
					report(result.error());
				}
				return;
			}
		}
	}
	const auto previous = std::ranges::find(candidates, focused_.value_or(NodeId{}));
	const auto next = previous == candidates.end() ? candidates.begin() : std::next(previous);
	if (const auto result =
			focus(next == candidates.end() ? candidates.front() : *next, FocusReason::traversal);
		!result) {
		report(result.error());
	}
}

void Document::queueFocusLoss(NodeId identity, FocusReason reason) {
	if (focused_ != identity) {
		return;
	}
	focusBookmark_.clear();
	for (const auto &snapshot : snapshots()) {
		const auto *record = find(snapshot.id);
		if (record != nullptr &&
			(record->kind == NodeKind::button || record->kind == NodeKind::toggle ||
				record->editor)) {
			focusBookmark_.push_back(record->id);
		}
	}
	focusBookmarkTarget_ = identity;
	std::vector<std::pair<NodeId, std::vector<EventRegistration>>> registrations;
	for (const auto id : ancestorRoute(identity)) {
		if (const auto *record = find(id)) {
			std::vector<EventRegistration> blur;
			for (const auto &registration : record->events) {
				if (registration.kind == EventKind::blur) {
					blur.push_back(registration);
				}
			}
			registrations.emplace_back(id, std::move(blur));
		}
	}
	const auto *record = find(identity);
	const auto value = record != nullptr && record->editor
		? std::optional{record->editor->buffer.text()}
		: std::nullopt;
	const auto weak = weak_from_this();
	reactive::detail::ReactiveRuntime::current().effects.emplace_back(
		[weak, identity, reason, value, registrations] -> void {
			const auto owner = weak.lock();
			if (!owner) {
				return;
			}
			FocusEvent event{.reason = reason, .value = value, .target = identity};
			const auto invoke = [&](const auto &entry, Phase phase) -> void {
				event.current = entry.first;
				event.phase = phase;
				for (const auto &registration : entry.second) {
					if (registration.kind == EventKind::blur && registration.phase == phase) {
						if (const auto *handler = std::get_if<std::function<void(FocusEvent &)>>(
								&registration.handler
							);
							handler != nullptr && *handler) {
							(*handler)(event);
						}
					}
				}
			};
			for (const auto &entry : std::views::reverse(registrations)) {
				invoke(entry, Phase::capture);
			}
			for (const auto &entry : registrations) {
				invoke(entry, Phase::bubble);
			}
		}
	);
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
	pointerDown(PointerInput{.position = point});
}

void Document::pointerDown(PointerInput input) {
	const auto point = input.position;
	stana::detail::requireFinite(point.x);
	stana::detail::requireFinite(point.y);
	if (input.clickCount == 0) {
		throw std::invalid_argument("Pointer click count must be positive");
	}
	commit();
	pointerInside_ = true;
	pointerPosition_ = point;
	const auto target = hitTest(point);
	updateHover(target, point);
	if (!target) {
		if (!isCommitting_) {
			commit();
		}
		return;
	}
	const auto event = dispatch<PointerEvent>(
		*target,
		EventKind::pointerDown,
		PointerEvent{
			.button = input.button,
			.modifiers = input.modifiers,
			.clickCount = input.clickCount,
			.position = point
		},
		true
	);
	if (event.defaultPrevented) {
		if (!isCommitting_) {
			commit();
		}
		return;
	}
	if (input.button != PointerButton::primary) {
		if (!isCommitting_) {
			commit();
		}
		return;
	}
	bool acquired = false;
	for (const auto identity : ancestorRoute(*target)) {
		const auto *node = find(identity);
		if (node != nullptr &&
			(node->kind == NodeKind::button || node->kind == NodeKind::toggle || node->editor)) {
			acquired = true;
			if (!node->editor) {
				pressed_ = identity;
				pointerPressInside_ = true;
				invalidate(identity, Dirty::paint | Dirty::snapshot);
			}
			if (const auto result = focus(identity, FocusReason::pointer); !result) {
				report(result.error());
			}
			if (const auto *current = find(identity); current != nullptr && current->editor) {
				selectEditorAt(identity, point, input.modifiers.shift, input.clickCount == 2);
			}
			break;
		}
	}
	if (!acquired) {
		if (const auto result = focus({}, FocusReason::pointer); !result) {
			report(result.error());
		}
	}
}

void Document::pointerUp(Point point) {
	pointerUp(PointerInput{.position = point});
}

void Document::pointerUp(PointerInput input) {
	const auto point = input.position;
	stana::detail::requireFinite(point.x);
	stana::detail::requireFinite(point.y);
	if (input.clickCount == 0) {
		throw std::invalid_argument("Pointer click count must be positive");
	}
	if (capturedEditor_) {
		if (auto *node = find(*capturedEditor_); node != nullptr && node->editor) {
			node->editor->dragAnchor.reset();
			node->editor->dragGeneration = stana::detail::nextIdentity();
			restartCaret(node->id);
		}
		capturedEditor_.reset();
	}
	pointerInside_ = true;
	pointerPosition_ = point;
	const auto pressed = std::exchange(pressed_, std::nullopt);
	pointerPressInside_ = false;
	if (pressed && find(*pressed) != nullptr) {
		invalidate(*pressed, Dirty::paint | Dirty::snapshot);
	}
	const auto bounds = pressed ? interactionBounds(*pressed) : std::nullopt;
	const bool releasedInside =
		bounds.has_value() && bounds->contains(point) && hitTest(point).has_value();
	const auto target = hitTest(point);
	updateHover(target, point);
	if (target) {
		static_cast<void>(dispatch<PointerEvent>(
			*target,
			EventKind::pointerUp,
			PointerEvent{
				.button = input.button,
				.modifiers = input.modifiers,
				.clickCount = input.clickCount,
				.position = point
			},
			true
		));
	}
	if (pressed && releasedInside) {
		if (const auto result = activate(*pressed); !result) {
			report(result.error());
		}
	}
	if (!isCommitting_) {
		commit();
	}
}

void Document::pointerMove(Point point) {
	pointerMove(PointerInput{.position = point});
}

void Document::pointerMove(PointerInput input) {
	const auto point = input.position;
	stana::detail::requireFinite(point.x);
	stana::detail::requireFinite(point.y);
	if (input.clickCount == 0) {
		throw std::invalid_argument("Pointer click count must be positive");
	}
	if (capturedEditor_) {
		const auto identity = *capturedEditor_;
		selectEditorAt(identity, point, true, false);
		if (auto *node = find(identity); node != nullptr && node->editor) {
			node->editor->dragGeneration = stana::detail::nextIdentity();
			autoscrollEditor(identity, node->editor->dragGeneration);
		}
	}
	pointerInside_ = true;
	pointerPosition_ = point;
	const auto target = hitTest(point);
	updateHover(target, point);
	refreshPressedVisual();
	if (!isCommitting_) {
		commit();
	}
	if (!target) {
		return;
	}
	static_cast<void>(dispatch<PointerEvent>(
		*target,
		EventKind::pointerMove,
		PointerEvent{
			.button = input.button,
			.modifiers = input.modifiers,
			.clickCount = input.clickCount,
			.position = point
		},
		true
	));
}

void Document::pointerLeave() {
	pointerInside_ = false;
	updateHover(std::nullopt, pointerPosition_);
	clearPointerPress();
	if (!isCommitting_) {
		commit();
	}
}

void Document::cancelPointer() {
	if (capturedEditor_) {
		if (auto *node = find(*capturedEditor_); node != nullptr && node->editor) {
			node->editor->dragAnchor.reset();
			node->editor->dragGeneration = stana::detail::nextIdentity();
		}
		capturedEditor_.reset();
	}
	clearPointerPress();
	if (!isCommitting_) {
		commit();
	}
}

void Document::scroll(Point point, Points delta) {
	stana::detail::requireFinite(delta.value());
	pointerInside_ = true;
	pointerPosition_ = point;
	notePointerReconcile();
	const auto target = hitTest(point);
	updateHover(target, point);
	refreshPressedVisual();
	if (!target) {
		if (!isCommitting_) {
			commit();
		}
		return;
	}
	const auto event = dispatch<ScrollEvent>(
		*target,
		EventKind::scroll,
		ScrollEvent{.position = point, .delta = delta},
		true
	);
	if (event.defaultPrevented) {
		if (!isCommitting_) {
			commit();
		}
		return;
	}
	for (auto current = target; current;) {
		auto *node = find(*current);
		if (node == nullptr) {
			return;
		}
		if (node->kind == NodeKind::scroll) {
			if (std::is_eq(delta <=> Points{})) {
				return;
			}
			node->scrollOffset += delta;
			invalidate(node->id, Dirty::arrange);
			commit();
			return;
		}
		current = node->parent.serial == 0 ? std::nullopt : std::optional{node->parent};
	}
	if (!isCommitting_) {
		commit();
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
	if (textServices_.invalidateGeometry) {
		const auto notify = textServices_.invalidateGeometry;
		notify();
	}
	if (pointerInside_) {
		notePointerReconcile();
		if (!isCommitting_) {
			commit();
		}
	}
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

auto Document::reportPresentation(
	stana::rendering::PresentationAttempt attempt,
	PresentationTimePoint timestamp
) -> bool {
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
	if (!std::isfinite(observation.timestamp.time_since_epoch().count()) ||
		observation.timestamp.time_since_epoch().count() <= 0) {
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
	if (pointerInside_) {
		notePointerReconcile();
		if (!isCommitting_) {
			commit();
		}
	}
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
	if (pointerInside_) {
		notePointerReconcile();
		if (!isCommitting_) {
			commit();
		}
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

auto Document::routeKey(Key key, bool released, bool backwards) -> bool {
	const auto event = dispatch<KeyEvent>(
		keyboardTarget(),
		released ? EventKind::keyUp : EventKind::keyDown,
		KeyEvent{.key = key, .backwards = backwards},
		true
	);
	return !event.defaultPrevented && !isClosed_;
}

void Document::keyDown(Key key, bool backwards) {
	if (!routeKey(key, false, backwards)) {
		return;
	}
	if (const auto target = textInputTarget()) {
		const auto sequence = beginInputSequence();
		if (key == Key::enter || key == Key::escape) {
			const auto result = editingCommand(
				*target,
				sequence,
				key == Key::enter ? EditingCommand::submit : EditingCommand::cancel
			);
			endInputSequence(sequence);
			if (!result) {
				report(result.error());
			}
			return;
		}
		if (key == Key::space) {
			const auto result = editText(*target, sequence, {.text = " "});
			endInputSequence(sequence);
			if (!result) {
				report(result.error());
			}
			return;
		}
		if (key == Key::tab) {
			const auto snapshot = textInputSnapshot(*target);
			if (snapshot && snapshot->composition) {
				const auto result =
					editText(*target, sequence, {.kind = text::StoreEdit::Kind::commit});
				endInputSequence(sequence);
				if (!result) {
					report(result.error());
				}
				return;
			}
		}
		endInputSequence(sequence);
	}
	switch (key) {
	case Key::tab:
		tab(backwards);
		break;
	case Key::space:
	case Key::enter: {
		// A repeat arrives as another key-down while the latch is already held.
		const auto held = (key == Key::space ? spaceTarget_ : enterTarget_) == focused_;
		latchKey(key);
		if (!held) {
			keyActivate();
		}
		break;
	}
	case Key::escape:
	case Key::text:
		break;
	}
	if (!isCommitting_) {
		commit();
	}
}

void Document::keyUp(Key key, bool backwards) {
	static_cast<void>(dispatch<KeyEvent>(
		keyboardTarget(),
		EventKind::keyUp,
		KeyEvent{.key = key, .backwards = backwards},
		true
	));
	if (key == Key::space) {
		clearKeyLatch(spaceTarget_, std::nullopt);
	} else if (key == Key::enter) {
		clearKeyLatch(enterTarget_, std::nullopt);
	}
	if (!isCommitting_) {
		commit();
	}
}
} // namespace stana::document::detail
