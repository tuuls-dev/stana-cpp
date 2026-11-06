module;
#include <array>
#include <compare>
#include <optional>
#include <span>
#include <vector>
export module stana:rendering_shadow;
import :primitives;

export namespace stana::rendering {
/// @brief One CSS-style box silhouette shadow in logical points; first list entry is frontmost.
/// Offset/spread are signed; blur is finite and nonnegative. Missing color follows foreground.
/// Shadows do not change layout or hit areas. Consuming APIs throw invalid_argument on invalid
/// input.
struct BoxShadow {
	Point offset{};				  ///< Signed horizontal/vertical displacement.
	double blurRadius = 0;		  ///< Gaussian sigma is half this radius; zero has a sharp edge.
	double spreadRadius = 0;	  ///< Expands outer casters; contracts inset openings.
	std::optional<Color> color{}; ///< Explicit/themed color, or effective foreground when absent.
	bool isInset = false; ///< Clips the complement of the padding opening inside that contour.
	auto operator<=>(const BoxShadow &) const = default;

	[[nodiscard]] auto operator==(const BoxShadow &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Interpolates index-paired shadow lists using premultiplied scene-linear colors.
/// @param from Starting ordered list.
/// @param to Ending ordered list; shorter lists are padded with transparent matching-inset shadows.
/// @param progress Finite value in [0,1]; incompatible inset pairs switch the entire list at 0.5.
/// @param foreground Resolves omitted colors before interpolation, using environment
/// theme/contrast.
/// @param environment Color-resolution inputs; no animation engine or implicit scheduling is
/// involved.
/// @return Owning list with explicit resolved colors. Throws invalid_argument for invalid inputs.
[[nodiscard]] auto interpolateBoxShadows(
	std::span<const BoxShadow> from,
	std::span<const BoxShadow> to,
	double progress,
	Color foreground = Color::text(),
	Environment environment = {}
) -> std::vector<BoxShadow>;

namespace detail {
/// @brief Resolved physical contour: radii run top-left, top-right, bottom-right, bottom-left.
struct BoxContour {
	Rect bounds{};
	std::array<Size, 4> radii{};
	auto operator<=>(const BoxContour &) const = default;

	[[nodiscard]] auto operator==(const BoxContour &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

struct BoxGeometry {
	BoxContour outer{};
	BoxContour padding{};
};

/// @brief Immutable paint payload shared by retained projection and explicit GPU drawing.
struct ShadowPaint {
	BoxContour clip{};
	BoxContour caster{};
	Point offset{};
	Color color{};
	double blurRadius = 0;
	bool isInset = false;
	Rect ink{};
	[[nodiscard]] auto operator==(const ShadowPaint &) const -> bool = default;
};

/// @brief Exact finite Gaussian support is 3 sigma, plus one pixel for coverage/placement.
inline constexpr double shadowSigmaSupport = 3;
void validateBoxShadows(std::span<const BoxShadow>);
void validateCornerRadii(CornerRadii);
void validateBorderWidths(BorderWidths);
[[nodiscard]] auto
	boxGeometry(Rect, CornerRadii = {}, BorderWidths = {}, Direction = Direction::leftToRight)
		-> BoxGeometry;
[[nodiscard]] auto prepareBoxShadows(
	Rect,
	std::span<const BoxShadow>,
	CornerRadii = {},
	BorderWidths = {},
	Color foreground = Color::text(),
	Environment = {}
) -> std::vector<ShadowPaint>;
} // namespace detail
} // namespace stana::rendering
