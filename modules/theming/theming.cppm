module;

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <variant>

export module stana:theming;
import :primitives;

export namespace stana::theming {

/// @brief Semantic color roles selected from the environment theme.
/// These are styling references, not concrete color-space representations.
enum class ColorRole : std::uint8_t {
	surface,	   ///< Document/control surface fill.
	background,	   ///< Recessed control fill such as text-field chrome.
	text,		   ///< Primary label color.
	secondaryText, ///< De-emphasized label color.
	accent,		   ///< Interactive accent fill.
	border		   ///< Outline color; high-contrast strengthens the gray.
};

/// @brief Semantic color reference with an absolute opacity override.
/// Does not own RGB or OKLCH coordinates. Resolution uses Environment theme
/// and high-contrast state. Opacity replaces the palette color's alpha.
struct ColorToken {
	ColorRole role = ColorRole::text; ///< Palette role selected at resolve time.
	Opacity alpha{1};				  ///< Absolute opacity applied after the role color is chosen.

	[[nodiscard]] static constexpr auto surface(Opacity alpha = Opacity{1}) -> ColorToken {
		return validated({.role = ColorRole::surface, .alpha = alpha});
	}

	[[nodiscard]] static constexpr auto background(Opacity alpha = Opacity{1}) -> ColorToken {
		return validated({.role = ColorRole::background, .alpha = alpha});
	}

	[[nodiscard]] static constexpr auto text(Opacity alpha = Opacity{1}) -> ColorToken {
		return validated({.role = ColorRole::text, .alpha = alpha});
	}

	[[nodiscard]] static constexpr auto secondaryText(Opacity alpha = Opacity{1}) -> ColorToken {
		return validated({.role = ColorRole::secondaryText, .alpha = alpha});
	}

	[[nodiscard]] static constexpr auto accent(Opacity alpha = Opacity{1}) -> ColorToken {
		return validated({.role = ColorRole::accent, .alpha = alpha});
	}

	[[nodiscard]] static constexpr auto border(Opacity alpha = Opacity{1}) -> ColorToken {
		return validated({.role = ColorRole::border, .alpha = alpha});
	}

	auto operator<=>(const ColorToken &) const = default;

  private:
	[[nodiscard]] static constexpr auto validated(ColorToken token) -> ColorToken {
		if (!std::isfinite(token.alpha.value()) || token.alpha.value() < 0 ||
			token.alpha.value() > 1) {
			throw std::invalid_argument("ColorToken opacity out of range");
		}
		return token;
	}
};

/// @brief Styling input holding either a concrete Color or a ColorToken.
/// Not a color-space representation and not a renderer paint payload.
/// Direct constructors from ColorRGBA and ColorOKLCH avoid chaining two
/// user-defined conversions. Color does not accept ColorToken or ColorSource.
class ColorSource {
  public:
	/// @brief Concrete opaque RGB black.
	ColorSource() : value_(Color{}) {}

	ColorSource(Color value) : value_(std::move(value)) {}

	ColorSource(ColorRGBA value) : value_(Color{value}) {}

	ColorSource(ColorOKLCH value) : value_(Color{value}) {}

	ColorSource(ColorToken token) : value_(validatedToken(token)) {}

	[[nodiscard]] auto asColor() const noexcept -> const Color * {
		return std::get_if<Color>(&value_);
	}

	[[nodiscard]] auto asToken() const noexcept -> const ColorToken * {
		return std::get_if<ColorToken>(&value_);
	}

	auto operator<=>(const ColorSource &) const = default;

	[[nodiscard]] auto operator==(const ColorSource &other) const -> bool {
		return std::is_eq(*this <=> other);
	}

  private:
	std::variant<Color, ColorToken> value_;

	[[nodiscard]] static auto validatedToken(ColorToken token) -> ColorToken {
		if (!std::isfinite(token.alpha.value()) || token.alpha.value() < 0 ||
			token.alpha.value() > 1) {
			throw std::invalid_argument("ColorToken opacity out of range");
		}
		return token;
	}
};

/// @brief Returns a concrete Color for paint preparation.
/// Concrete sources keep their representation and coordinates. Tokens select the
/// role color from the environment theme/high-contrast palette and apply the
/// token's absolute alpha. Does not convert OKLCH to RGB.
[[nodiscard]] inline auto resolveColor(const ColorSource &source, const Environment &environment)
	-> Color {
	if (const auto *color = source.asColor()) {
		return *color;
	}
	const auto *token = source.asToken();
	if (token == nullptr) {
		throw std::invalid_argument("ColorSource has no retained representation");
	}
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
	const bool dark = environment.theme == Theme::dark;
	Color result;
	switch (token->role) {
	case ColorRole::accent:
		result = Color::rgb(defaults::accentRed, defaults::accentGreen, defaults::accentBlue);
		break;
	case ColorRole::text:
		result = dark ? Color::white() : Color::black();
		break;
	case ColorRole::secondaryText:
		result = dark ? Color::rgb(secondaryDark, secondaryDark, secondaryDarkBlue)
					  : Color::rgb(secondaryLight, secondaryLight, secondaryLightBlue);
		break;
	case ColorRole::surface:
		result = dark ? Color::rgb(surfaceDark, surfaceDark, surfaceDarkBlue) : Color::white();
		break;
	case ColorRole::background:
		result = dark ? Color::rgb(backgroundDark, backgroundDark, backgroundDarkBlue)
					  : Color::rgb(backgroundLight, backgroundLight, backgroundLightBlue);
		break;
	case ColorRole::border: {
		const double value = environment.highContrast ? (dark ? 0.8 : 0.2) : (dark ? 0.36 : 0.76);
		result = Color::rgb(value, value, value);
		break;
	}
	}
	return result.withAlpha(token->alpha);
}

} // namespace stana::theming
