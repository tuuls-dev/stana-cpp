#ifndef STANA_PLATFORM_TEXT_CONTRACTS_HPP
#define STANA_PLATFORM_TEXT_CONTRACTS_HPP
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace stana::platform::detail {
/// @brief Immutable native glyph owner. No UI objects or document references are permitted.
class TextResource {
  public:
	TextResource() = default;
	TextResource(const TextResource &) = delete;
	TextResource(TextResource &&) = delete;
	auto operator=(const TextResource &) -> TextResource & = delete;
	auto operator=(TextResource &&) -> TextResource & = delete;
	virtual ~TextResource() = default;

	/// @brief Unwraps a checked application bridge; concrete native resources return no wrapper.
	[[nodiscard]] virtual auto nativeOwner() const -> std::shared_ptr<const TextResource> {
		return {};
	}
};

struct FontRequest {
	double size = 0;
	double weight = 0;
	std::optional<std::string> family{};
	bool italic = false;
};

struct NativeCaret {
	std::size_t utf16Offset = 0;
	double primary = 0;
	double secondary = 0;
};

struct NativeTextRun {
	std::size_t start = 0;
	std::size_t end = 0;
	double x = 0;
	double width = 0;
	bool rightToLeft = false;
};

struct NativeTextLine {
	double width = 0;
	double ascent = 0;
	double descent = 0;
	double leading = 0;
	double xHeight = 0;
	double underlineThickness = 1;
	std::vector<NativeCaret> carets{};
	std::vector<NativeTextRun> runs{};
	std::shared_ptr<const TextResource> resource{};
};

[[nodiscard]] auto shapeTextLine(const std::string &text, const FontRequest &font, bool rightToLeft)
	-> std::expected<NativeTextLine, std::string>;
[[nodiscard]] auto readTextClipboard() -> std::expected<std::optional<std::string>, std::string>;
[[nodiscard]] auto writeTextClipboard(const std::string &text) -> std::expected<void, std::string>;

/// @brief Mirror of portable semantic commands, explicitly converted by the application bridge.
enum class TextCommand : std::uint8_t {
	left,
	right,
	wordBackward,
	wordForward,
	lineStart,
	lineEnd,
	selectLeft,
	selectRight,
	selectWordBackward,
	selectWordForward,
	selectLineStart,
	selectLineEnd,
	deleteBackward,
	deleteForward,
	deleteWordBackward,
	deleteWordForward,
	selectAll,
	cut,
	copy,
	paste,
	undo,
	redo,
	submit,
	cancel,
	nextFocus,
	previousFocus
};

struct NativeTextTarget {
	std::uint64_t document = 0;
	std::uint64_t node = 0;
	std::uint64_t session = 0;
	std::uint64_t store = 0;
};

/// @brief Ordered half-open UTF-16 range. Adapters validate addition before constructing it.
struct NativeTextRange {
	std::size_t start = 0;
	std::size_t end = 0;
};

struct NativeTextState {
	NativeTextTarget target{};
	std::string displayed{};
	NativeTextRange selected{};
	std::optional<NativeTextRange> marked{};
};

struct NativeTextEdit {
	enum class Kind : std::uint8_t { insert, mark, commit, cancel };
	Kind kind = Kind::insert;
	std::string text{};
	std::optional<NativeTextRange> replacement{};
	NativeTextRange selection{};
};

struct NativeTextRect {
	double x = 0;
	double y = 0;
	double width = 0;
	double height = 0;
};

/// @brief Per-window attachment operations; installed before native view creation.
struct TextInputHost {
	std::function<bool()> finish{};
	std::function<void()> discard{};
	std::function<void()> invalidateGeometry{};
};

struct NativePointer {
	double x = 0;
	double y = 0;
	std::uint8_t button = 0;
	bool shift = false;
	bool control = false;
	bool alt = false;
	bool meta = false;
	std::uint32_t clickCount = 1;
};

enum class NativeTextKey : std::uint8_t { text, tab, enter, escape };

struct TextInputCallbacks {
	std::shared_ptr<TextInputHost> host{};
	std::function<std::optional<NativeTextState>()> state{};
	std::function<std::uint64_t()> begin{};
	std::function<void(std::uint64_t)> end{};
	std::function<bool(NativeTextTarget, std::uint64_t, const NativeTextEdit &)> edit{};
	std::function<bool(NativeTextTarget, std::uint64_t, TextCommand)> command{};
	std::function<bool(NativeTextTarget, TextCommand)> canEdit{};
	std::function<std::optional<NativeTextRect>(NativeTextTarget, std::size_t)> caret{};
	std::function<std::optional<std::size_t>(double, double)> index{};
	std::function<std::optional<NativeTextTarget>(double, double)> contextMenu{};
	std::function<void(bool)> active{};
	std::function<bool(NativeTextKey, bool, bool)>
		key{}; ///< Return false when a routed key prevents interpretation.
};
} // namespace stana::platform::detail
#endif
