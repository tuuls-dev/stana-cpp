#ifndef STANA_MACOS_HOST_VIEW_HPP
#define STANA_MACOS_HOST_VIEW_HPP
#include "presentation.hpp"
/// @brief AppKit view that translates native events into the retained document callbacks.
/// The native view hierarchy owns this object; state_ owns no strong view reference.
@interface STANAView : NSView {
  @public
	std::shared_ptr<stana::platform::detail::WindowState>
		state_; ///< Engine callbacks and renderer resources.
	CADisplayLink *frameLink_;
	NSScreen *frameScreen_;
	NSMutableArray *visibilityObservers_;
	NSTrackingArea *trackingArea_;
	bool reportedHostVisible_;
	bool visibilityArmed_;
	bool frameLinkRetired_;
	NSTextInputContext *textContext_;
	std::uint64_t textSequence_;
	std::optional<stana::platform::detail::NativeTextTarget> menuTextTarget_;
}
- (void)dispatchPointerMove:(NSEvent *)event;
- (void)dispatchKey:(NSEvent *)event released:(BOOL)released;
- (instancetype)initWithFrame:(NSRect)frameRect;
- (void)installFrameLink;
- (void)stopFrameLink;
- (void)setFrameDemand:(BOOL)demand;
- (void)publishCurrentVisibility;
- (void)retargetFrameLink;
- (void)tickFrameLink:(CADisplayLink *)link;
@end

namespace stana::platform::detail {
/// @brief Forces the static archive to retain the text-input category, then attaches services.
void attachTextInput(STANAView *view);
} // namespace stana::platform::detail

@interface STANAView (TextInput) <NSTextInputClient, NSMenuItemValidation>
- (void)installTextInput;
- (BOOL)handleTextKey:(NSEvent *)event;
- (void)performTextCommand:(stana::platform::detail::TextCommand)command;
- (void)performContextTextCommand:(NSMenuItem *)item;
@end
#endif
