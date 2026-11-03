#ifndef STANA_NATIVE_CONTRACTS_HPP
#define STANA_NATIVE_CONTRACTS_HPP

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/// @brief Private module-free transport shared by C++ engine adapters and native code.
/// These types are not installed as public headers and carry no borrowed Node data.
namespace stana::native {

/// @brief Native adapter's paint opcode; converted explicitly from portable PaintKind.
enum class PaintOperation : std::uint8_t {
	fill,	  ///< Filled rounded rectangle.
	stroke,	  ///< Outlined rounded rectangle.
	text,	  ///< Core Text label.
	line,	  ///< Line from rectangle origin to its opposite corner.
	pushClip, ///< Save state and intersect rounded clipping bounds.
	popClip	  ///< Restore the previous saved state.
};

/// @brief Owning typed command crossing the module/Objective-C++ boundary.
/// The C++ application adapter produces this value; the native raster pass reads it.
struct Command {
	PaintOperation operation = PaintOperation::fill; ///< Paint opcode.
	double x = 0;		   ///< Document-relative left coordinate in logical points.
	double y = 0;		   ///< Document-relative top coordinate in logical points.
	double width = 0;	   ///< Logical horizontal extent.
	double height = 0;	   ///< Logical vertical extent.
	double red = 0;		   ///< Red component in encoded sRGB.
	double green = 0;	   ///< Green component in encoded sRGB.
	double blue = 0;	   ///< Blue component in encoded sRGB.
	double alpha = 1;	   ///< Opacity applied by scene projection.
	double radius = 0;	   ///< Rounding radius for rectangles/clips.
	double lineWidth = 1;  ///< Outline thickness in logical points.
	std::string text{};	   ///< Owned UTF-8 label, including embedded NUL bytes.
	double fontSize = 0;   ///< Resolved, scaled font size in logical points.
	double fontWeight = 0; ///< Native system-font weight request.
};

/// @brief Complete immutable-by-publication raster input owned through GPU submission.
struct Scene {
	std::uint64_t revision = 0;		 ///< Document scene revision, not a presentation timestamp.
	double red = 1;					 ///< Viewport clear red component.
	double green = 1;				 ///< Viewport clear green component.
	double blue = 1;				 ///< Viewport clear blue component.
	std::vector<Command> commands{}; ///< Painter-ordered command values.
};
/// @brief Native keyboard actions understood by the retained document adapter.
enum class KeyAction : std::uint8_t {
	tabForward,	 ///< Move focus to the next eligible occurrence.
	tabBackward, ///< Move focus to the previous eligible occurrence.
	activate	 ///< Invoke the currently focused control.
};

/// @brief Module-free copy of an exact portable presentation identity, never a scene ID alone.
struct FrameIdentity {
	std::uint64_t epoch = 0;		 ///< Changes on surface geometry or drawable-queue replacement.
	std::uint64_t sequence = 0;		 ///< Never-reused submission identity, including retries.
	std::uint64_t sceneRevision = 0; ///< Owning document's submitted scene revision.
};

/// @brief Callbacks installed by one C++ window session and invoked on the main thread.
/// Native hosts own this value until closure. Callback captures must not borrow
/// component descriptions, because descriptions are already destroyed after mounting.
struct Window {
	std::string title{}; ///< Initial UTF-8 native title.
	double width = 0;	 ///< Initial content width in logical points.
	double height = 0;	 ///< Initial content height in logical points.
	std::function<std::shared_ptr<const Scene>()> scene{}; ///< Produces an owning current scene.
	std::function<void(double, double)> resize{};		   ///< Reflows the retained viewport.
	std::function<void(bool isDark)>
		appearance{}; ///< Reports whether the current system appearance is dark.
	std::function<void(double, double)> pointerDown{}; ///< Dispatches a logical-coordinate press.
	std::function<void(double, double)> pointerUp{};   ///< Dispatches logical-coordinate release.
	std::function<void(double, double, double)> scroll{}; ///< Dispatches vertical scrolling.
	std::function<void(KeyAction)> key{}; ///< Invokes portable focus/activation behavior.
	std::function<std::expected<FrameIdentity, std::string>(std::uint64_t, std::uint64_t)>
		beginPresentation{}; ///< Registers exact evidence identity before GPU submission.
	std::function<bool(FrameIdentity, bool)>
		gpuCompleted{}; ///< Returns true only when this observation newly confirms geometry.
	std::function<bool(FrameIdentity, double)>
		drawablePresented{}; ///< Joins timestamp evidence through the same tracker as tests.
	std::function<void(FrameIdentity)>
		abandonPresentation{}; ///< Retires expired work without inventing presentation evidence.
	std::function<void(std::uint64_t)>
		resetPresentation{}; ///< Invalidates callbacks from previous native attachments/surfaces.
	std::function<void()> close{}; ///< Disposes the document before native state is released.
	std::function<void(std::function<void()>)>
		ready{}; ///< Connects document publication to repaint demand.
};

/// @brief Last-known native progress for one active host; no mutable native objects escape.
struct WindowStatistics {
	std::uint64_t surfaceEpoch = 0;		 ///< Generation of the current surface geometry.
	std::uint64_t submittedRevision = 0; ///< Last scene sent to a GPU command buffer.
	std::uint64_t confirmedRevision =
		0;				   ///< Last scene with joined GPU and positive timestamp evidence.
	std::string failure{}; ///< Terminal native failure, or empty when none was recorded.
};

/// @brief Returns UI-thread snapshots of active native hosts without creating an application.
[[nodiscard]] auto diagnostics() -> std::vector<WindowStatistics>;

/// @brief Runs native windows until the final window closes or stop() is called.
/// @return Exit status, or a diagnostic if required native rendering is unavailable.
[[nodiscard]] auto run(std::vector<Window> windows) -> std::expected<int, std::string>;
/// @brief Requests event-loop shutdown without creating NSApplication on its own.
void stop();
/// @brief Schedules an owning callback on the UI queue after a nonnegative delay.
void postAfter(std::chrono::milliseconds delay, std::function<void()> callback);

/// @brief Native system-font parameters, grouped to prevent swapping size and weight.
struct FontRequest {
	double size = 0;   ///< Logical point size after environment scaling.
	double weight = 0; ///< Native system font weight, zero for regular.
};

/// @brief Measures UTF-8 text with the same Core Text font contract used for drawing.
/// @return Logical width and line height, constrained by the requested maximum width.
[[nodiscard]] auto measureText(const std::string &text, FontRequest font, double maximumWidth)
	-> std::pair<double, double>;
} // namespace stana::native

#endif // STANA_NATIVE_CONTRACTS_HPP
