module;

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
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

/// @brief Ordered half-open UTF-8 byte range, validated against its accompanying stamp.
struct TextRange {
	std::size_t start = 0;
	std::size_t end = 0;
	auto operator<=>(const TextRange &) const = default;
};

/// @brief Identifies all observable store state, including selection and composition.
struct StoreStamp {
	std::uint64_t revision = 0;
	auto operator<=>(const StoreStamp &) const = default;
};

/// @brief Owning provisional replacement; selection offsets are relative to text.
struct Composition {
	TextRange replacement{};
	TextSelection originalSelection{};
	std::string text{};
	TextRange selection{};
	auto operator==(const Composition &) const -> bool = default;
};

/// @brief Owning snapshot; committed content and provisional bytes stay separate.
struct StoreSnapshot {
	StoreStamp stamp{};
	std::string text{};
	TextSelection selection{};
	std::optional<Composition> composition{};
	bool canUndo = false;
	bool canRedo = false;
};

/// @brief Coordinate space of an explicitly supplied replacement/selection range.
enum class TextSpace : std::uint8_t { committed, displayed };

/// @brief One validated store operation. Missing replacement resolves current selection/preedit.
struct StoreEdit {
	enum class Kind : std::uint8_t { replace, select, compose, commit, cancel };
	Kind kind = Kind::replace;
	StoreStamp stamp{};
	TextSpace space = TextSpace::displayed;
	std::optional<TextRange> replacement{};
	std::string text{};
	TextRange selection{};
	bool groupWithPrevious = false;
};

/// @brief Returns whether UTF-8 content is acceptable to a single-line editor.
[[nodiscard]] auto isSingleLine(std::string_view text) noexcept -> bool;
/// @brief Converts each Unicode line break to one space; rejects malformed UTF-8.
[[nodiscard]] auto normalizePaste(std::string_view text) -> std::expected<std::string, Error>;
/// @brief Unicode word/line break positions, retaining UTF-8 byte coordinates.
[[nodiscard]] auto wordBoundaries(const std::string &text) -> std::vector<std::size_t>;
[[nodiscard]] auto lineBoundaries(const std::string &text) -> std::vector<std::size_t>;

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
	void cancelMarkedText();

	/// @brief Restores the previous edit group; returns false when history is empty.
	[[nodiscard]] auto undo() -> bool;
	/// @brief Reapplies the most recently undone group; returns false when none exists.
	[[nodiscard]] auto redo() -> bool;

	/// @brief Returns an owning snapshot of all observable editor state.
	[[nodiscard]] auto snapshot() const -> StoreSnapshot;

	[[nodiscard]] auto stamp() const noexcept -> StoreStamp { return {storeRevision_}; }

	[[nodiscard]] auto displayedText() const -> std::string;
	[[nodiscard]] auto displayedSelection() const -> TextRange;
	/// @brief Rejects stale/misaligned operations without changing state or history.
	[[nodiscard]] auto apply(const StoreEdit &edit) -> std::expected<void, Error>;

  private:
	std::uint64_t storeRevision_ = 0;
	std::optional<Composition> composition_{};

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
/// @brief Soft wrapping policy; hard line breaks are honored independently.
enum class Wrap : std::uint8_t { none, word, grapheme };

/// @brief Full resolved request. Missing width is unbounded, never infinity.
struct LayoutRequest {
	std::string text{};
	Font font{};
	Direction direction = Direction::leftToRight;
	Wrap wrap = Wrap::none;
	std::optional<Points> width{};
	std::optional<Points> lineHeight{};
	Alignment alignment = Alignment::leading;
	std::optional<std::size_t> maxLines{};
	std::uint64_t environment = 0;
	auto operator==(const LayoutRequest &) const -> bool = default;
};

/// @brief Immutable provider-owned glyph data; safe to retain beyond document lifetime.
class PaintResource {
  public:
	PaintResource() = default;
	PaintResource(const PaintResource &) = delete;
	PaintResource(PaintResource &&) = delete;
	auto operator=(const PaintResource &) -> PaintResource & = delete;
	auto operator=(PaintResource &&) -> PaintResource & = delete;
	virtual ~PaintResource() = default;
};

/// @brief A scalar query boundary; secondary preserves split bidi caret affinity.
struct CaretStop {
	std::size_t offset = 0;
	Points primary{};
	Points secondary{};
	bool isGrapheme = true;
};

/// @brief One shaped visual run in line-relative byte coordinates.
struct VisualRun {
	TextRange source{};
	Points x{};
	Points width{};
	bool rightToLeft = false;
};

/// @brief Immutable shaped line, with all geometry in logical points.
struct ShapedLine {
	Points width{};
	Points ascent{};
	Points descent{};
	Points leading{};
	Points xHeight{};
	Points underlineThickness{1};
	std::vector<CaretStop> carets{};
	std::vector<VisualRun> runs{};
	std::shared_ptr<const PaintResource> resource{};
};

/// @brief A source-preserving line positioned within a layout.
struct LayoutLine {
	TextRange source{};
	std::size_t inkEnd = 0;
	Point origin{};
	Points baseline{};
	Points height{};
	ShapedLine shape{};
};

/// @brief Complete owning geometry and paint resources for one resolved request.
struct TextLayout {
	std::uint64_t identity = 0;
	LayoutRequest request{};
	Size size{};
	std::vector<LayoutLine> lines{};
	/// @brief Returns nearest grapheme stop in visual order.
	[[nodiscard]] auto hit(Point point, bool graphemesOnly = true) const -> std::size_t;
	/// @brief Returns a line-local insertion caret, preserving requested bidi affinity.
	[[nodiscard]] auto caret(std::size_t offset, bool secondary = false) const -> Rect;
	/// @brief Returns visual selection fragments in layout coordinates.
	[[nodiscard]] auto selection(TextRange range) const -> std::vector<Rect>;
};

using LineShaper = std::function<ShapedLine(const std::string &, const Font &, Direction)>;
using LayoutProvider = std::function<std::shared_ptr<const TextLayout>(const LayoutRequest &)>;

/// @brief Builds Unicode-aware layout with immutable provider-shaped lines.
/// Omitted shaper uses deterministic 8-point scalar advances and 20-point line boxes at 14pt.
[[nodiscard]] auto layoutText(const LayoutRequest &request, const LineShaper &shaper = {})
	-> std::shared_ptr<const TextLayout>;
} // namespace stana::text
