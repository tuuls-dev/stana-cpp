module;
#include <algorithm>
#include <chrono>
#include <cmath>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
module stana;
import :document;
import :text;
import :tasks;
import :primitives;
import :reactive;

namespace stana::document {
InputSequenceScope::InputSequenceScope(std::shared_ptr<detail::Document> document)
	: document_(std::move(document)),
	  sequence_(document_->beginInputSequence()) {
}

InputSequenceScope::~InputSequenceScope() {
	document_->endInputSequence(sequence_);
}
} // namespace stana::document

namespace stana::document::detail {
namespace {
constexpr std::chrono::milliseconds caretHalfPeriod{500};
constexpr std::chrono::milliseconds autoscrollPeriod{16};

[[nodiscard]] auto resolveFont(Font font, const TypographyOverrides &overrides) -> Font {
	if (overrides.font) {
		font = *overrides.font;
	}
	if (overrides.fontFamily) {
		font.family = overrides.fontFamily;
	}
	if (overrides.fontSize) {
		font.size = *overrides.fontSize;
	}
	if (overrides.fontWeight) {
		font.weight = *overrides.fontWeight;
	}
	if (overrides.fontStyle) {
		font.style = *overrides.fontStyle;
	}
	return font;
}

[[nodiscard]] auto ordered(text::TextRange range) -> text::TextRange {
	return {.start = std::min(range.start, range.end), .end = std::max(range.start, range.end)};
}

[[nodiscard]] auto adjacent(const std::vector<std::size_t> &stops, std::size_t head, bool forward)
	-> std::size_t {
	if (forward) {
		const auto next = std::ranges::upper_bound(stops, head);
		return next == stops.end() ? stops.back() : *next;
	}
	const auto next = std::ranges::lower_bound(stops, head);
	return next == stops.begin() ? stops.front() : *std::prev(next);
}
} // namespace

void Document::setTextServices(TextServices services) {
	if (!services.layout) {
		throw std::invalid_argument("Text layout provider is required");
	}
	if (nodes_.size() > 1) {
		throw std::logic_error("Install text services before mounting");
	}
	textServices_ = std::move(services);
	textCache_.clear();
	textEnvironment_ = stana::detail::nextIdentity();
}

auto Document::resolveTextLayout(Node &node, std::optional<Points> width, bool placeholder)
	-> std::shared_ptr<const text::TextLayout> {
	auto font = resolveFont(node.font, node.textOverrides);
	auto height = node.textOverrides.lineHeight;
	if (placeholder) {
		font = resolveFont(font, node.editor->placeholderStyle);
		if (node.editor->placeholderStyle.lineHeight) {
			height = node.editor->placeholderStyle.lineHeight;
		}
	}
	font.size *= environment_.textScale;
	if (height) {
		*height *= environment_.textScale;
	}
	const text::LayoutRequest request{
		.text = node.editor
			? (placeholder ? node.editor->placeholder : node.editor->buffer.displayedText())
			: node.text,
		.font = font,
		.direction = environment_.direction,
		.wrap = node.editor ? text::Wrap::none : node.wrap,
		.width = node.editor ? std::nullopt : width,
		.lineHeight = height,
		.alignment = node.editor ? Alignment::leading : node.textAlignment,
		.maxLines = node.editor ? std::optional<std::size_t>{1} : node.maxLines,
		.environment = textEnvironment_
	};
	for (const auto &[key, weak] : textCache_) {
		if (key == request) {
			if (const auto cached = weak.lock()) {
				return cached;
			}
		}
	}
	const auto provider = textServices_.layout;
	const auto stamp = mutationStamp_;
	const auto layout = provider(request);
	if (isClosed_ || stamp != mutationStamp_ ||
		!reactive::detail::ReactiveRuntime::current().pending.empty()) {
		throw std::runtime_error("Retained inputs changed during text layout");
	}
	if (!layout || layout->request != request) {
		throw std::runtime_error("Invalid text layout result");
	}
	stana::detail::requireNonnegative(layout->size.width);
	stana::detail::requireNonnegative(layout->size.height);
	constexpr std::size_t cacheLimit = 256;
	std::erase_if(textCache_, [](const auto &entry) -> bool {
		return entry.second.expired();
	});
	if (textCache_.size() >= cacheLimit) {
		textCache_.erase(textCache_.begin());
	}
	textCache_.emplace_back(request, layout);
	return layout;
}

auto Document::beginInputSequence() -> InputSequence {
	const InputSequence sequence{stana::detail::nextIdentity()};
	inputSequences_.push_back({.id = sequence});
	return sequence;
}

void Document::endInputSequence(InputSequence sequence) {
	std::erase_if(inputSequences_, [sequence](const SequenceState &state) -> bool {
		return state.id == sequence;
	});
}

auto Document::sequenceState(InputSequence sequence) -> SequenceState * {
	for (auto &state : inputSequences_) {
		if (state.id == sequence) {
			return &state;
		}
	}
	return nullptr;
}

auto Document::textInputTarget() const -> std::optional<TextInputTarget> {
	const auto *node = focused_ ? find(*focused_) : nullptr;
	if (!inputActive_ || node == nullptr || !node->editor || !isEligible(node->id)) {
		return std::nullopt;
	}
	return TextInputTarget{
		.node = node->id,
		.session = focusSession_,
		.stamp = node->editor->buffer.stamp()
	};
}

auto Document::validateEditor(TextInputTarget target) const -> std::expected<Node *, Error> {
	if (target.node.document != identity_) {
		return std::unexpected(Error::wrongDocument);
	}
	auto *node = find(target.node);
	if (node == nullptr || !node->scope.alive()) {
		return std::unexpected(Error::staleHandle);
	}
	if (!node->editor || !inputActive_ || !isEligible(target.node) || focused_ != target.node) {
		return std::unexpected(Error::notFocusable);
	}
	if (target.session != focusSession_) {
		return std::unexpected(Error::cancelled);
	}
	if (target.stamp != node->editor->buffer.stamp()) {
		return std::unexpected(Error::invalidTextRange);
	}
	return node;
}

auto Document::textInputSnapshot(TextInputTarget target) const
	-> std::expected<text::StoreSnapshot, Error> {
	const auto node = validateEditor(target);
	if (!node) {
		return std::unexpected(node.error());
	}
	return (*node)->editor->buffer.snapshot();
}

void Document::externalEditorChanged(NodeId identity) {
	restartCaret(identity);
	const auto target = textInputTarget();
	if (!target || target->node != identity) {
		return;
	}
	const auto weak = weak_from_this();
	reactive::detail::ReactiveRuntime::current().effects.emplace_back(
		[weak, captured = *target] -> void {
			const auto owner = weak.lock();
			if (!owner || !owner->validateEditor(captured)) {
				return;
			}
			const auto discard = owner->textServices_.discardComposition;
			if (discard) {
				discard();
			}
		}
	);
}

void Document::editorChanged(NodeId identity, const std::string &previous) {
	auto *node = find(identity);
	if (node == nullptr || !node->editor) {
		return;
	}
	node->text = node->editor->buffer.text();
	const auto value = node->text;
	const auto binding = node->editor->binding;
	const auto display = node->editor->buffer.displayedText();
	const auto &effectiveText = display;
	const bool layoutChanged = !node->textLayout || node->textLayout->request.text != effectiveText;
	invalidate(
		identity,
		(layoutChanged ? Dirty::measure : Dirty::arrange) | Dirty::paint | Dirty::snapshot
	);
	restartCaret(identity);
	if (value != previous) {
		node = nullptr;
		binding.set(value);
	}
	// A binding setter may remove this occurrence or close the entire document.
	if (find(identity) != nullptr && textServices_.invalidateGeometry) {
		const auto notify = textServices_.invalidateGeometry;
		notify();
	}
}

auto Document::editText(TextInputTarget target, InputSequence sequence, text::StoreEdit edit)
	-> std::expected<void, Error> {
	const auto result = validateEditor(target);
	if (!result) {
		return std::unexpected(result.error());
	}
	auto *state = sequenceState(sequence);
	if (state == nullptr) {
		return std::unexpected(Error::cancelled);
	}
	auto *node = *result;
	const auto before = node->editor->buffer.snapshot();
	edit.stamp = target.stamp;
	edit.groupWithPrevious = edit.kind == text::StoreEdit::Kind::replace && !edit.replacement &&
		!before.composition && node->editor->typingGroup && edit.text.size() != 0;
	const auto changed = node->editor->buffer.apply(edit);
	if (!changed) {
		return changed;
	}
	state->compositionConsumed = state->compositionConsumed || before.composition.has_value() ||
		edit.kind == text::StoreEdit::Kind::compose;
	node->editor->typingGroup = edit.kind == text::StoreEdit::Kind::replace && !edit.replacement &&
		!before.composition && !edit.text.empty();
	node->editor->deletionGroup.reset();
	if (before.stamp != node->editor->buffer.stamp()) {
		reactive::Transaction::perform([&] -> void {
			editorChanged(target.node, before.text);
		});
	}
	return {};
}

auto Document::canEdit(TextInputTarget target, EditingCommand command) const -> bool {
	const auto result = validateEditor(target);
	if (!result) {
		return false;
	}
	const auto snapshot = (*result)->editor->buffer.snapshot();
	const bool selection =
		snapshot.selection.anchor.utf8Offset != snapshot.selection.head.utf8Offset;
	using enum EditingCommand;
	if (command == copy || command == cut) {
		return selection && static_cast<bool>(textServices_.writeClipboard);
	}
	if (command == paste) {
		return static_cast<bool>(textServices_.readClipboard);
	}
	if (command == undo) {
		return snapshot.canUndo;
	}
	if (command == redo) {
		return snapshot.canRedo;
	}
	if (command == selectAll) {
		return !snapshot.text.empty();
	}
	return true;
}

auto Document::editingCommand(
	TextInputTarget target,
	InputSequence sequence,
	EditingCommand command
) -> std::expected<void, Error> {
	const auto checked = validateEditor(target);
	if (!checked) {
		return std::unexpected(checked.error());
	}
	auto *state = sequenceState(sequence);
	if (state == nullptr) {
		return std::unexpected(Error::cancelled);
	}
	auto *node = *checked;
	auto &editor = *node->editor;
	using enum EditingCommand;
	const auto before = editor.buffer.snapshot();
	const bool traversal = command == nextFocus || command == previousFocus;
	if ((command == submit || command == cancel || traversal) &&
		(state->compositionConsumed || before.composition)) {
		state->compositionConsumed = true;
		const auto outcome = command != cancel
			? editor.buffer.commitMarkedText()
			: editor.buffer.apply(
				  {.kind = text::StoreEdit::Kind::cancel, .stamp = editor.buffer.stamp()}
			  );
		if (outcome) {
			reactive::Transaction::perform([&] -> void {
				editorChanged(target.node, before.text);
			});
		}
		return outcome;
	}
	if (traversal) {
		tab(command == previousFocus);
		return {};
	}
	if (!canEdit(target, command)) {
		return {};
	}
	const auto oldDeletion = editor.deletionGroup;
	editor.typingGroup = false;
	editor.deletionGroup.reset();
	if (command == submit || command == cancel) {
		const auto callback = command == submit ? editor.onSubmit : editor.onCancel;
		const TextInputEvent event{.target = target.node, .value = before.text};
		if (callback) {
			reactive::Transaction::perform([&] -> void {
				callback(event);
			});
		}
		return {};
	}
	if (before.composition) {
		editor.buffer.cancelMarkedText();
		state->compositionConsumed = true;
		editorChanged(target.node, before.text);
		node = find(target.node);
		if (node == nullptr || !node->editor) {
			return std::unexpected(Error::staleHandle);
		}
		if (textServices_.discardComposition) {
			target.stamp = node->editor->buffer.stamp();
			const auto notify = textServices_.discardComposition;
			node = nullptr;
			notify();
			const auto current = validateEditor(target);
			if (!current) {
				return std::unexpected(current.error());
			}
			node = *current;
		}
	}
	// Never keep an editor reference across host callbacks below.
	auto snapshot = node->editor->buffer.snapshot();
	auto range = ordered(
		{.start = snapshot.selection.anchor.utf8Offset, .end = snapshot.selection.head.utf8Offset}
	);
	const auto content = snapshot.text;
	if (command == copy || command == cut) {
		const auto stamp = node->editor->buffer.stamp();
		const auto write = textServices_.writeClipboard;
		const auto value = content.substr(range.start, range.end - range.start);
		node = nullptr;
		const auto outcome = write(value);
		if (!outcome) {
			return outcome;
		}
		target.stamp = stamp;
		const auto current = validateEditor(target);
		if (!current) {
			return std::unexpected(current.error());
		}
		node = *current;
		if (command == cut) {
			const auto deleted = node->editor->buffer.replaceSelection("");
			if (!deleted) {
				return deleted;
			}
		}
	} else if (command == paste) {
		const auto stamp = node->editor->buffer.stamp();
		const auto read = textServices_.readClipboard;
		node = nullptr;
		const auto value = read();
		if (!value) {
			return std::unexpected(value.error());
		}
		target.stamp = stamp;
		const auto current = validateEditor(target);
		if (!current) {
			return std::unexpected(current.error());
		}
		node = *current;
		if (*value && !(*value)->empty()) {
			const auto insertion = text::normalizePaste(**value);
			if (!insertion) {
				return std::unexpected(insertion.error());
			}
			const auto replaced = node->editor->buffer.replaceSelection(*insertion);
			if (!replaced) {
				return replaced;
			}
		}
	} else if (command == undo || command == redo) {
		static_cast<void>(
			command == undo ? node->editor->buffer.undo() : node->editor->buffer.redo()
		);
	} else {
		const auto graphemes = text::graphemeBoundaries(content);
		const auto words = text::wordBoundaries(content);
		const auto head = snapshot.selection.head.utf8Offset;
		std::size_t position = head;
		bool extend = false;
		bool deleting = false;
		bool forward = false;
		bool word = false;
		if (command == selectAll) {
			range = {.start = 0, .end = content.size()};
			position = range.end;
			extend = true;
		}
		extend = extend || command == selectLeft || command == selectRight ||
			command == selectWordBackward || command == selectWordForward ||
			command == selectLineStart || command == selectLineEnd;
		deleting = command == deleteBackward || command == deleteForward ||
			command == deleteWordBackward || command == deleteWordForward;
		forward = command == right || command == selectRight || command == wordForward ||
			command == selectWordForward || command == deleteForward ||
			command == deleteWordForward;
		word = command == wordBackward || command == wordForward || command == selectWordBackward ||
			command == selectWordForward || command == deleteWordBackward ||
			command == deleteWordForward;
		const bool start = command == lineStart || command == selectLineStart;
		const bool end = command == lineEnd || command == selectLineEnd;
		if (start || end) {
			position = start ? 0 : content.size();
			if (node->textLayout && !node->textLayout->lines.empty()) {
				const auto &stops = node->textLayout->lines.front().shape.carets;
				const auto edge = std::ranges::minmax_element(stops, {}, &text::CaretStop::primary);
				if (edge.min != stops.end()) {
					position = (start ? edge.min : edge.max)->offset;
				}
			}
		} else if (command != selectAll) {
			position = adjacent(word ? words : graphemes, head, forward);
			if (!deleting && !word && node->textLayout && !node->textLayout->lines.empty()) {
				using VisualStop = std::tuple<Points, std::size_t, bool>;
				std::vector<VisualStop> visual;
				for (const auto &stop : node->textLayout->lines.front().shape.carets) {
					if (stop.isGrapheme) {
						visual.emplace_back(stop.primary, stop.offset, false);
						if (stop.secondary != stop.primary) {
							visual.emplace_back(stop.secondary, stop.offset, true);
						}
					}
				}
				std::ranges::sort(visual, [](const auto &a, const auto &b) -> bool {
					if (std::get<0>(a) != std::get<0>(b)) {
						return std::get<0>(a) < std::get<0>(b);
					}
					return std::get<1>(a) < std::get<1>(b);
				});
				for (std::size_t index = 0; index < visual.size(); ++index) {
					const auto &stop = visual.at(index);
					if (std::get<1>(stop) == head &&
						std::get<2>(stop) == node->editor->secondaryAffinity) {
						const auto &next = visual.at(
							forward ? std::min(index + 1, visual.size() - 1)
									: (index == 0 ? 0 : index - 1)
						);
						position = std::get<1>(next);
						node->editor->secondaryAffinity = std::get<2>(next);
						break;
					}
				}
			}
			if (range.start != range.end && !extend && !deleting) {
				position = forward ? range.end : range.start;
				if (node->textLayout) {
					const auto a = node->textLayout->caret(range.start).origin.x;
					const auto b = node->textLayout->caret(range.end).origin.x;
					position = (forward ? a > b : a < b) ? range.start : range.end;
				}
			}
		}
		const auto anchor =
			command == selectAll ? 0 : (extend ? snapshot.selection.anchor.utf8Offset : position);
		if (deleting) {
			if (range.start == range.end) {
				range = ordered({.start = head, .end = position});
			}
			const bool group = !word && oldDeletion == command;
			const auto erased = node->editor->buffer.apply(
				{.kind = text::StoreEdit::Kind::replace,
					.stamp = node->editor->buffer.stamp(),
					.space = text::TextSpace::committed,
					.replacement = range,
					.groupWithPrevious = group}
			);
			if (!erased) {
				return erased;
			}
			node->editor->deletionGroup = command;
		} else {
			const auto selected = node->editor->buffer.select(
				{.anchor = {.utf8Offset = anchor, .revision = node->editor->buffer.revision()},
					.head = {.utf8Offset = position, .revision = node->editor->buffer.revision()}}
			);
			if (!selected) {
				return selected;
			}
		}
	}
	if (find(target.node) != nullptr) {
		reactive::Transaction::perform([&] -> void {
			editorChanged(target.node, before.text);
		});
	}
	return {};
}

void Document::restartCaret(NodeId identity) {
	auto *node = find(identity);
	if (node == nullptr || !node->editor) {
		return;
	}
	node->editor->caretVisible = true;
	const auto generation = stana::detail::nextIdentity();
	node->editor->blinkGeneration = generation;
	if (focused_ == identity && inputActive_) {
		blinkCaret(identity, generation);
	}
}

void Document::blinkCaret(NodeId identity, std::uint64_t generation) {
	const auto weak = weak_from_this();
	tasks::detail::TaskScheduler::current().post(
		caretHalfPeriod,
		[weak, identity, generation] -> void {
			const auto owner = weak.lock();
			if (!owner || !owner->inputActive_ || owner->focused_ != identity ||
				!owner->isEligible(identity)) {
				return;
			}
			auto *node = owner->find(identity);
			if (node == nullptr || !node->editor || node->editor->blinkGeneration != generation ||
				node->editor->dragAnchor) {
				return;
			}
			node->editor->caretVisible = !node->editor->caretVisible;
			owner->invalidate(identity, Dirty::paint | Dirty::snapshot);
			owner->commit();
			owner->blinkCaret(identity, generation);
		}
	);
}

void Document::setInputActive(bool active) {
	if (active == inputActive_) {
		return;
	}
	if (!active && focused_) {
		const auto target = textInputTarget();
		if (target) {
			const auto finish = textServices_.finishComposition;
			if (finish) {
				if (const auto result = finish(); !result) {
					report(result.error());
				}
			}
			if (auto *node = find(target->node); node != nullptr && node->editor) {
				const auto previous = node->editor->buffer.text();
				if (const auto result = node->editor->buffer.commitMarkedText(); !result) {
					report(result.error());
				}
				editorChanged(target->node, previous);
			}
		}
	}
	inputActive_ = active;
	focusSession_ = stana::detail::nextIdentity();
	capturedEditor_.reset();
	if (const auto focused = focused_) {
		invalidate(*focused, Dirty::paint | Dirty::snapshot);
		restartCaret(*focused);
	}
	commit();
}

void Document::reconcileEditors() {
	if (focused_ && !isEligible(*focused_)) {
		const auto identity = *focused_;
		queueFocusLoss(identity, FocusReason::eligibility);
		focused_.reset();
		focusSession_ = stana::detail::nextIdentity();
		capturedEditor_.reset();
		if (auto *node = find(identity); node != nullptr && node->editor) {
			node->editor->buffer.cancelMarkedText();
			node->editor->dragAnchor.reset();
			node->editor->blinkGeneration = stana::detail::nextIdentity();
		}
		invalidate(identity, Dirty::paint | Dirty::snapshot);
	}
}

auto Document::textCaretRect(TextInputTarget target, std::size_t offset) const
	-> std::expected<Rect, Error> {
	const auto checked = validateEditor(target);
	if (!checked) {
		return std::unexpected(checked.error());
	}
	const auto *node = *checked;
	const auto bounds = interactionBounds(target.node);
	if (!bounds || !node->textLayout ||
		!text::utf16Offset(node->editor->buffer.displayedText(), offset)) {
		return std::unexpected(Error::cancelled);
	}
	const auto geometry = rendering::detail::geometryFind(
		usesPresentationGeometry_
			? confirmedGeometry_
			: (render_ ? render_->geometry : rendering::detail::GeometryRoot{}),
		target.node
	);
	if (!geometry || geometry->textStamp != target.stamp ||
		geometry->textLayout != node->textLayout) {
		return std::unexpected(Error::cancelled);
	}
	auto rect = node->textLayout->caret(offset, node->editor->secondaryAffinity);
	rect.origin.x += geometry->textOrigin.x - geometry->textScroll;
	rect.origin.y += geometry->textOrigin.y;
	if (usesPresentationGeometry_ &&
		(surfaceGated_ || std::ranges::any_of(ambiguousRegions_, [rect](Rect region) -> bool {
			return rect.origin.x < region.origin.x + region.size.width &&
				region.origin.x < rect.origin.x + rect.size.width &&
				rect.origin.y < region.origin.y + region.size.height &&
				region.origin.y < rect.origin.y + rect.size.height;
		}))) {
		return std::unexpected(Error::cancelled);
	}
	const auto &clip = geometry->textClip;
	const auto left = Points{std::max(rect.origin.x.value(), clip.origin.x.value())};
	const auto top = Points{std::max(rect.origin.y.value(), clip.origin.y.value())};
	const auto right = Points{std::min(
		(rect.origin.x + rect.size.width).value(),
		(clip.origin.x + clip.size.width).value()
	)};
	const auto bottom = Points{std::min(
		(rect.origin.y + rect.size.height).value(),
		(clip.origin.y + clip.size.height).value()
	)};
	if (right <= left || bottom <= top) {
		return std::unexpected(Error::cancelled);
	}
	return Rect{
		.origin = {.x = left, .y = top},
		.size = {.width = right - left, .height = bottom - top}
	};
}

auto Document::textIndexAt(Point point, bool graphemesOnly) const
	-> std::expected<std::size_t, Error> {
	if (usesPresentationGeometry_ &&
		(surfaceGated_ || std::ranges::any_of(ambiguousRegions_, [point](Rect rect) -> bool {
			return rect.contains(point);
		}))) {
		return std::unexpected(Error::cancelled);
	}
	const auto target = textInputTarget();
	if (!target) {
		return std::unexpected(Error::notFocusable);
	}
	const auto checked = validateEditor(*target);
	if (!checked) {
		return std::unexpected(checked.error());
	}
	const auto geometry = rendering::detail::geometryFind(
		usesPresentationGeometry_
			? confirmedGeometry_
			: (render_ ? render_->geometry : rendering::detail::GeometryRoot{}),
		target->node
	);
	if (!geometry || geometry->textStamp != target->stamp || !geometry->textLayout ||
		!interactionBounds(target->node)) {
		return std::unexpected(Error::cancelled);
	}
	return geometry->textLayout->hit(
		{.x = point.x - geometry->textOrigin.x + geometry->textScroll,
			.y = point.y - geometry->textOrigin.y},
		graphemesOnly
	);
}

void Document::selectEditorAt(NodeId identity, Point point, bool extend, bool word) {
	const auto target = textInputTarget();
	if (!target || target->node != identity) {
		return;
	}
	const auto hit = textIndexAt(point);
	if (!hit) {
		return;
	}
	auto *node = find(identity);
	const auto previous = node->editor->buffer.text();
	if (node->editor->buffer.snapshot().composition) {
		const auto displayed = node->editor->buffer.displayedText();
		const auto finish = textServices_.finishComposition;
		if (finish) {
			if (const auto result = finish(); !result) {
				report(result.error());
				return;
			}
		}
		node = find(identity);
		if (node == nullptr || !node->editor || focused_ != identity) {
			return;
		}
		if (const auto result = node->editor->buffer.commitMarkedText(); !result) {
			report(result.error());
			return;
		}
		if (node->editor->buffer.displayedText() != displayed) {
			return;
		}
	}
	const auto snapshot = node->editor->buffer.snapshot();
	const auto geometry = rendering::detail::geometryFind(
		usesPresentationGeometry_ ? confirmedGeometry_ : render_->geometry,
		identity
	);
	if (geometry && geometry->textLayout) {
		const auto primary = geometry->textLayout->caret(*hit).origin.x;
		const auto secondary = geometry->textLayout->caret(*hit, true).origin.x;
		const auto x = point.x - geometry->textOrigin.x + geometry->textScroll;
		node->editor->secondaryAffinity =
			std::abs((x - secondary).value()) < std::abs((x - primary).value());
	}
	std::size_t anchor = extend ? snapshot.selection.anchor.utf8Offset : *hit;
	std::size_t head = *hit;
	if (word) {
		const auto stops = text::wordBoundaries(snapshot.text);
		anchor = adjacent(stops, *hit, false);
		head = adjacent(stops, *hit, true);
	}
	if (const auto selected = node->editor->buffer.select(
			{.anchor = {.utf8Offset = anchor, .revision = node->editor->buffer.revision()},
				.head = {.utf8Offset = head, .revision = node->editor->buffer.revision()}}
		);
		!selected) {
		report(selected.error());
		return;
	}
	node->editor->dragAnchor = anchor;
	node->editor->typingGroup = false;
	node->editor->deletionGroup.reset();
	capturedEditor_ = identity;
	editorChanged(identity, previous);
	commit();
}

auto Document::contextMenuTarget(Point point) -> std::optional<TextInputTarget> {
	const auto hit = hitTest(point);
	const auto *node = hit ? find(*hit) : nullptr;
	if (node == nullptr || !node->editor) {
		return std::nullopt;
	}
	if (const auto focused = focus(node->id); !focused) {
		return std::nullopt;
	}
	return textInputTarget();
}

void Document::autoscrollEditor(NodeId identity, std::uint64_t generation) {
	const auto weak = weak_from_this();
	tasks::detail::TaskScheduler::current().post(
		autoscrollPeriod,
		[weak, identity, generation] -> void {
			const auto owner = weak.lock();
			if (!owner || !owner->inputActive_ || owner->capturedEditor_ != identity ||
				!owner->isEligible(identity)) {
				return;
			}
			auto *node = owner->find(identity);
			if (node == nullptr || !node->editor || !node->editor->dragAnchor ||
				node->editor->dragGeneration != generation) {
				return;
			}
			const auto content = layout::detail::contentRect(
				node->bounds,
				node->padding,
				owner->environment_.direction
			);
			const double x = owner->pointerPosition_.x.value();
			const double outside = x < content.origin.x.value()
				? x - content.origin.x.value()
				: std::max(0.0, x - content.origin.x.value() - content.size.width.value());
			const Points delta{std::clamp(12 * outside, -1200.0, 1200.0) * 0.016};
			if (std::is_eq(delta <=> Points{})) {
				return;
			}
			const auto previous = node->editor->scroll;
			const double maximum = node->textLayout
				? std::max(0.0, node->textLayout->size.width.value() - content.size.width.value())
				: 0;
			node->editor->scroll = Points{std::clamp((previous + delta).value(), 0.0, maximum)};
			if (node->editor->scroll == previous) {
				return;
			}
			owner->invalidate(identity, Dirty::paint | Dirty::snapshot);
			owner->commit();
			owner->selectEditorAt(identity, owner->pointerPosition_, true, false);
			owner->autoscrollEditor(identity, generation);
		}
	);
}

} // namespace stana::document::detail
