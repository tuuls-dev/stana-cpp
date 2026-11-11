#ifndef STANA_MACOS_WINDOW_CONFIGURATION_HPP
#define STANA_MACOS_WINDOW_CONFIGURATION_HPP

#include "../native_contracts.hpp"
#import <AppKit/AppKit.h>
#include <memory>

namespace stana::platform::detail {

struct WindowState;

/// @brief Tracks last-applied requested values so unchanged updates skip native writes.
struct AppliedWindowConfiguration {
	WindowConfiguration requested{};
	std::string defaultTabbingIdentifier{};
	bool hasApplied = false;
	std::optional<NativeWindowState> transitionTarget{};
	std::uint64_t deferredCloseRequest = 0;
	id toolbarDelegate = nil;
	std::vector<NativeActionCompletion> closeRequests{};
	std::vector<std::pair<NativeWindowState, NativeActionCompletion>> stateRequests{};
};

[[nodiscard]] auto validateNativeConfiguration(const WindowConfiguration &) -> bool;
void applyWindowFacilities(NSWindow *, AppliedWindowConfiguration &, const WindowConfiguration &);
void dispatchWindowGesture(
	NSWindow *,
	const std::shared_ptr<WindowState> &,
	const NativeWindowAction &,
	const NativeActionCompletion &
);
void captureWindowContent(
	NSWindow *,
	const std::shared_ptr<WindowState> &,
	bool printing,
	NativeActionCompletion
);
void publishWindowObservation(const std::shared_ptr<WindowState> &);
void registerConfiguredWindow(const std::shared_ptr<WindowState> &);
[[nodiscard]] auto attachWindowRole(const std::shared_ptr<WindowState> &) -> bool;
void unregisterConfiguredWindows();
void closeOwnedWindows(std::uint64_t owner);
[[nodiscard]] auto applyRoleVisibility(NSWindow *, const WindowConfiguration &, bool visible)
	-> bool;
void encodeWindowRestoration(const std::shared_ptr<WindowState> &, NSCoder *);
void decodeWindowRestoration(const std::shared_ptr<WindowState> &, NSCoder *);
[[nodiscard]] auto negotiateWindowDrag(
	const std::shared_ptr<WindowState> &,
	id<NSDraggingInfo>,
	bool entered,
	bool dropping
) -> NSDragOperation;
void publishWindowInteraction(const std::shared_ptr<WindowState> &, NSEvent *, std::uint8_t phase);

/// @brief Builds the AppKit style mask for the requested role and chrome flags.
[[nodiscard]] auto nativeStyleMask(const WindowConfiguration &configuration) -> NSWindowStyleMask;

/// @brief Creates an NSWindow or NSPanel matching role/style without ordering it front.
[[nodiscard]] auto createConfiguredWindow(const WindowConfiguration &configuration) -> NSWindow *;

/// @brief Applies configuration deltas; only writes when requested values change.
void applyWindowConfiguration(
	NSWindow *window,
	AppliedWindowConfiguration &applied,
	const WindowConfiguration &requested
);

/// @brief Installs host-control callbacks for runtime updates and discrete actions.
void installWindowHostControl(
	NSWindow *window,
	const std::shared_ptr<WindowState> &state,
	const std::shared_ptr<WindowHostControl> &control,
	const std::shared_ptr<AppliedWindowConfiguration> &applied
);

/// @brief Observes current geometry/state in Stana top-left y-down coordinates.
[[nodiscard]] auto observeNativeWindow(NSWindow *window) -> NativeWindowObservation;

/// @brief Enumerates NSScreens into portable logical-point screen values.
[[nodiscard]] auto enumerateMacScreens() -> std::vector<NativeScreen>;

} // namespace stana::platform::detail

#endif
