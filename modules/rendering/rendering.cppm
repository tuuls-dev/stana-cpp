module;

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module stana:rendering;
export import :rendering_presentation;
export import :rendering_gpu;
export import :rendering_shadow;
export import :primitives;
export import :rendering_diagnostics;

export namespace stana::rendering {

/// @brief Defines the rendering primitive stored in a paint command.
enum class PaintKind : std::uint8_t {
	fillRect,	///< Fills a rectangle, optionally with rounded corners.
	strokeRect, ///< Outlines a rectangle using lineWidth and optional rounded corners.
	text,		///< Draws text using the supplied font and foreground color.
	line,		///< Draws a segment from bounds.origin to origin + size.
	pushClip,	///< Saves graphics state and intersects it with the supplied clip region.
	boxShadow,	///< Built-in GPU shadow; projection-only, not a CPUCanvas authoring operation.
	popClip		///< Restores the graphics state saved by the matching pushClip.
};

/// @brief Represents one portable display-list operation in top-left logical coordinates.
///
/// Drawing records local-coordinate commands; Document translates them into a
/// complete SceneSnapshot consumed by the native renderer. Commands own their
/// strings and value parameters, with no OS contexts or borrowed resource handles.
/// Published projections are immutable; native preparation/rasterization stays on the UI executor.
///
/// @note Fields are interpreted according to kind. Text/font are used only by text
/// commands; cornerRadius applies to rectangles/clips; popClip ignores geometry.
/// Rich box geometry and shadow payloads are projection-only; Drawing::validate rejects
/// authored payloads. CPUCanvas has no shadow-authoring API; use GPUDrawing instead.
/// The value layout is not a persistent serialization format or stable plugin ABI.
struct PaintCommand {
	PaintKind kind = PaintKind::fillRect; ///< Operation the scene renderer must execute.
	Rect bounds{}; ///< Local bounds in Drawing; document-relative bounds in a published scene.
	Color color{}; ///< Fill, outline, line or text color, including opacity.
	Points cornerRadius{}; ///< Rectangle/clip rounding radius in logical points.
	Points lineWidth{1};   ///< Outline or segment thickness in logical points.
	std::string text{};	   ///< Owned display text; used only for PaintKind::text.
	Font font{};		   ///< System-font request; used only for PaintKind::text.
	std::optional<detail::BoxGeometry>
		boxGeometry{};							 ///< Opt-in physical fill/border/clip contours.
	std::optional<detail::ShadowPaint> shadow{}; ///< Owning immutable built-in GPU operation.
	[[nodiscard]] auto operator==(const PaintCommand &) const -> bool;
};

/// @brief Builds decorative paint commands for a CPUCanvas component.
///
/// Record geometry in CPUCanvas-local logical coordinates, then pass the value as
/// CPUCanvas::drawing. Mounting copies the commands into the retained occurrence;
/// changing this original builder afterward does not mutate the mounted canvas.
/// Supply an accessible equivalent when the picture carries meaningful content.
struct Drawing {
	std::vector<PaintCommand>
		commands; ///< Ordered commands; later commands paint above earlier ones.

	/// @brief Validates the complete authored stream, including direct command edits.
	void validate() const;
	[[nodiscard]] auto operator==(const Drawing &) const -> bool = default;
	void fillRect(Rect bounds, Color color, Points cornerRadius = Points{});
	void
	strokeRect(Rect bounds, Color color, Points width = Points{1}, Points cornerRadius = Points{});

	void fill(Rect bounds, Color color, Points cornerRadius = Points{}) {
		fillRect(bounds, color, cornerRadius);
	}

	void
	stroke(Rect bounds, Color color, Points width = Points{1}, Points cornerRadius = Points{}) {
		strokeRect(bounds, color, width, cornerRadius);
	}
};

/// @brief Owns a complete ordered paint projection of one committed document revision.
///
/// Document materializes this lazily from a settled immutable commit. TestHost exposes it
/// for deterministic paint assertions; native hosts consume the shared internal tree
/// without materializing this compatibility display list. Share as shared_ptr<const SceneSnapshot>
/// so in-flight rendering retains the exact scene even if the UI publishes a newer revision.
/// @note A scene revision identifies logical publication, not GPU completion or
/// confirmed display presentation. Those require separate native observations.
struct SceneSnapshot {
	std::uint64_t revision = 0; ///< Monotonic document-local publication revision.
	Size size{};				///< Logical viewport extent used to arrange and paint this scene.
	Color background = Color::white();	///< Complete viewport clear color before commands paint.
	std::vector<PaintCommand> commands; ///< Document-coordinate display list in painter's order.
};
} // namespace stana::rendering
