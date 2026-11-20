#include <chrono>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
import stana;

namespace {
using stana::document::EditingCommand;
using stana::text::StoreEdit;
constexpr stana::Points hostWidth{300};
constexpr stana::Points hostHeight{80};
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
	const auto before = input(host).editor.value().store;
	value.set("");
	require(
		input(host).editor.value().store.stamp == before.stamp,
		"equal model echo preserves store"
	);
	const auto sequence = host.beginInputSequence();
	require(
		host.dispatchText(
				host.textInputTarget().value(),
				sequence,
				{.kind = StoreEdit::Kind::commit}
		)
			.has_value(),
		"commit preedit"
	);
	require(value.get() == "日本", "committed binding");
	require(
		host.dispatchCommand(host.textInputTarget().value(), sequence, EditingCommand::submit)
			.has_value(),
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
	const auto target = host.textInputTarget().value();
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
		input(host).text == "external" && !input(host).editor.value().store.composition &&
			!input(host).editor.value().store.canUndo,
		"different external value resets editor"
	);
	value.set("invalid\xE2\x80\xA8value");
	require(
		input(host).text == "external" && host.errors().back() == stana::Error::invalidValue,
		"invalid external value preserves editor"
	);
	const auto active = host.textInputTarget().value();
	host.setInputActive(false);
	require(
		!host.textInputTarget() && input(host).isFocused && !input(host).editor.value().isActive,
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

void eligibilityAndBlink() {
	const stana::reactive::Signal value{std::string{}};
	const stana::reactive::Signal inert{false};
	stana::testing::TestHost host;
	host.mount(stana::ui::TextInput{.value = value.binding(), .interaction = {.inert = inert}});
	focus(host);
	require(input(host).editor.value().isCaretVisible, "initial solid caret");
	host.advanceTime(caretHalfPeriod);
	require(!input(host).editor.value().isCaretVisible, "deterministic caret blink");
	require(
		host.insertText("a").has_value() && input(host).editor.value().isCaretVisible,
		"typing resets blink"
	);
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
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
