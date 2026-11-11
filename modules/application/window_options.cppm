module;

#include <concepts>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

export module stana:application_window_options;
export import :application_window_types;
export import :application_window_requests;
export import :platform_macos;
export import :primitives;
import :reactive;
import :rendering;

export namespace stana::application {
namespace detail {

inline constexpr Points defaultContentWidth{800};
inline constexpr Points defaultContentHeight{600};

#if defined(__APPLE__)
inline constexpr bool buildingForApple = true;
#else
inline constexpr bool buildingForApple = false;
#endif

/// @brief True for registered platform option group types.
template <typename T> inline constexpr bool registeredPlatformWindowOptions = false;

template <>
inline constexpr bool registeredPlatformWindowOptions<platform::macos::WindowOptions> = true;

/// @brief True when a registered extension may be constructed on this build target.
template <typename T> inline constexpr bool compatiblePlatformWindowOptions = false;

template <>
inline constexpr bool compatiblePlatformWindowOptions<platform::macos::WindowOptions> =
	buildingForApple;

template <typename T>
concept RegisteredPlatformWindowOptions = registeredPlatformWindowOptions<std::remove_cvref_t<T>>;

} // namespace detail

/// @brief Owning wrapper for typed platform option groups with target checking.
///
/// The default value requests no platform overrides. Constructing from an extension
/// registered for another OS fails compilation. Presence of a group is not proof the
/// runtime backend or facility is available.
class PlatformWindowOptions {
  public:
	PlatformWindowOptions() = default;

	/// @brief Stores a registered platform group when the build target accepts it.
	///
	/// Incompatible targets are removed from the overload set (`requires`), so misuse
	/// fails at compile time without a runtime fallback path.
	template <detail::RegisteredPlatformWindowOptions Options>
		requires(detail::compatiblePlatformWindowOptions<std::remove_cvref_t<Options>>)
	PlatformWindowOptions(Options options) : storage_(std::move(options)) {}

	/// @brief Deleted overload that keeps the diagnostic anchored on `PlatformWindowOptions`.
	template <detail::RegisteredPlatformWindowOptions Options>
		requires(!detail::compatiblePlatformWindowOptions<std::remove_cvref_t<Options>>)
	PlatformWindowOptions(Options) = delete;

	[[nodiscard]] auto empty() const noexcept -> bool {
		return std::holds_alternative<std::monostate>(storage_);
	}

	[[nodiscard]] auto macos() const noexcept -> const platform::macos::WindowOptions * {
		if (const auto *value = std::get_if<platform::macos::WindowOptions>(&storage_)) {
			return value;
		}
		return nullptr;
	}

	[[nodiscard]] auto macos() noexcept -> platform::macos::WindowOptions * {
		if (auto *value = std::get_if<platform::macos::WindowOptions>(&storage_)) {
			return value;
		}
		return nullptr;
	}

  private:
	std::variant<std::monostate, platform::macos::WindowOptions> storage_{};
};

/// @brief Complete flat window configuration and discrete request stream.
///
/// Mutable settings use `stana::reactive::Property<T>` and accept constants,
/// `Signal<T>`, and `Computed<T>`. Discrete actions use `requests` so reactive
/// coalescing cannot discard commands.
struct WindowOptions {
	WindowRole role = StandardWindow{}; ///< Structured role; ownership lives on alternatives.
	::stana::rendering::RenderingColorMode renderingColorMode =
		::stana::rendering::RenderingColorMode::automatic; ///< Renderer color policy.
	::stana::reactive::Property<Size> contentSize{
		Size{.width = detail::defaultContentWidth, .height = detail::defaultContentHeight}
	}; ///< Content dimensions; default 800×600 logical points.
	::stana::reactive::Property<WindowPlacement> placement{
		CenteredPlacement{}
	}; ///< Default centered.
	::stana::reactive::Property<std::optional<Size>>
		minimumContentSize{}; ///< Unconstrained default.
	::stana::reactive::Property<std::optional<Size>>
		maximumContentSize{}; ///< Unconstrained default.
	::stana::reactive::Property<std::optional<double>>
		aspectRatio{}; ///< width/height; unconstrained.
	::stana::reactive::Property<std::optional<Size>>
		resizeIncrement{};							  ///< Platform default when empty.
	::stana::reactive::Property<std::string> title{}; ///< Owning UTF-8 title; default empty.
	::stana::reactive::Property<WindowDecorations> decorations{
		NativeDecorations{}
	}; ///< Native chrome.
	::stana::reactive::Property<WindowBackground> background{SystemBackground{}}; ///< System fill.
	::stana::reactive::Property<double> opacity{1.0}; ///< Finite opacity in [0, 1]; default 1.
	::stana::reactive::Property<std::optional<Theme>> theme{}; ///< Follows system when empty.
	::stana::reactive::Property<bool> hasShadow{true};		   ///< Subject to platform capability.
	::stana::reactive::Property<bool> isResizable{true};	   ///< Default resizable.
	::stana::reactive::Property<WindowControls>
		controls{}; ///< Defaults preserve native affordances.
	::stana::reactive::Property<WindowState> state{WindowState::normal}; ///< Default restored.
	::stana::reactive::Property<bool> isVisible{true};	  ///< Shown after successful creation.
	::stana::reactive::Property<bool> isEnabled{true};	  ///< Accepts interaction by default.
	::stana::reactive::Property<bool> acceptsFocus{true}; ///< May become key by default.
	::stana::reactive::Property<WindowActivation> activation{
		PlatformDefaultActivation{}
	}; ///< Default.
	::stana::reactive::Property<WindowPointerPolicy> pointerPolicy{
		NormalPointerPolicy{}
	}; ///< Normal.
	::stana::reactive::Property<WindowLevel> level{
		WindowLevel::normal
	}; ///< Portable stacking intent.
	::stana::reactive::Property<TaskSwitcherParticipation> taskSwitcher{
		TaskSwitcherParticipation::automatic
	}; ///< Task list participation; unsupported control is reported explicitly.
	::stana::reactive::Property<std::optional<WindowToolbar>>
		toolbar{}; ///< Owned toolbar description.
	::stana::reactive::Property<std::optional<WindowDocument>>
		document{};					  ///< Represented document.
	WindowDragAndDrop dragAndDrop{};  ///< Accepted formats, operations, and lifecycle handlers.
	WindowRestoration restoration{};  ///< Creation-only identity plus save/restore hooks.
	PlatformWindowOptions platform{}; ///< Typed OS extensions; default empty.
	WindowRequestStream requests{};	  ///< Ordered discrete action stream.
};

namespace detail {

/// @brief Resolved property snapshot used for validation before native apply.
struct WindowConfigurationSnapshot {
	WindowId id{};
	WindowRole role = StandardWindow{};
	Size contentSize{};
	WindowPlacement placement = CenteredPlacement{};
	std::optional<Size> minimumContentSize{};
	std::optional<Size> maximumContentSize{};
	std::optional<double> aspectRatio{};
	std::optional<Size> resizeIncrement{};
	std::string title{};
	WindowDecorations decorations = NativeDecorations{};
	WindowBackground background = SystemBackground{};
	double opacity = 1.0;
	std::optional<Theme> theme{};
	bool hasShadow = true;
	bool isResizable = true;
	WindowControls controls{};
	WindowState state = WindowState::normal;
	bool isVisible = true;
	bool isEnabled = true;
	bool acceptsFocus = true;
	WindowActivation activation = PlatformDefaultActivation{};
	WindowPointerPolicy pointerPolicy = NormalPointerPolicy{};
	WindowLevel level = WindowLevel::normal;
	TaskSwitcherParticipation taskSwitcher = TaskSwitcherParticipation::automatic;
};

/// @brief Validates a fully resolved configuration transaction snapshot.
///
/// Recoverable validation failures use `stana::Error` via `std::expected`, matching
/// other engine services. Geometry/opacity/constraint/screen problems map to
/// `Error::invalidValue`; nonsensical role ownership maps to `Error::invalidStructure`.
[[nodiscard]] auto validateConfiguration(const WindowConfigurationSnapshot &snapshot)
	-> std::expected<void, Error>;

/// @brief Samples mutable properties into a validation snapshot.
[[nodiscard]] auto sampleConfiguration(WindowId id, const WindowOptions &options)
	-> WindowConfigurationSnapshot;

} // namespace detail

} // namespace stana::application
