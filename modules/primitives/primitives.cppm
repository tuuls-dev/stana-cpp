module;

#include <algorithm>
#include <atomic>
#include <cmath>
#include <compare>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

export module stana:primitives;

export namespace stana {
namespace defaults {
inline constexpr double fontSize = 14;
inline constexpr double accentRed = 0.12;
inline constexpr double accentGreen = 0.38;
inline constexpr double accentBlue = 0.92;
inline constexpr double singularTolerance = 1e-12;
} // namespace defaults

/// @brief Recoverable operation failures returned by node handles and engine services.
/// Callers inspect std::expected results; these are not exception descriptions or
/// native error codes. Programmer-contract violations use diagnosed exceptions.
enum class Error : std::uint8_t {
	staleHandle,		 ///< The mounted occurrence or owning document no longer exists.
	wrongDocument,		 ///< An identity belongs to a different document namespace.
	invalidStructure,	 ///< A tree mutation violates parent/child or ordering invariants.
	duplicateKey,		 ///< A keyed region update contains repeated row keys.
	rowIdentityChanged,	 ///< An existing key now refers to a different observable object.
	invalidValue,		 ///< A recoverable service input failed validation.
	invalidTextRange,	 ///< A text range is out of bounds, misaligned or revision-stale.
	notFocusable,		 ///< The occurrence cannot currently receive the requested interaction.
	unsupportedPlatform, ///< No native implementation is available on this platform.
	nativeFailure,		 ///< A native service could not complete the operation.
	cancelled			 ///< The operation was cancelled before successful completion.
};

/// @brief A location in top-left, y-down logical coordinates.
/// Used by layout, input and paint commands; the containing API determines whether
/// the origin is local, document-relative or screen-relative. Not physical pixels.
struct Point {
	double x = 0; ///< Horizontal coordinate, increasing rightward.
	double y = 0; ///< Vertical coordinate, increasing downward.
	auto operator<=>(const Point &) const = default;

	[[nodiscard]] auto operator==(const Point &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Logical width and height used for measurement, window content and viewports.
/// Geometry-consuming APIs require finite, nonnegative dimensions. Native backends
/// convert logical points to device pixels using the window backing scale.
struct Size {
	double width = 0;  ///< Horizontal extent in logical points.
	double height = 0; ///< Vertical extent in logical points.
	auto operator<=>(const Size &) const = default;

	[[nodiscard]] auto operator==(const Size &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief An origin and extent in one logical coordinate space.
/// Layout publishes document-relative rectangles; Drawing accepts local rectangles.
/// contains() excludes the right/bottom edges so adjacent hit regions do not overlap.
struct Rect {
	Point origin{}; ///< Top-left corner in the coordinate space defined by the containing API.
	Size size{};	///< Nonnegative logical extent from origin.

	[[nodiscard]] auto contains(Point point) const noexcept -> bool {
		return point.x >= origin.x && point.y >= origin.y && point.x < origin.x + size.width &&
			point.y < origin.y + size.height;
	}

	[[nodiscard]] auto inset(double amount) const noexcept -> Rect {
		return {
			.origin = {.x = origin.x + amount, .y = origin.y + amount},
			.size = {
				.width = std::max(0.0, size.width - 2 * amount),
				.height = std::max(0.0, size.height - 2 * amount)
			}
		};
	}

	auto operator<=>(const Rect &) const = default;

	[[nodiscard]] auto operator==(const Rect &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Logical padding around a component content rectangle.
/// Set all edges with Insets::all(value), or designate top/leading/bottom/trailing.
/// Layout resolves leading/trailing against the document direction on each commit.
struct Insets {
	double top = 0;		 ///< Inset from the physical top edge.
	double leading = 0;	 ///< Inset from the direction-dependent leading edge.
	double bottom = 0;	 ///< Inset from the physical bottom edge.
	double trailing = 0; ///< Inset from the direction-dependent trailing edge.

	[[nodiscard]] static constexpr auto all(double value) noexcept -> Insets {
		return {.top = value, .leading = value, .bottom = value, .trailing = value};
	}

	auto operator<=>(const Insets &) const = default;

	[[nodiscard]] auto operator==(const Insets &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Logical reading direction used to resolve leading/trailing layout and row order.
/// This does not override Core Text shaping direction inferred from the actual text.
enum class Direction : std::uint8_t {
	leftToRight, ///< Leading is the left edge; row order advances rightward.
	rightToLeft	 ///< Leading is the right edge; row order advances leftward.
};
/// @brief Logical horizontal alignment inside a container content rectangle.
/// Leading/trailing are direction-aware; center is independent of reading direction.
enum class Alignment : std::uint8_t {
	leading, ///< Aligns to the logical leading edge of the content rectangle.
	center,	 ///< Centers within the available horizontal extent.
	trailing ///< Aligns to the logical trailing edge of the content rectangle.
};
/// @brief Layout/paint participation without changing the mounted lifetime.
/// visible paints normally, hidden reserves layout but does not paint or interact,
/// and collapsed removes its layout footprint. Use If for actual disposal.
enum class Visibility : std::uint8_t {
	visible,  ///< Participates in layout, painting and eligible input.
	hidden,	  ///< Retains layout and state but does not paint or accept input.
	collapsed ///< Retains state but contributes no layout footprint or paint.
};
/// @brief Document appearance selection used by scene defaults.
/// Native macOS sessions copy the system appearance into Environment::theme.
/// The enum itself does not create a native application or window.
enum class Theme : std::uint8_t {
	light, ///< Uses the light document appearance defaults.
	dark   ///< Uses the dark document appearance defaults.
};

/// @brief A typed sizing request resolved by layout against available logical space.
/// Use points(), fill(), percent() or the default automatic kind. An automatic
/// axis uses intrinsic measurement; percentage values use 100 as the full extent.
struct Length {
	enum class Kind : std::uint8_t {
		automatic, ///< Uses measured intrinsic size for this axis.
		points,	   ///< Uses value as an absolute logical extent.
		fill,	   ///< Requests the available parent extent for this axis.
		percent	   ///< Uses value / 100 of the available parent extent.
	};
	Kind kind = Kind::automatic; ///< Rule layout uses to interpret value.
	double value = 0;			 ///< Point extent or percentage; ignored for automatic sizing.

	[[nodiscard]] static constexpr auto points(double value) noexcept -> Length {
		return {.kind = Kind::points, .value = value};
	}

	[[nodiscard]] static constexpr auto fill() noexcept -> Length {
		return {.kind = Kind::fill, .value = 1};
	}

	[[nodiscard]] static constexpr auto percent(double value) noexcept -> Length {
		return {.kind = Kind::percent, .value = value};
	}

	auto operator<=>(const Length &) const = default;

	[[nodiscard]] auto operator==(const Length &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

inline namespace literals {
[[nodiscard]] constexpr auto operator""_px(long double value) noexcept -> Length {
	return Length::points(static_cast<double>(value));
}

[[nodiscard]] constexpr auto operator""_px(unsigned long long value) noexcept -> Length {
	return Length::points(static_cast<double>(value));
}
} // namespace literals

/// @brief Sizing and constraint overrides for LayoutProperties::frame.
/// Each axis is automatic unless explicitly set. Optional minima/maxima constrain
/// the resolved size; omission does not impose a zero limit. Values are logical points.
struct Frame {
	Length width{};					   ///< Horizontal sizing rule; automatic uses intrinsic width.
	Length height{};				   ///< Vertical sizing rule; automatic uses intrinsic height.
	std::optional<double> minWidth{};  ///< Optional lower bound on resolved width.
	std::optional<double> minHeight{}; ///< Optional lower bound on resolved height.
	std::optional<double> maxWidth{};  ///< Optional upper bound on resolved width.
	std::optional<double> maxHeight{}; ///< Optional upper bound on resolved height.
	auto operator<=>(const Frame &) const = default;

	[[nodiscard]] auto operator==(const Frame &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Portable RGBA paint value used by styling and drawing commands.
/// Components store these values without native color objects; renderer adapters
/// interpret them in the scene color contract. Alpha is opacity, with zero transparent.
struct Color {
	enum class Token : std::uint8_t {
		explicitColor,
		surface,
		background,
		text,
		secondaryText,
		accent,
		border
	};
	double red = 0;						///< Red channel in the scene color contract.
	double green = 0;					///< Green channel in the scene color contract.
	double blue = 0;					///< Blue channel in the scene color contract.
	double alpha = 1;					///< Opacity; zero is transparent and one is opaque.
	Token token = Token::explicitColor; ///< Explicit numeric colors are never theme-dependent.

	[[nodiscard]] static constexpr auto
	rgb(double red, double green, double blue, double alpha = 1) noexcept -> Color {
		return {.red = red, .green = green, .blue = blue, .alpha = alpha};
	}

	[[nodiscard]] static constexpr auto white() noexcept -> Color {
		return {.red = 1, .green = 1, .blue = 1, .alpha = 1};
	}

	[[nodiscard]] static constexpr auto black() noexcept -> Color {
		return {.red = 0, .green = 0, .blue = 0, .alpha = 1};
	}

	[[nodiscard]] static constexpr auto clear() noexcept -> Color {
		return {.red = 0, .green = 0, .blue = 0, .alpha = 0};
	}

	[[nodiscard]] static constexpr auto accent() noexcept -> Color {
		return {
			.red = defaults::accentRed,
			.green = defaults::accentGreen,
			.blue = defaults::accentBlue,
			.alpha = 1,
			.token = Token::accent
		};
	}

	[[nodiscard]] static constexpr auto surface() noexcept -> Color {
		return {.token = Token::surface};
	}

	[[nodiscard]] static constexpr auto background() noexcept -> Color {
		return {.token = Token::background};
	}

	[[nodiscard]] static constexpr auto text() noexcept -> Color { return {.token = Token::text}; }

	[[nodiscard]] static constexpr auto secondaryText() noexcept -> Color {
		return {.token = Token::secondaryText};
	}

	[[nodiscard]] static constexpr auto border() noexcept -> Color {
		return {.token = Token::border};
	}

	[[nodiscard]] auto resolved(Theme theme, bool highContrast = false) const noexcept -> Color {
		constexpr double secondaryDark = 0.72;
		constexpr double secondaryDarkBlue = 0.74;
		constexpr double secondaryLight = 0.36;
		constexpr double secondaryLightBlue = 0.38;
		constexpr double surfaceDark = 0.17;
		constexpr double surfaceDarkBlue = 0.18;
		constexpr double backgroundDark = 0.11;
		constexpr double backgroundDarkBlue = 0.12;
		constexpr double backgroundLight = 0.96;
		constexpr double backgroundLightBlue = 0.97;
		const bool dark = theme == Theme::dark;
		Color result;
		switch (token) {
		case Token::explicitColor:
			return *this;
		case Token::accent:
			result = rgb(defaults::accentRed, defaults::accentGreen, defaults::accentBlue);
			break;
		case Token::text:
			result = dark ? white() : black();
			break;
		case Token::secondaryText:
			result = dark ? rgb(secondaryDark, secondaryDark, secondaryDarkBlue)
						  : rgb(secondaryLight, secondaryLight, secondaryLightBlue);
			break;
		case Token::surface:
			result = dark ? rgb(surfaceDark, surfaceDark, surfaceDarkBlue) : white();
			break;
		case Token::background:
			result = dark ? rgb(backgroundDark, backgroundDark, backgroundDarkBlue)
						  : rgb(backgroundLight, backgroundLight, backgroundLightBlue);
			break;
		case Token::border: {
			const double value = highContrast ? (dark ? 0.8 : 0.2) : (dark ? 0.36 : 0.76);
			result = rgb(value, value, value);
			break;
		}
		}
		result.alpha = alpha;
		return result;
	}

	auto operator<=>(const Color &) const = default;

	[[nodiscard]] auto operator==(const Color &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief System-font request shared by text measurement and painting.
/// Set size in logical points and weight using the native system-font weight scale.
/// Environment::textScale multiplies size; custom font-family selection is not exposed.
struct Font {
	double size = defaults::fontSize; ///< Unscaled logical point size; Environment::textScale is
									  ///< applied by the document.
	double weight = 0; ///< System font weight request, with zero representing regular weight.

	[[nodiscard]] static constexpr auto system(double size, double weight = 0) noexcept -> Font {
		return {.size = size, .weight = weight};
	}

	auto operator<=>(const Font &) const = default;

	[[nodiscard]] auto operator==(const Font &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Portable 2D affine mapping for local-to-parent geometry.
/// apply() maps one point; inverse() returns no value for a nonfinite or near-singular
/// linear part. Coefficients follow x′=a*x+c*y+tx and y′=b*x+d*y+ty.
struct AffineTransform {
	double a = 1;  ///< Horizontal contribution of the input x coordinate.
	double b = 0;  ///< Vertical contribution of the input x coordinate.
	double c = 0;  ///< Horizontal contribution of the input y coordinate.
	double d = 1;  ///< Vertical contribution of the input y coordinate.
	double tx = 0; ///< Horizontal translation in the destination coordinate space.
	double ty = 0; ///< Vertical translation in the destination coordinate space.

	/// @brief Returns the mapping that leaves every point unchanged.
	[[nodiscard]] static constexpr auto identity() noexcept -> AffineTransform { return {}; }

	/// @brief Returns a pure translation. x′=x+tx and y′=y+ty.
	[[nodiscard]] static constexpr auto translation(double x, double y) noexcept
		-> AffineTransform {
		return {.tx = x, .ty = y};
	}

	/// @brief Returns an axis-aligned scale about the origin.
	[[nodiscard]] static constexpr auto scale(double x, double y) noexcept -> AffineTransform {
		return {.a = x, .d = y};
	}

	/// @brief Returns a clockwise rotation about the origin in y-down coordinates.
	/// The angle is radians. Trigonometry is not a constant expression on this standard library.
	[[nodiscard]] static auto rotation(double radians) noexcept -> AffineTransform {
		return {
			.a = std::cos(radians),
			.b = std::sin(radians),
			.c = -std::sin(radians),
			.d = std::cos(radians)
		};
	}

	/// @brief Returns the mapping that applies @p other first and then this transform.
	[[nodiscard]] constexpr auto concatenating(const AffineTransform &other) const noexcept
		-> AffineTransform {
		return {
			.a = a * other.a + c * other.b,
			.b = b * other.a + d * other.b,
			.c = a * other.c + c * other.d,
			.d = b * other.c + d * other.d,
			.tx = a * other.tx + c * other.ty + tx,
			.ty = b * other.tx + d * other.ty + ty
		};
	}

	/// @brief Largest singular value of the linear part, or zero when that part is zero.
	/// Used to choose symbol preparation scale. Nonfinite coefficients yield a nonfinite result.
	[[nodiscard]] auto maximumScale() const noexcept -> double {
		const double dominant = std::max({std::abs(a), std::abs(b), std::abs(c), std::abs(d)});
		if (!(dominant > 0)) {
			return 0;
		}
		const double horizontalX = a / dominant;
		const double verticalX = b / dominant;
		const double horizontalY = c / dominant;
		const double verticalY = d / dominant;
		const double trace = horizontalX * horizontalX + verticalX * verticalX +
			horizontalY * horizontalY + verticalY * verticalY;
		const double determinant = horizontalX * verticalY - verticalX * horizontalY;
		const double discriminant = trace * trace - 4 * determinant * determinant;
		return dominant * std::sqrt((trace + std::sqrt(std::max(0.0, discriminant))) / 2);
	}

	[[nodiscard]] constexpr auto apply(Point point) const noexcept -> Point {
		return {.x = a * point.x + c * point.y + tx, .y = b * point.x + d * point.y + ty};
	}

	[[nodiscard]] auto inverse() const noexcept -> std::optional<AffineTransform> {
		const double determinant = a * d - b * c;
		if (!std::isfinite(determinant) || std::abs(determinant) < defaults::singularTolerance) {
			return std::nullopt;
		}
		return AffineTransform{
			.a = d / determinant,
			.b = -b / determinant,
			.c = -c / determinant,
			.d = a / determinant,
			.tx = (c * ty - d * tx) / determinant,
			.ty = (b * tx - a * ty) / determinant
		};
	}

	auto operator<=>(const AffineTransform &) const = default;

	[[nodiscard]] auto operator==(const AffineTransform &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

/// @brief Process-local identity of one retained occurrence in one document.
/// The document namespace prevents cross-window aliasing; serials are never reused.
/// Use equality for identity checks, not ordering as a proxy for current paint order.
struct NodeId {
	std::uint64_t document = 0; ///< Never-reused namespace of the owning retained document.
	std::uint64_t serial = 0;	///< Never-reused occurrence token; zero denotes no occurrence.
	auto operator<=>(const NodeId &) const = default;
};

/// @brief Window/document-wide inputs used when resolving component presentation.
/// TestHost and native sessions own a copy. Direction affects logical layout and
/// textScale affects measurement; changing environment does not remount components.
/// A macOS session updates theme when the system appearance changes.
struct Environment {
	Theme theme =
		Theme::light; ///< Light or dark scene defaults. Native sessions follow the system.
	Direction direction =
		Direction::leftToRight; ///< Logical direction used to resolve layout edges and row order.
	double textScale = 1;		///< Nonnegative multiplier applied to requested system-font sizes.
	bool reducedMotion =
		false; ///< Accessibility preference for motion consumers; does not change layout.
	bool highContrast =
		false; ///< Accessibility contrast preference available to appearance consumers.
	auto operator<=>(const Environment &) const = default;

	[[nodiscard]] auto operator==(const Environment &other) const -> bool {
		return std::is_eq(*this <=> other);
	}
};

namespace detail {
/// @brief Reserves a never-reused process-local token for documents, nodes and resources.
/// Relaxed atomic ordering guarantees uniqueness only, not publication of UI state.
/// Throws on exhaustion instead of wrapping into an existing identity.
[[nodiscard]] inline auto nextIdentity() -> std::uint64_t {
	static std::atomic<std::uint64_t> counter{1};
	auto current = counter.load(std::memory_order_relaxed);
	for (;;) {
		if (current == std::numeric_limits<std::uint64_t>::max()) {
			throw std::overflow_error("Stana identity exhausted");
		}
		if (counter.compare_exchange_weak(current, current + 1, std::memory_order_relaxed)) {
			return current;
		}
	}
}

inline void requireFinite(double value) {
	if (!std::isfinite(value)) {
		throw std::invalid_argument("Stana requires finite geometry");
	}
}

inline void requireNonnegative(double value) {
	requireFinite(value);
	if (value < 0) {
		throw std::invalid_argument("Stana requires nonnegative geometry");
	}
}
} // namespace detail
} // namespace stana
