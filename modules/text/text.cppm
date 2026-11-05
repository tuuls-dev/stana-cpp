module;

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module stana:text;
import :primitives;

export namespace stana::text {
/// @brief Identifies a UTF-8 byte boundary in one specific editor revision.
///
/// TextBuffer returns these positions for selection and native-service requests.
/// Keep revision with the offset: an offset alone must not be applied after an
/// intervening edit, because it may address different content or split a grapheme.
struct TextPosition {
	std::size_t utf8Offset = 0; ///< Byte offset from the beginning of committed UTF-8 text.
	std::uint64_t revision = 0; ///< Editor content revision in which the offset is valid.
	auto operator<=>(const TextPosition &) const = default;
};

/// @brief Direction-preserving selection in committed editor text.
/// anchor is the fixed end and head is the moving caret. A collapsed selection
/// has equal offsets; ranges are validated against one shared current revision.
struct TextSelection {
	TextPosition anchor{}; ///< Fixed endpoint, retained during shift-selection.
	TextPosition head{};   ///< Moving endpoint and caret position.
	auto operator<=>(const TextSelection &) const = default;
};

/// @brief Returns whether all bytes form canonical, well-formed UTF-8 scalar values.
/// Embedded NUL is valid; overlong encodings, surrogates and truncated sequences are not.
[[nodiscard]] auto isValidUtf8(std::string_view text) noexcept -> bool;
/// @brief Computes extended-grapheme boundaries as UTF-8 byte offsets using ICU.
/// Includes offsets zero and text.size(); the empty string has one boundary.
/// @throws std::invalid_argument if text is malformed UTF-8 or exceeds ICU's size limit.
[[nodiscard]] auto graphemeBoundaries(const std::string &text) -> std::vector<std::size_t>;
/// @brief Converts a validated UTF-8 boundary into a native UTF-16 code-unit offset.
/// @return Error::invalidTextRange for malformed text or a split scalar boundary.
[[nodiscard]] auto utf16Offset(const std::string &text, std::size_t utf8Offset)
	-> std::expected<std::size_t, Error>;
/// @brief Converts a native UTF-16 boundary to a UTF-8 byte boundary without rounding.
/// @return Error::invalidTextRange for a split surrogate pair or out-of-range offset.
[[nodiscard]] auto utf8Offset(const std::string &text, std::size_t utf16Offset)
	-> std::expected<std::size_t, Error>;

/// @brief Owns single-line editor content, selection, composition and undo history.
///
/// Editing adapters use this value rather than adopting an NSTextField text store.
/// select() and replaceSelection() validate revisioned grapheme positions; external
/// setText() cancels composition only when the model value actually differs.
/// Marked text remains separate from committed text until commitMarkedText().
///
/// This portable buffer performs no native application initialization and is UI-
/// executor-confined by its containing editor. Copies are independent edit histories.
class TextBuffer {
  public:
	explicit TextBuffer(std::string text = {});

	/// @brief Returns committed UTF-8 model content, excluding uncommitted marked text.
	[[nodiscard]] auto text() const -> const std::string & { return text_; }

	/// @brief Returns the current content revision used by all accepted positions.
	[[nodiscard]] auto revision() const noexcept -> std::uint64_t { return revision_; }

	/// @brief Returns the direction-preserving committed-text selection.
	[[nodiscard]] auto selection() const noexcept -> TextSelection;
	/// @brief Replaces selection endpoints after checking revision and grapheme boundaries.
	[[nodiscard]] auto select(TextSelection selection) -> std::expected<void, Error>;
	/// @brief Replaces selected committed text and places the caret after the insertion.
	/// @param groupWithPrevious Joins this edit with the prior undo group when true.
	[[nodiscard]] auto
	replaceSelection(const std::string &insertion, bool groupWithPrevious = false)
		-> std::expected<void, Error>;
	/// @brief Replaces content from an external binding; equal echoes preserve composition.
	[[nodiscard]] auto setText(std::string text) -> std::expected<void, Error>;
	/// @brief Stores IME provisional text without writing it into the model.
	[[nodiscard]] auto setMarkedText(std::string text) -> std::expected<void, Error>;

	/// @brief Returns provisional IME text, or no value when not composing.
	[[nodiscard]] auto markedText() const -> const std::optional<std::string> & { return marked_; }

	/// @brief Commits provisional IME text through the normal validated replacement path.
	[[nodiscard]] auto commitMarkedText() -> std::expected<void, Error>;

	/// @brief Abandons provisional IME text without changing committed content.
	void cancelMarkedText() { marked_.reset(); }

	/// @brief Restores the previous edit group; returns false when history is empty.
	[[nodiscard]] auto undo() -> bool;
	/// @brief Reapplies the most recently undone group; returns false when none exists.
	[[nodiscard]] auto redo() -> bool;

  private:
	/// @brief Undo value owning text and unrevisioned selection offsets for restoration.
	struct EditState {
		std::string text{};		///< Committed model text before or after an edit group.
		std::size_t anchor = 0; ///< Saved fixed selection endpoint in UTF-8 bytes.
		std::size_t head = 0;	///< Saved moving selection endpoint in UTF-8 bytes.
	};

	std::string text_{};		 ///< Authoritative committed UTF-8 editor model.
	std::uint64_t revision_ = 0; ///< Never-reused revision advanced on content replacement.
	std::size_t anchor_ = 0;	 ///< Current fixed endpoint, validated as a grapheme boundary.
	std::size_t head_ = 0; ///< Current moving endpoint/caret, validated as a grapheme boundary.
	std::optional<std::string> marked_{}; ///< Provisional IME text not yet committed to the model.
	std::vector<EditState> undo_{};		  ///< Earlier grouped committed states, newest at the back.
	std::vector<EditState> redo_{};		  ///< Undone groups available until the next new edit.
	[[nodiscard]] auto currentState() const -> EditState;
	void restore(EditState state);
};
} // namespace stana::text
