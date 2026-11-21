#include "host_view.hpp"
#include <array>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace stana::platform::detail {
namespace {
[[nodiscard]] auto nativeState(STANAView *view) -> std::optional<NativeTextState> {
	const auto state = view->state_;
	return state && !state->isClosed && state->callbacks.textInput.state
		? state->callbacks.textInput.state()
		: std::nullopt;
}

[[nodiscard]] auto utf8String(id value) -> std::optional<std::string> {
	NSString *string = nil;
	if ([value isKindOfClass:[NSAttributedString class]]) {
		string = [static_cast<NSAttributedString *>(value) string];
	} else if ([value isKindOfClass:[NSString class]]) {
		string = static_cast<NSString *>(value);
	}
	NSData *const data = [string dataUsingEncoding:NSUTF8StringEncoding];
	if (data == nil) {
		return std::nullopt;
	}
	return std::string{static_cast<const char *>(data.bytes), data.length};
}

[[nodiscard]] auto checkedRange(NSRange range) -> std::optional<NativeTextRange> {
	if (range.location == NSNotFound ||
		range.length > std::numeric_limits<NSUInteger>::max() - range.location) {
		return std::nullopt;
	}
	return NativeTextRange{.start = range.location, .end = range.location + range.length};
}

[[nodiscard]] auto nsRange(NativeTextRange range) -> NSRange {
	return NSMakeRange(range.start, range.end - range.start);
}

class InputScope {
  public:
	explicit InputScope(STANAView *view, bool independent = false)
		: view_(view),
		  callbacks_(view->state_->callbacks.textInput),
		  previous_(view->textSequence_),
		  previousTarget_(view->textSequenceTarget_),
		  owns_(previous_ == 0 || independent) {
		if (owns_) {
			const auto current = nativeState(view);
			const auto sequence = callbacks_.begin ? callbacks_.begin() : 0;
			view_->textSequence_ = sequence;
			view_->textSequenceTarget_ = current ? std::optional{current->target} : std::nullopt;
		}
	}

	InputScope(const InputScope &) = delete;
	InputScope(InputScope &&) = delete;
	auto operator=(const InputScope &) -> InputScope & = delete;
	auto operator=(InputScope &&) -> InputScope & = delete;

	~InputScope() {
		try {
			const auto sequence = view_->textSequence_;
			view_->textSequence_ = previous_;
			view_->textSequenceTarget_ = previousTarget_;
			if (owns_ && sequence != 0 && callbacks_.end) {
				callbacks_.end(sequence);
			}
		} catch (...) {
			std::terminate();
		}
	}

  private:
	STANAView *__strong view_;
	TextInputCallbacks callbacks_;
	std::uint64_t previous_;
	std::optional<NativeTextTarget> previousTarget_;
	bool owns_;
};

[[nodiscard]] auto commandFor(SEL selector) -> std::optional<TextCommand> {
	using enum TextCommand;
	const std::string_view name{sel_getName(selector)};
	constexpr std::array commands{
		std::pair<std::string_view, TextCommand>{"moveLeft:", left},
		std::pair<std::string_view, TextCommand>{"moveRight:", right},
		std::pair<std::string_view, TextCommand>{"moveWordLeft:", wordBackward},
		std::pair<std::string_view, TextCommand>{"moveWordRight:", wordForward},
		std::pair<std::string_view, TextCommand>{"moveBackward:", left},
		std::pair<std::string_view, TextCommand>{"moveForward:", right},
		std::pair<std::string_view, TextCommand>{"moveWordBackward:", wordBackward},
		std::pair<std::string_view, TextCommand>{"moveWordForward:", wordForward},
		std::pair<std::string_view, TextCommand>{"moveToBeginningOfLine:", lineStart},
		std::pair<std::string_view, TextCommand>{"moveToEndOfLine:", lineEnd},
		std::pair<std::string_view, TextCommand>{"moveToBeginningOfDocument:", lineStart},
		std::pair<std::string_view, TextCommand>{"moveToEndOfDocument:", lineEnd},
		std::pair<std::string_view, TextCommand>{"moveLeftAndModifySelection:", selectLeft},
		std::pair<std::string_view, TextCommand>{"moveRightAndModifySelection:", selectRight},
		std::pair<std::string_view, TextCommand>{"moveBackwardAndModifySelection:", selectLeft},
		std::pair<std::string_view, TextCommand>{"moveForwardAndModifySelection:", selectRight},
		std::pair<std::string_view, TextCommand>{
			"moveWordLeftAndModifySelection:",
			selectWordBackward
		},
		std::pair<std::string_view, TextCommand>{
			"moveWordRightAndModifySelection:",
			selectWordForward
		},
		std::pair<std::string_view, TextCommand>{
			"moveWordBackwardAndModifySelection:",
			selectWordBackward
		},
		std::pair<std::string_view, TextCommand>{
			"moveWordForwardAndModifySelection:",
			selectWordForward
		},
		std::pair<std::string_view, TextCommand>{
			"moveToBeginningOfLineAndModifySelection:",
			selectLineStart
		},
		std::pair<std::string_view, TextCommand>{
			"moveToEndOfLineAndModifySelection:",
			selectLineEnd
		},
		std::pair<std::string_view, TextCommand>{"deleteBackward:", deleteBackward},
		std::pair<std::string_view, TextCommand>{"deleteForward:", deleteForward},
		std::pair<std::string_view, TextCommand>{"deleteWordBackward:", deleteWordBackward},
		std::pair<std::string_view, TextCommand>{"deleteWordForward:", deleteWordForward},
		std::pair<std::string_view, TextCommand>{"selectAll:", selectAll},
		std::pair<std::string_view, TextCommand>{"cut:", cut},
		std::pair<std::string_view, TextCommand>{"copy:", copy},
		std::pair<std::string_view, TextCommand>{"paste:", paste},
		std::pair<std::string_view, TextCommand>{"undo:", undo},
		std::pair<std::string_view, TextCommand>{"redo:", redo},
		std::pair<std::string_view, TextCommand>{"insertNewline:", submit},
		std::pair<std::string_view, TextCommand>{"insertLineBreak:", submit},
		std::pair<std::string_view, TextCommand>{"cancelOperation:", cancel},
	};
	for (const auto &[candidate, command] : commands) {
		if (candidate == name) {
			return command;
		}
	}
	return std::nullopt;
}

[[nodiscard]] auto sequenceMatches(STANAView *view, NativeTextTarget target) -> bool {
	const auto &captured = view->textSequenceTarget_;
	return captured && captured->document == target.document && captured->node == target.node &&
		captured->session == target.session;
}

void applyNativeEdit(STANAView *view, const NativeTextEdit &edit) {
	const auto current = nativeState(view);
	if (!current || !view->state_->callbacks.textInput.edit) {
		return;
	}
	const InputScope scope{view};
	if (!sequenceMatches(view, current->target)) {
		return;
	}
	static_cast<void>(
		view->state_->callbacks.textInput.edit(current->target, view->textSequence_, edit)
	);
}
} // namespace
} // namespace stana::platform::detail

namespace stana::platform::detail {
void attachTextInput(STANAView *view) {
	[view installTextInput];
}

auto textKeyForEvent(NSEvent *event) -> NativeTextKey {
	NSString *const characters = event.charactersIgnoringModifiers;
	if ([characters isEqualToString:@"\t"]) {
		return NativeTextKey::tab;
	}
	if ([characters isEqualToString:@"\r"]) {
		return NativeTextKey::enter;
	}
	if ([characters isEqualToString:@"\x1b"]) {
		return NativeTextKey::escape;
	}
	return NativeTextKey::text;
}
} // namespace stana::platform::detail

@implementation STANAView (TextInput)

- (void)installTextInput {
	if (!state_ || !state_->callbacks.textInput.host) {
		return;
	}
	__weak STANAView *const weak = self;
	state_->callbacks.textInput.host->finish = [weak] -> bool {
		STANAView *const view = weak;
		if (view == nil) {
			return false;
		}
		[view unmarkText];
		[view->textContext_ discardMarkedText];
		return true;
	};
	state_->callbacks.textInput.host->discard = [weak] -> void {
		STANAView *const view = weak;
		if (view != nil) {
			[view->textContext_ discardMarkedText];
		}
	};
	state_->callbacks.textInput.host->invalidateGeometry = [weak] -> void {
		STANAView *const view = weak;
		if (view != nil) {
			[[view inputContext] invalidateCharacterCoordinates];
		}
	};
}

- (NSTextInputContext *)inputContext {
	if (!stana::platform::detail::nativeState(self)) {
		return nil;
	}
	if (textContext_ == nil) {
		textContext_ = [[NSTextInputContext alloc] initWithClient:self];
	}
	return textContext_;
}

- (BOOL)handleTextKey:(NSEvent *)event {
	const auto current = stana::platform::detail::nativeState(self);
	if (!current) {
		return NO;
	}
	const auto callbacks = state_->callbacks.textInput;
	const auto key = stana::platform::detail::textKeyForEvent(event);
	if (callbacks.key &&
		!callbacks.key(key, false, (event.modifierFlags & NSEventModifierFlagShift) != 0)) {
		return YES;
	}
	const auto next = stana::platform::detail::nativeState(self);
	if (!next || next->target.document != current->target.document ||
		next->target.node != current->target.node ||
		next->target.session != current->target.session) {
		return YES;
	}
	const stana::platform::detail::InputScope scope{self, true};
	const auto flags = event.modifierFlags;
	if ((flags & NSEventModifierFlagCommand) != 0) {
		NSString *const shortcutCharacters = event.charactersIgnoringModifiers.lowercaseString;
		using Command = stana::platform::detail::TextCommand;
		std::optional<Command> command;
		if ([shortcutCharacters isEqualToString:@"a"]) {
			command = Command::selectAll;
		}
		if ([shortcutCharacters isEqualToString:@"x"]) {
			command = Command::cut;
		}
		if ([shortcutCharacters isEqualToString:@"c"]) {
			command = Command::copy;
		}
		if ([shortcutCharacters isEqualToString:@"v"]) {
			command = Command::paste;
		}
		if ([shortcutCharacters isEqualToString:@"z"]) {
			command = (flags & NSEventModifierFlagShift) != 0 ? Command::redo : Command::undo;
		}
		if (command) {
			[self performTextCommand:*command];
			return YES;
		}
	}
	if (event.isARepeat &&
		([event.charactersIgnoringModifiers isEqualToString:@"\r"] ||
			[event.charactersIgnoringModifiers isEqualToString:@"\x1b"])) {
		return YES;
	}
	if (![[self inputContext] handleEvent:event]) {
		[self interpretKeyEvents:@[ event ]];
	}
	return YES;
}

- (void)performTextCommand:(stana::platform::detail::TextCommand)command {
	const auto current = stana::platform::detail::nativeState(self);
	if (!current || !state_->callbacks.textInput.command) {
		return;
	}
	const stana::platform::detail::InputScope scope{self};
	if (!stana::platform::detail::sequenceMatches(self, current->target)) {
		return;
	}
	static_cast<void>(state_->callbacks.textInput.command(current->target, textSequence_, command));
}

- (void)doCommandBySelector:(SEL)selector {
	const auto command = stana::platform::detail::commandFor(selector);
	if (command) {
		[self performTextCommand:*command];
		return;
	}
	if (selector == @selector(insertTab:) || selector == @selector(insertBacktab:)) {
		[self performTextCommand:selector == @selector(insertBacktab:)
				? stana::platform::detail::TextCommand::previousFocus
				: stana::platform::detail::TextCommand::nextFocus];
		return;
	}
	[super doCommandBySelector:selector];
}

- (void)insertText:(id)value replacementRange:(NSRange)replacement {
	const auto text = stana::platform::detail::utf8String(value);
	if (!text) {
		return;
	}
	const auto range = stana::platform::detail::checkedRange(replacement);
	if (replacement.location != NSNotFound && !range) {
		return;
	}
	stana::platform::detail::applyNativeEdit(
		self,
		{.kind = stana::platform::detail::NativeTextEdit::Kind::insert,
			.text = *text,
			.replacement = range}
	);
}

- (void)setMarkedText:(id)value
		selectedRange:(NSRange)selected
	 replacementRange:(NSRange)replacement {
	const auto text = stana::platform::detail::utf8String(value);
	const auto selection = stana::platform::detail::checkedRange(selected);
	const auto range = stana::platform::detail::checkedRange(replacement);
	if (!text || !selection || (replacement.location != NSNotFound && !range)) {
		return;
	}
	stana::platform::detail::applyNativeEdit(
		self,
		{.kind = stana::platform::detail::NativeTextEdit::Kind::mark,
			.text = *text,
			.replacement = range,
			.selection = *selection}
	);
}

- (void)unmarkText {
	stana::platform::detail::applyNativeEdit(
		self,
		{.kind = stana::platform::detail::NativeTextEdit::Kind::commit}
	);
}

- (BOOL)hasMarkedText {
	const auto current = stana::platform::detail::nativeState(self);
	return current && current->marked.has_value();
}

- (NSRange)markedRange {
	const auto current = stana::platform::detail::nativeState(self);
	return current && current->marked ? stana::platform::detail::nsRange(*current->marked)
									  : NSMakeRange(NSNotFound, 0);
}

- (NSRange)selectedRange {
	const auto current = stana::platform::detail::nativeState(self);
	return current ? stana::platform::detail::nsRange(current->selected)
				   : NSMakeRange(NSNotFound, 0);
}

- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText {
	return @[];
}

- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)proposed
												actualRange:(NSRangePointer)actual {
	const auto current = stana::platform::detail::nativeState(self);
	const auto checked = stana::platform::detail::checkedRange(proposed);
	if (!current || !checked) {
		if (actual != nullptr) {
			*actual = NSMakeRange(NSNotFound, 0);
		}
		return nil;
	}
	NSString *const string = [[NSString alloc] initWithBytes:current->displayed.data()
													  length:current->displayed.size()
													encoding:NSUTF8StringEncoding];
	if (checked->end > string.length ||
		(checked->start < string.length &&
			(CFStringIsSurrogateLowCharacter([string characterAtIndex:checked->start]) != 0u)) ||
		(checked->end < string.length &&
			(CFStringIsSurrogateLowCharacter([string characterAtIndex:checked->end]) != 0u))) {
		if (actual != nullptr) {
			*actual = NSMakeRange(NSNotFound, 0);
		}
		return nil;
	}
	if (actual != nullptr) {
		*actual = proposed;
	}
	return [[NSAttributedString alloc] initWithString:[string substringWithRange:proposed]];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual {
	const auto current = stana::platform::detail::nativeState(self);
	const auto checked = stana::platform::detail::checkedRange(range);
	if (actual != nullptr) {
		*actual = NSMakeRange(NSNotFound, 0);
	}
	if (!current || !checked || !state_->callbacks.textInput.caret || self.window == nil) {
		return NSZeroRect;
	}
	const auto rect = state_->callbacks.textInput.caret(current->target, checked->start);
	if (!rect) {
		return NSZeroRect;
	}
	if (actual != nullptr) {
		*actual = NSMakeRange(checked->start, 0);
	}
	const NSRect local = NSMakeRect(rect->x, rect->y, rect->width, rect->height);
	return [self.window convertRectToScreen:[self convertRect:local toView:nil]];
}

- (NSUInteger)characterIndexForPoint:(NSPoint)screen {
	if (!state_ || !state_->callbacks.textInput.index || self.window == nil) {
		return NSNotFound;
	}
	const auto point = [self convertPoint:[self.window convertPointFromScreen:screen] fromView:nil];
	return state_->callbacks.textInput.index(point.x, point.y).value_or(NSNotFound);
}

- (NSMenu *)menuForEvent:(NSEvent *)event {
	if (!state_ || !state_->allowsPointer() || !state_->callbacks.textInput.contextMenu) {
		return nil;
	}
	const auto point = [self convertPoint:event.locationInWindow fromView:nil];
	menuTextTarget_ = state_->callbacks.textInput.contextMenu(point.x, point.y);
	if (!menuTextTarget_) {
		return nil;
	}
	[self.window makeFirstResponder:self];
	NSMenu *const menu = [[NSMenu alloc] initWithTitle:@""];
	using Command = stana::platform::detail::TextCommand;
	const auto append = [&](NSString *title, Command command) -> void {
		NSMenuItem *const item =
			[[NSMenuItem alloc] initWithTitle:title
									   action:@selector(performContextTextCommand:)
								keyEquivalent:@""];
		item.target = self;
		item.tag = static_cast<NSInteger>(command);
		[menu addItem:item];
	};
	append(@"Undo", Command::undo);
	append(@"Redo", Command::redo);
	[menu addItem:[NSMenuItem separatorItem]];
	append(@"Cut", Command::cut);
	append(@"Copy", Command::copy);
	append(@"Paste", Command::paste);
	[menu addItem:[NSMenuItem separatorItem]];
	append(@"Select All", Command::selectAll);
	return menu;
}

- (void)performContextTextCommand:(NSMenuItem *)item {
	if (!state_ || !menuTextTarget_ || !state_->callbacks.textInput.command) {
		return;
	}
	const stana::platform::detail::InputScope scope{self, true};
	static_cast<void>(state_->callbacks.textInput.command(
		*menuTextTarget_,
		textSequence_,
		static_cast<stana::platform::detail::TextCommand>(item.tag)
	));
}

- (BOOL)validateMenuItem:(NSMenuItem *)item {
	const auto current = stana::platform::detail::nativeState(self);
	const auto target = item.action == @selector(performContextTextCommand:)
		? menuTextTarget_
		: (current ? std::optional{current->target} : std::nullopt);
	const auto command = item.action == @selector(performContextTextCommand:)
		? std::optional{static_cast<stana::platform::detail::TextCommand>(item.tag)}
		: stana::platform::detail::commandFor(item.action);
	return target && command && state_ && state_->callbacks.textInput.canEdit &&
		state_->callbacks.textInput.canEdit(*target, *command);
}

- (void)cut:(id)sender {
	static_cast<void>(sender);
	[self performTextCommand:stana::platform::detail::TextCommand::cut];
}

- (void)copy:(id)sender {
	static_cast<void>(sender);
	[self performTextCommand:stana::platform::detail::TextCommand::copy];
}

- (void)paste:(id)sender {
	static_cast<void>(sender);
	[self performTextCommand:stana::platform::detail::TextCommand::paste];
}

- (void)selectAll:(id)sender {
	static_cast<void>(sender);
	[self performTextCommand:stana::platform::detail::TextCommand::selectAll];
}

- (void)undo:(id)sender {
	static_cast<void>(sender);
	[self performTextCommand:stana::platform::detail::TextCommand::undo];
}

- (void)redo:(id)sender {
	static_cast<void>(sender);
	[self performTextCommand:stana::platform::detail::TextCommand::redo];
}

@end
