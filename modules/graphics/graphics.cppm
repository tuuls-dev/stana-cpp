module;

#include <cstdint>
#include <string>
#include <vector>

export module stana.graphics;
export import :presentation;
export import stana.primitives;

export namespace stana {

/// @brief Defines the rendering primitive stored in a paint command.
enum class PaintKind : std::uint8_t {
	fillRect,	///< Fills a rectangle, optionally with rounded corners.
	strokeRect, ///< Outlines a rectangle using lineWidth and optional rounded corners.
	text,		///< Draws text using the supplied font and foreground color.
	line,		///< Draws a segment from bounds.origin to origin + size.
	pushClip,	///< Saves graphics state and intersects it with the supplied clip region.
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
/// The value layout is not a persistent serialization format or stable plugin ABI.
struct PaintCommand {
	PaintKind kind = PaintKind::fillRect; ///< Operation the scene renderer must execute.
	Rect bounds{}; ///< Local bounds in Drawing; document-relative bounds in a published scene.
	Color color{}; ///< Fill, outline, line or text color, including opacity.
	double cornerRadius = 0; ///< Rectangle/clip rounding radius in logical points.
	double lineWidth = 1;	 ///< Outline or segment thickness in logical points.
	std::string text{};		 ///< Owned display text; used only for PaintKind::text.
	Font font{};			 ///< System-font request; used only for PaintKind::text.
};

/// @brief Builds decorative paint commands for a Canvas component.
///
/// Record geometry in Canvas-local logical coordinates, then pass the value as
/// Canvas::drawing. Mounting copies the commands into the retained occurrence;
/// changing this original builder afterward does not mutate the mounted canvas.
/// Supply an accessible equivalent when the picture carries meaningful content.
struct Drawing {
	std::vector<PaintCommand>
		commands; ///< Ordered commands; later commands paint above earlier ones.

	/// @brief Appends a filled, optionally rounded rectangle in local coordinates.
	/// @param bounds Logical rectangle relative to the Canvas origin.
	/// @param color Fill color and opacity.
	/// @param cornerRadius Nonnegative rounding radius in logical points.
	void fill(Rect bounds, Color color, double cornerRadius = 0) {
		commands.push_back(
			{.kind = PaintKind::fillRect,
				.bounds = bounds,
				.color = color,
				.cornerRadius = cornerRadius}
		);
	}

	/// @brief Appends an outlined, optionally rounded rectangle in local coordinates.
	/// @param width Stroke thickness in logical points, independent of device scale.
	void stroke(Rect bounds, Color color, double width = 1, double cornerRadius = 0) {
		commands.push_back(
			{.kind = PaintKind::strokeRect,
				.bounds = bounds,
				.color = color,
				.cornerRadius = cornerRadius,
				.lineWidth = width}
		);
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
} // namespace stana
