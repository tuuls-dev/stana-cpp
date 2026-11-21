#include <chrono>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
import stana;

namespace {
using stana::document::EditingCommand;
using stana::text::StoreEdit;
constexpr stana::Points hostWidth{300};
constexpr stana::Points hostHeight{80};
constexpr stana::Points compositionFontSize{28};
constexpr std::chrono::milliseconds caretHalfPeriod{500};

void require(bool value, const std::string &message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] auto input(const stana::testing::TestHost &host) -> stana::document::NodeSnapshot {
	for (const auto &node : host.nodes()) {
		if (node.editor) {
			return node;
		}
	}
	throw std::runtime_error("Missing editor snapshot");
}

[[nodiscard]] auto editor(const stana::testing::TestHost &host) -> stana::document::EditorSnapshot {
	const auto snapshot = input(host);
	if (!snapshot.editor) {
		throw std::runtime_error("Missing editor state");
	}
	return *snapshot.editor;
}

[[nodiscard]] auto inputTarget(const stana::testing::TestHost &host)
	-> stana::document::TextInputTarget {
	const auto target = host.textInputTarget();
	if (!target) {
		throw std::runtime_error("Missing active editor target");
	}
	return *target;
}

void focus(stana::testing::TestHost &host) {
	require(host.handle(input(host).id).requestFocus().has_value(), "focus editor");
}

void compositionSequence() {
	const stana::reactive::Signal value{std::string{}};
	int submissions = 0;
	stana::testing::TestHost host;
	host.mount(
		stana::ui::TextInput{
			.value = value.binding(),
			.placeholder = "Name",
			.onSubmit = [&](const auto &event) -> void {
				++submissions;
				require(
					event.value == "日本" && event.target == input(host).id,
					"owning submission"
				);
			}
		}
	);
	focus(host);
	const auto identity = input(host).id;
	require(
		host.setMarkedText("日本").has_value() && value.get().empty(),
		"preedit is not model text"
	);
	const auto before = editor(host).store;
	value.set("");
	require(editor(host).store.stamp == before.stamp, "equal model echo preserves store");
	const auto sequence = host.beginInputSequence();
	require(
		host.dispatchText(inputTarget(host), sequence, {.kind = StoreEdit::Kind::commit})
			.has_value(),
		"commit preedit"
	);
	require(value.get() == "日本", "committed binding");
	require(
		host.dispatchCommand(inputTarget(host), sequence, EditingCommand::submit).has_value(),
		"same sequence submit"
	);
	require(submissions == 0, "composition consumes submit");
	host.endInputSequence(sequence);
	require(
		host.editingCommand(EditingCommand::submit).has_value() && submissions == 1,
		"independent submit once"
	);
	require(input(host).id == identity, "editing never remounts");
	require(host.setMarkedText("候補").has_value(), "new preedit");
	require(
		host.cancelMarkedText().has_value() && value.get() == "日本",
		"cancel preserves committed model"
	);
	require(
		host.editingCommand(EditingCommand::undo).has_value() && value.get().empty(),
		"IME commit is one history group"
	);
}

void selectionHistoryAndClipboard() {
	const stana::reactive::Signal value{std::string{}};
	const auto clipboard = std::make_shared<stana::testing::MemoryClipboard>();
	stana::testing::TestHost host{
		{.width = hostWidth, .height = hostHeight},
		{},
		{.clipboard = clipboard}
	};
	host.mount(stana::ui::TextInput{.value = value.binding()});
	focus(host);
	require(
		host.insertText("e\xCC\x81").has_value() && host.insertText("😀").has_value(),
		"Unicode typing"
	);
	require(
		host.editingCommand(EditingCommand::undo).has_value() && value.get().empty(),
		"contiguous typing groups"
	);
	require(host.editingCommand(EditingCommand::redo).has_value(), "redo typing");
	require(
		host.editingCommand(EditingCommand::deleteBackward).has_value() &&
			value.get() == "e\xCC\x81",
		"delete complete emoji"
	);
	require(
		host.editingCommand(EditingCommand::deleteBackward).has_value() && value.get().empty(),
		"delete combining cluster"
	);
	require(
		host.editingCommand(EditingCommand::undo).has_value() && value.get() == "e\xCC\x81😀",
		"repeated deletion groups"
	);
	require(host.editingCommand(EditingCommand::selectAll).has_value(), "select all");
	require(
		host.editingCommand(EditingCommand::copy).has_value() && clipboard->text == value.get(),
		"copy committed selection"
	);
	clipboard->failWrite = true;
	require(
		!host.editingCommand(EditingCommand::cut) && value.get() == "e\xCC\x81😀",
		"failed cut preserves content"
	);
	clipboard->failWrite = false;
	clipboard->text = "a\r\nb\rc\nd\xC2\x85"
					  "e\xE2\x80\xA8"
					  "f\xE2\x80\xA9g\t";
	require(
		host.editingCommand(EditingCommand::paste).has_value() && value.get() == "a b c d e f g\t",
		"paste normalizes all breaks"
	);
	require(
		host.editingCommand(EditingCommand::undo).has_value() && value.get() == "e\xCC\x81😀",
		"paste one undo group"
	);
	clipboard->text = std::string{"\xFF"};
	require(
		!host.editingCommand(EditingCommand::paste) && value.get() == "e\xCC\x81😀",
		"invalid clipboard rejected"
	);
}

void staleTargetsAndExternalValues() {
	const stana::reactive::Signal value{std::string{"valid"}};
	stana::testing::TestHost host;
	host.mount(stana::ui::TextInput{.value = value.binding()});
	focus(host);
	const auto target = inputTarget(host);
	const auto sequence = host.beginInputSequence();
	require(
		host.editingCommand(EditingCommand::selectAll).has_value(),
		"selection changes store stamp"
	);
	const auto stale = host.dispatchText(target, sequence, {.text = "wrong"});
	require(
		!stale && stale.error() == stana::Error::invalidTextRange && value.get() == "valid",
		"reject stale selection-only stamp"
	);
	host.endInputSequence(sequence);
	require(host.setMarkedText("候補").has_value(), "compose before external value");
	value.set("external");
	require(
		input(host).text == "external" && !editor(host).store.composition &&
			!editor(host).store.canUndo,
		"different external value resets editor"
	);
	value.set("invalid\xE2\x80\xA8value");
	require(
		input(host).text == "external" && host.errors().back() == stana::Error::invalidValue,
		"invalid external value preserves editor"
	);
	const auto active = inputTarget(host);
	host.setInputActive(false);
	require(
		!host.textInputTarget() && input(host).isFocused && !editor(host).isActive,
		"inactive retains logical focus"
	);
	host.setInputActive(true);
	const auto next = host.beginInputSequence();
	const auto oldSession = host.dispatchText(active, next, {.text = "stale"});
	require(
		!oldSession && oldSession.error() == stana::Error::cancelled,
		"fresh activation session"
	);
	host.endInputSequence(next);
}

void reentrancyAndMenus() {
	const stana::reactive::Signal value{std::string{"abc"}};
	stana::testing::TestHost host;
	host.mount(
		stana::ui::TextInput{.value = value.binding(), .onSubmit = [&](const auto &) -> void {
								 host.close();
							 }}
	);
	focus(host);
	const auto node = input(host);
	const stana::Point point{
		.x = node.bounds.origin.x + stana::Points{10},
		.y = node.bounds.origin.y + stana::Points{10}
	};
	require(
		host.editingCommand(EditingCommand::selectAll).has_value(),
		"select before context menu"
	);
	const auto menuTarget = host.contextMenuTarget(point);
	if (!menuTarget) {
		throw std::runtime_error("Missing context menu target");
	}
	require(
		menuTarget.has_value() && host.canEdit(EditingCommand::copy),
		"context menu targets eligible field"
	);
	const auto sequence = host.beginInputSequence();
	require(host.insertText("new").has_value(), "change state while menu open");
	require(
		!host.dispatchCommand(menuTarget.value(), sequence, EditingCommand::cut) &&
			value.get() == "new",
		"stale menu cannot cut new content"
	);
	host.endInputSequence(sequence);
	require(
		host.editingCommand(EditingCommand::submit).has_value() && !host.handle(node.id).isAlive(),
		"submit can close document"
	);
}

void bindingSetterReentrancy() {
	const stana::reactive::Signal value{std::string{"old"}};
	stana::testing::TestPlatform platform;
	platform.writeClipboard = [&](const std::string &) -> std::expected<void, stana::Error> {
		value.set("replacement");
		return {};
	};
	stana::testing::TestHost host{{.width = hostWidth, .height = hostHeight}, {}, platform};
	host.mount(stana::ui::TextInput{.value = value.binding()});
	focus(host);
	require(host.editingCommand(EditingCommand::selectAll).has_value(), "select for reentrant cut");
	require(
		!host.editingCommand(EditingCommand::cut) && value.get() == "replacement",
		"cut revalidates after clipboard callback"
	);
}

void compositionLayoutChangeCancelsPointerDefault() {
	constexpr stana::Points contentStart{8};
	constexpr stana::Points pointerY{10};
	const stana::reactive::Signal value{std::string{}};
	const stana::reactive::Signal font{stana::Font::system(stana::defaults::fontSize)};
	const auto document =
		stana::document::detail::Document::create({.width = hostWidth, .height = hostHeight});
	stana::document::detail::TextServices services;
	services.finishComposition = [font] -> std::expected<void, stana::Error> {
		font.set(stana::Font::system(compositionFontSize));
		return {};
	};
	document->setTextServices(std::move(services));
	stana::ui::MountContext context{.document = document, .parent = document->root()};
	const auto handle = stana::ui::mount(
		stana::ui::TextInput{.value = value.binding(), .textStyle = {.font = font}},
		context
	);
	require(handle.requestFocus().has_value(), "focus composition layout probe");
	const auto target = document->textInputTarget();
	if (!target) {
		throw std::runtime_error("Missing composition layout target");
	}
	const stana::document::InputSequenceScope sequence{document};
	require(
		document
			->editText(
				*target,
				sequence.sequence(),
				{.kind = StoreEdit::Kind::compose,
					.text = "abc",
					.selection = {.start = 3, .end = 3}}
			)
			.has_value(),
		"prepare pointer preedit"
	);
	document->pointerDown(stana::Point{.x = contentStart, .y = pointerY});
	const auto current = document->textInputTarget();
	if (!current) {
		throw std::runtime_error("Composition layout target retired");
	}
	const auto snapshot = document->textInputSnapshot(*current);
	require(
		snapshot && snapshot->text == "abc" && value.get() == "abc" && !snapshot->composition &&
			snapshot->selection.head.utf8Offset == snapshot->text.size(),
		"changed shaping cancels the old hit while retaining the composition commit"
	);
	document->close();
}

void eligibilityAndBlink() {
	const stana::reactive::Signal value{std::string{}};
	const stana::reactive::Signal inert{false};
	stana::testing::TestHost host;
	host.mount(stana::ui::TextInput{.value = value.binding(), .interaction = {.inert = inert}});
	focus(host);
	require(editor(host).isCaretVisible, "initial solid caret");
	host.advanceTime(caretHalfPeriod);
	require(!editor(host).isCaretVisible, "deterministic caret blink");
	require(host.insertText("a").has_value() && editor(host).isCaretVisible, "typing resets blink");
	inert.set(true);
	require(
		!input(host).isFocused && !host.textInputTarget(),
		"eligibility reconciles focus before publication"
	);
}
} // namespace

auto main() -> int {
	try {
		compositionSequence();
		selectionHistoryAndClipboard();
		staleTargetsAndExternalValues();
		reentrancyAndMenus();
		bindingSetterReentrancy();
		eligibilityAndBlink();
		compositionLayoutChangeCancelsPointerDefault();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
