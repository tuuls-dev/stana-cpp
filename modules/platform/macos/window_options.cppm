module;

#include <cstdint>
#include <optional>
#include <string>

export module stana:platform_macos;
import :primitives;
import :reactive;

/// @file
/// @brief Typed macOS window extension descriptors.
///
/// Lives under `modules/platform/macos/` with AppKit adapters in
/// `src/platform/macos/`. Public namespace remains `stana::application::platform::macos`
/// so portable consumers reach extensions through `import stana;` without native types.

export namespace stana::application::platform::macos {

/// @brief How content integrates with the native title bar on macOS.
enum class TitleBarContentIntegration : std::uint8_t {
	separated,			///< Standard title bar above the content view.
	fullSizeContentView ///< Content extends under the title bar region.
};

/// @brief Traffic-light control visibility when using full-size content views.
enum class TitleBarControlVisibility : std::uint8_t {
	visible, ///< Standard traffic lights remain visible.
	hidden	 ///< Traffic lights are hidden; closing must be provided elsewhere.
};

/// @brief macOS title-bar and content-integration preferences.
///
/// No AppKit types escape. Unsupported combinations report typed outcomes at
/// apply time rather than silently changing window role.
struct TitleBarOptions {
	::stana::reactive::Property<TitleBarContentIntegration> contentIntegration{
		TitleBarContentIntegration::separated
	}; ///< Content versus title-bar geometry relationship.
	::stana::reactive::Property<bool> transparentTitleBar{
		false
	}; ///< Transparent title-bar material.
	::stana::reactive::Property<bool> titleVisible{true}; ///< Whether the title text is shown.
	::stana::reactive::Property<TitleBarControlVisibility> controls{
		TitleBarControlVisibility::visible
	}; ///< Traffic-light visibility preference.
};

/// @brief Native tabbing participation for `NSWindow` tabbing APIs.
enum class TabbingMode : std::uint8_t {
	automatic, ///< System automatic tabbing preference.
	preferred, ///< Prefer joining a tabbing group.
	disallowed ///< Disallow tabbing for this window.
};

/// @brief macOS window tabbing preferences.
struct TabbingOptions {
	::stana::reactive::Property<TabbingMode> mode{
		TabbingMode::automatic
	}; ///< Tabbing participation.
	::stana::reactive::Property<std::optional<std::string>>
		tabbingIdentifier{}; ///< Group identity.
};

/// @brief Spaces / Mission Control collection behaviors that are macOS-specific.
enum class SpacesBehavior : std::uint8_t {
	managed,		  ///< Default Spaces participation.
	canJoinAllSpaces, ///< Visible across Spaces when supported.
	moveToActiveSpace ///< Moves with the active Space when ordered front.
};

/// @brief Fullscreen participation relative to Spaces.
enum class FullscreenBehavior : std::uint8_t {
	primary,   ///< Ordinary fullscreen Space.
	auxiliary, ///< Auxiliary fullscreen behavior where supported.
	disallowed ///< Reject fullscreen entry for this window.
};

/// @brief macOS Spaces and fullscreen participation preferences.
struct SpacesOptions {
	::stana::reactive::Property<SpacesBehavior> behavior{
		SpacesBehavior::managed
	}; ///< Spaces policy.
	::stana::reactive::Property<FullscreenBehavior> fullscreen{
		FullscreenBehavior::primary
	}; ///< Fullscreen Space participation.
};

/// @brief Panel-specific activation and modality interaction rules.
struct PanelOptions {
	::stana::reactive::Property<bool> becomesKeyOnlyIfNeeded{
		false
	}; ///< Nonactivating until needed.
	::stana::reactive::Property<bool> worksWhenModal{
		false
	}; ///< Receives events during app modality.
	::stana::reactive::Property<bool> floatingPanel{false}; ///< Panel floating collection behavior.
};

/// @brief Native AppKit window levels expressed without integer masks.
enum class NativeWindowLevel : std::uint8_t {
	normal,		 ///< `NSNormalWindowLevel` intent.
	floating,	 ///< `NSFloatingWindowLevel` intent.
	submenu,	 ///< `NSSubmenuWindowLevel` intent.
	tornOffMenu, ///< `NSTornOffMenuWindowLevel` intent.
	mainMenu,	 ///< `NSMainMenuWindowLevel` intent.
	statusBar,	 ///< `NSStatusWindowLevel` intent.
	modalPanel,	 ///< `NSModalPanelWindowLevel` intent.
	popUpMenu,	 ///< `NSPopUpMenuWindowLevel` intent.
	screenSaver	 ///< `NSScreenSaverWindowLevel` intent.
};

/// @brief macOS-only native stacking level preference.
struct NativeLevelOptions {
	::stana::reactive::Property<std::optional<NativeWindowLevel>>
		level{}; ///< Empty follows portable level.
};

/// @brief Typed macOS window extension groups; default requests no overrides beyond defaults.
///
/// Portable consumers need not instantiate this type. Construction into
/// `PlatformWindowOptions` fails compilation on non-Apple targets.
struct WindowOptions {
	TitleBarOptions titleBar{};		  ///< Title-bar / content integration.
	TabbingOptions tabbing{};		  ///< Native tabbing.
	SpacesOptions spaces{};			  ///< Spaces and fullscreen participation.
	PanelOptions panel{};			  ///< Panel activation behaviors.
	NativeLevelOptions nativeLevel{}; ///< Native window levels.
};

} // namespace stana::application::platform::macos
