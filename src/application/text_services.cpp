module;
#include "../platform/text_contracts.hpp"
#include <algorithm>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
module stana;
import :application;
import :document;
import :text;
#include "text_services.hpp"

namespace stana::application::detail {
namespace {
/// @brief Immutable wrapper defined in this translation unit so module RTTI has one owner.
struct NativeTextResource final : ::stana::text::PaintResource,
								  ::stana::platform::detail::TextResource {
	std::shared_ptr<const ::stana::platform::detail::TextResource> native;

	explicit NativeTextResource(
		std::shared_ptr<const ::stana::platform::detail::TextResource> value
	)
		: native(std::move(value)) {}

	[[nodiscard]] auto nativeOwner() const
		-> std::shared_ptr<const ::stana::platform::detail::TextResource> override {
		return native;
	}
};
namespace native = ::stana::platform::detail;

[[nodiscard]] auto portableTarget(native::NativeTextTarget target) -> document::TextInputTarget {
	return {
		.node = {.document = target.document, .serial = target.node},
		.session = target.session,
		.stamp = {target.store}
	};
}

[[nodiscard]] auto nativeTarget(document::TextInputTarget target) -> native::NativeTextTarget {
	return {
		.document = target.node.document,
		.node = target.node.serial,
		.session = target.session,
		.store = target.stamp.revision
	};
}

[[nodiscard]] auto portableCommand(native::TextCommand command) -> document::EditingCommand {
	using Command = document::EditingCommand;
	switch (command) {
	case native::TextCommand::left:
		return Command::left;
	case native::TextCommand::right:
		return Command::right;
	case native::TextCommand::wordBackward:
		return Command::wordBackward;
	case native::TextCommand::wordForward:
		return Command::wordForward;
	case native::TextCommand::lineStart:
		return Command::lineStart;
	case native::TextCommand::lineEnd:
		return Command::lineEnd;
	case native::TextCommand::selectLeft:
		return Command::selectLeft;
	case native::TextCommand::selectRight:
		return Command::selectRight;
	case native::TextCommand::selectWordBackward:
		return Command::selectWordBackward;
	case native::TextCommand::selectWordForward:
		return Command::selectWordForward;
	case native::TextCommand::selectLineStart:
		return Command::selectLineStart;
	case native::TextCommand::selectLineEnd:
		return Command::selectLineEnd;
	case native::TextCommand::deleteBackward:
		return Command::deleteBackward;
	case native::TextCommand::deleteForward:
		return Command::deleteForward;
	case native::TextCommand::deleteWordBackward:
		return Command::deleteWordBackward;
	case native::TextCommand::deleteWordForward:
		return Command::deleteWordForward;
	case native::TextCommand::selectAll:
		return Command::selectAll;
	case native::TextCommand::cut:
		return Command::cut;
	case native::TextCommand::copy:
		return Command::copy;
	case native::TextCommand::paste:
		return Command::paste;
	case native::TextCommand::undo:
		return Command::undo;
	case native::TextCommand::redo:
		return Command::redo;
	case native::TextCommand::submit:
		return Command::submit;
	case native::TextCommand::cancel:
		return Command::cancel;
	case native::TextCommand::nextFocus:
		return Command::nextFocus;
	case native::TextCommand::previousFocus:
		return Command::previousFocus;
	}
	throw std::logic_error("Unknown text command");
}

[[nodiscard]] auto shapeLine(const std::string &value, const Font &font, Direction direction)
	-> text::ShapedLine {
	const auto shaped = native::shapeTextLine(
		value,
		{.size = font.size.value(),
			.weight = font.weight.value(),
			.family = font.family,
			.italic = font.style == FontStyle::italic},
		direction == Direction::rightToLeft
	);
	if (!shaped) {
		throw std::runtime_error(shaped.error());
	}
	text::ShapedLine line{
		.width = Points{shaped->width},
		.ascent = Points{shaped->ascent},
		.descent = Points{shaped->descent},
		.leading = Points{shaped->leading},
		.xHeight = Points{shaped->xHeight},
		.underlineThickness = Points{shaped->underlineThickness}
	};
	const auto graphemes = text::graphemeBoundaries(value);
	const auto scalars = text::scalarBoundaries(value);
	const auto byteOffset = [&](std::size_t units) -> std::expected<std::size_t, Error> {
		const auto found =
			std::ranges::lower_bound(scalars, units, {}, &text::ScalarBoundary::utf16Offset);
		if (found == scalars.end() || found->utf16Offset != units) {
			return std::unexpected(Error::invalidTextRange);
		}
		return found->utf8Offset;
	};
	for (const auto &stop : shaped->carets) {
		const auto offset = byteOffset(stop.utf16Offset);
		if (!offset) {
			throw std::runtime_error("Invalid native caret offset");
		}
		line.carets.push_back(
			{.offset = *offset,
				.primary = Points{stop.primary},
				.secondary = Points{stop.secondary},
				.isGrapheme = std::ranges::binary_search(graphemes, *offset)}
		);
	}
	for (const auto &run : shaped->runs) {
		const auto start = byteOffset(run.start), end = byteOffset(run.end);
		if (!start || !end) {
			throw std::runtime_error("Invalid native run offset");
		}
		line.runs.push_back(
			{.source = {.start = *start, .end = *end},
				.x = Points{run.x},
				.width = Points{run.width},
				.rightToLeft = run.rightToLeft}
		);
	}
	line.resource = std::make_shared<NativeTextResource>(shaped->resource);
	return line;
}

[[nodiscard]] auto checkedRange(const std::string &displayed, native::NativeTextRange range)
	-> std::expected<text::TextRange, Error> {
	if (range.start > range.end) {
		return std::unexpected(Error::invalidTextRange);
	}
	const auto start = text::utf8Offset(displayed, range.start),
			   end = text::utf8Offset(displayed, range.end);
	if (!start || !end) {
		return std::unexpected(Error::invalidTextRange);
	}
	return text::TextRange{.start = *start, .end = *end};
}

[[nodiscard]] auto displaySnapshot(const text::StoreSnapshot &snapshot) -> std::string {
	auto display = snapshot.text;
	if (snapshot.composition) {
		const auto &composition = *snapshot.composition;
		display.replace(
			composition.replacement.start,
			composition.replacement.end - composition.replacement.start,
			composition.text
		);
	}
	return display;
}
} // namespace

auto installTextServices(const std::shared_ptr<document::detail::Document> &document)
	-> native::TextInputCallbacks {
	const auto host = std::make_shared<native::TextInputHost>();
	document::detail::TextServices services;
	services.layout =
		[](const text::LayoutRequest &request) -> std::shared_ptr<const ::stana::text::TextLayout> {
		return text::layoutText(request, shapeLine);
	};
	services.readClipboard = [] -> std::expected<std::optional<std::string>, Error> {
		const auto result = native::readTextClipboard();
		if (!result) {
			return std::unexpected(Error::nativeFailure);
		}
		return *result;
	};
	services.writeClipboard = [](const std::string &value) -> std::expected<void, Error> {
		if (native::writeTextClipboard(value)) {
			return {};
		}
		return std::unexpected(Error::nativeFailure);
	};
	services.finishComposition = [host] -> std::expected<void, Error> {
		if (!host->finish) {
			return std::unexpected(Error::unsupportedPlatform);
		}
		return host->finish() ? std::expected<void, Error>{}
							  : std::unexpected(Error::nativeFailure);
	};
	services.discardComposition = [host] -> void {
		if (host->discard) {
			host->discard();
		}
	};
	services.invalidateGeometry = [host] -> void {
		if (host->invalidateGeometry) {
			host->invalidateGeometry();
		}
	};
	document->setTextServices(std::move(services));
	document->setInputActive(false);
	const auto weak = std::weak_ptr{document};
	native::TextInputCallbacks callbacks{.host = host};
	callbacks.state = [weak] -> std::optional<native::NativeTextState> {
		const auto owner = weak.lock();
		const auto target = owner ? owner->textInputTarget() : std::nullopt;
		if (!target) {
			return std::nullopt;
		}
		const auto snapshot = owner->textInputSnapshot(*target);
		if (!snapshot) {
			return std::nullopt;
		}
		native::NativeTextState result{
			.target = nativeTarget(*target),
			.displayed = displaySnapshot(*snapshot)
		};
		auto a = snapshot->selection.anchor.utf8Offset, h = snapshot->selection.head.utf8Offset;
		if (snapshot->composition) {
			const auto &composition = *snapshot->composition;
			a = composition.replacement.start + composition.selection.start;
			h = composition.replacement.start + composition.selection.end;
			result.marked = native::NativeTextRange{
				.start = *text::utf16Offset(result.displayed, composition.replacement.start),
				.end = *text::utf16Offset(
					result.displayed,
					composition.replacement.start + composition.text.size()
				)
			};
		}
		result.selected = {
			.start = *text::utf16Offset(result.displayed, std::min(a, h)),
			.end = *text::utf16Offset(result.displayed, std::max(a, h))
		};
		return result;
	};
	callbacks.begin = [weak] -> std::uint64_t {
		const auto owner = weak.lock();
		return owner ? owner->beginInputSequence().identity : 0;
	};
	callbacks.end = [weak](std::uint64_t sequence) -> void {
		if (const auto owner = weak.lock()) {
			owner->endInputSequence({sequence});
		}
	};
	callbacks.edit = [weak](
						 native::NativeTextTarget target,
						 std::uint64_t sequence,
						 const native::NativeTextEdit &edit
					 ) -> bool {
		const auto owner = weak.lock();
		if (!owner) {
			return false;
		}
		const auto snapshot = owner->textInputSnapshot(portableTarget(target));
		if (!snapshot) {
			owner->report(snapshot.error());
			return false;
		}
		text::StoreEdit operation;
		switch (edit.kind) {
		case native::NativeTextEdit::Kind::insert:
			operation.kind = text::StoreEdit::Kind::replace;
			break;
		case native::NativeTextEdit::Kind::mark:
			operation.kind = text::StoreEdit::Kind::compose;
			break;
		case native::NativeTextEdit::Kind::commit:
			operation.kind = text::StoreEdit::Kind::commit;
			break;
		case native::NativeTextEdit::Kind::cancel:
			operation.kind = text::StoreEdit::Kind::cancel;
			break;
		}
		operation.text = edit.text;
		if (edit.replacement) {
			const auto range = checkedRange(displaySnapshot(*snapshot), *edit.replacement);
			if (!range) {
				owner->report(range.error());
				return false;
			}
			operation.replacement = *range;
		}
		if (operation.kind == text::StoreEdit::Kind::compose) {
			const auto selected = checkedRange(edit.text, edit.selection);
			if (!selected) {
				owner->report(selected.error());
				return false;
			}
			operation.selection = *selected;
		}
		const auto result = owner->editText(portableTarget(target), {sequence}, operation);
		if (!result) {
			owner->report(result.error());
		}
		return result.has_value();
	};
	callbacks.command = [weak](
							native::NativeTextTarget target,
							std::uint64_t sequence,
							native::TextCommand command
						) -> bool {
		const auto owner = weak.lock();
		if (!owner) {
			return false;
		}
		const auto result =
			owner->editingCommand(portableTarget(target), {sequence}, portableCommand(command));
		if (!result) {
			owner->report(result.error());
		}
		return result.has_value();
	};
	callbacks.canEdit =
		[weak](native::NativeTextTarget target, native::TextCommand command) -> bool {
		const auto owner = weak.lock();
		return owner && owner->canEdit(portableTarget(target), portableCommand(command));
	};
	callbacks.caret = [weak](native::NativeTextTarget target, std::size_t offset)
		-> std::optional<native::NativeTextRect> {
		const auto owner = weak.lock();
		if (!owner) {
			return std::nullopt;
		}
		const auto snapshot = owner->textInputSnapshot(portableTarget(target));
		if (!snapshot) {
			return std::nullopt;
		}
		const auto byte = text::utf8Offset(displaySnapshot(*snapshot), offset);
		if (!byte) {
			return std::nullopt;
		}
		const auto result = owner->textCaretRect(portableTarget(target), *byte);
		if (!result) {
			return std::nullopt;
		}
		return native::NativeTextRect{
			.x = result->origin.x.value(),
			.y = result->origin.y.value(),
			.width = result->size.width.value(),
			.height = result->size.height.value()
		};
	};
	callbacks.index = [weak](double x, double y) -> std::optional<std::size_t> {
		const auto owner = weak.lock();
		if (!owner) {
			return std::nullopt;
		}
		const auto result = owner->textIndexAt({.x = Points{x}, .y = Points{y}}, false);
		const auto target = owner->textInputTarget();
		if (!result || !target) {
			return std::nullopt;
		}
		const auto snapshot = owner->textInputSnapshot(*target);
		if (!snapshot) {
			return std::nullopt;
		}
		return text::utf16Offset(displaySnapshot(*snapshot), *result).value_or(0);
	};
	callbacks.contextMenu = [weak](double x, double y) -> std::optional<native::NativeTextTarget> {
		const auto owner = weak.lock();
		const auto target =
			owner ? owner->contextMenuTarget({.x = Points{x}, .y = Points{y}}) : std::nullopt;
		return target ? std::optional{nativeTarget(*target)} : std::nullopt;
	};
	callbacks.active = [weak](bool active) -> void {
		if (const auto owner = weak.lock()) {
			owner->setInputActive(active);
		}
	};
	callbacks.key = [weak](native::NativeTextKey key, bool released, bool backwards) -> bool {
		const auto owner = weak.lock();
		if (!owner) {
			return false;
		}
		document::Key portable = document::Key::text;
		switch (key) {
		case native::NativeTextKey::text:
			break;
		case native::NativeTextKey::tab:
			portable = document::Key::tab;
			break;
		case native::NativeTextKey::enter:
			portable = document::Key::enter;
			break;
		case native::NativeTextKey::escape:
			portable = document::Key::escape;
			break;
		}
		return owner->routeKey(portable, released, backwards);
	};
	return callbacks;
}

auto prepareTextServices(const std::shared_ptr<document::detail::Document> &document)
	-> std::shared_ptr<TextAttachment> {
	auto attachment = std::make_shared<TextAttachment>();
	attachment->callbacks = installTextServices(document);
	return attachment;
}
} // namespace stana::application::detail
