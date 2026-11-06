module;

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module stana:symbol_compiler;

import :symbol_asset;
import :symbol_compiler_detail;

export namespace stana::symbol {

/// @brief UTF-8 SVG literal used as the template argument of compile().
///
/// The characters live in the template argument, so the compiled geometry can be
/// published into static storage and returned from a consteval function.
template <std::size_t N> struct Artwork {
	std::array<char, N> text{};

	template <class Literal>
	constexpr Artwork(const Literal &literal) : text(std::to_array(literal)) {}

	/// @brief The artwork bytes, without the terminating null of the string literal.
	[[nodiscard]] constexpr auto view() const -> std::string_view {
		return {text.data(), N == 0 ? 0 : N - 1};
	}
};

template <class Literal> Artwork(const Literal &) -> Artwork<std::extent_v<Literal>>;

namespace detail {

inline constexpr std::size_t headerWords = 9;
inline constexpr std::size_t nodeStride = 32;
inline constexpr std::size_t commandStride = 8;
inline constexpr std::size_t headerNodes = 0;
inline constexpr std::size_t headerCommands = 1;
inline constexpr std::size_t headerRoots = 2;
inline constexpr std::size_t headerStroke = 3;
inline constexpr std::size_t headerBytes = 4;
inline constexpr std::size_t headerOriginX = 5;
inline constexpr std::size_t headerOriginY = 6;
inline constexpr std::size_t headerWidth = 7;
inline constexpr std::size_t headerHeight = 8;
inline constexpr std::size_t nodeKind = 0;
inline constexpr std::size_t nodeDescendants = 1;
inline constexpr std::size_t nodeChildren = 2;
inline constexpr std::size_t nodeCommandIndex = 3;
inline constexpr std::size_t nodeCommandCount = 4;
inline constexpr std::size_t nodeFill = 5;
inline constexpr std::size_t nodeStroke = 6;
inline constexpr std::size_t nodeFillRule = 7;
inline constexpr std::size_t nodeCap = 8;
inline constexpr std::size_t nodeJoin = 9;
inline constexpr std::size_t nodeA = 10;
inline constexpr std::size_t nodeOpacity = 16;
inline constexpr std::size_t nodeFillRed = 17;
inline constexpr std::size_t nodeStrokeRed = 21;
inline constexpr std::size_t nodeFillOpacity = 25;
inline constexpr std::size_t nodeStrokeOpacity = 26;
inline constexpr std::size_t nodeStrokeWidth = 27;
inline constexpr std::size_t nodeMiter = 28;
inline constexpr std::size_t commandKind = 0;
inline constexpr std::size_t commandValue = 1;
inline constexpr std::size_t offset5 = 5;
inline constexpr std::size_t offset6 = 6;
inline constexpr std::uint64_t kindGroup = 1;
inline constexpr std::uint64_t kindPath = 2;
inline constexpr std::uint64_t paintNone = 0;
inline constexpr std::uint64_t paintCurrent = 1;
inline constexpr std::uint64_t paintRgba = 2;
inline constexpr std::uint64_t commandMove = 1;
inline constexpr std::uint64_t commandLine = 2;
inline constexpr std::uint64_t commandQuadratic = 3;
inline constexpr std::uint64_t commandCubic = 4;
inline constexpr std::uint64_t commandArc = 5;
inline constexpr std::uint64_t commandClose = 6;
inline constexpr std::size_t transformWords = 6;
inline constexpr std::size_t colorWords = 4;

struct Census {
	std::size_t roots = 0;
	std::size_t nodes = 0;
	std::size_t commands = 0;
};

template <std::size_t Words> struct Blob {
	std::array<std::uint64_t, Words> words{};
};

template <std::size_t Words>
constexpr void writeDouble(Blob<Words> &blob, std::size_t index, double value) {
	blob.words.at(index) = std::bit_cast<std::uint64_t>(value);
}

template <std::size_t Words>
[[nodiscard]] constexpr auto readDouble(const Blob<Words> &blob, std::size_t index) -> double {
	return std::bit_cast<double>(blob.words.at(index));
}

[[nodiscard]] constexpr auto paintKind(const Paint &paint) -> std::uint64_t {
	if (std::holds_alternative<NoPaint>(paint)) {
		return paintNone;
	}
	if (std::holds_alternative<CurrentColor>(paint)) {
		return paintCurrent;
	}
	return paintRgba;
}

template <std::size_t Words>
constexpr void writeColor(Blob<Words> &blob, std::size_t index, const Paint &paint) {
	if (const auto *color = std::get_if<RgbaColor>(&paint)) {
		writeDouble(blob, index, color->red);
		writeDouble(blob, index + 1, color->green);
		writeDouble(blob, index + 2, color->blue);
		writeDouble(blob, index + 3, color->alpha);
	}
}

template <std::size_t Words>
constexpr void writeTransform(Blob<Words> &blob, std::size_t index, AffineTransform transform) {
	writeDouble(blob, index, transform.a);
	writeDouble(blob, index + 1, transform.b);
	writeDouble(blob, index + 2, transform.c);
	writeDouble(blob, index + 3, transform.d);
	writeDouble(blob, index + 4, transform.tx);
	writeDouble(blob, index + offset5, transform.ty);
}

[[nodiscard]] constexpr auto commandKindOf(const PathCommand &command) -> std::uint64_t {
	if (std::holds_alternative<MoveTo>(command)) {
		return commandMove;
	}
	if (std::holds_alternative<LineTo>(command)) {
		return commandLine;
	}
	if (std::holds_alternative<QuadraticTo>(command)) {
		return commandQuadratic;
	}
	if (std::holds_alternative<CubicTo>(command)) {
		return commandCubic;
	}
	if (std::holds_alternative<ArcTo>(command)) {
		return commandArc;
	}
	return commandClose;
}

template <std::size_t Words>
constexpr void writeCommand(Blob<Words> &blob, std::size_t slot, const PathCommand &command) {
	blob.words.at(slot + commandKind) = commandKindOf(command);
	if (const auto *move = std::get_if<MoveTo>(&command)) {
		writeDouble(blob, slot + commandValue, move->x);
		writeDouble(blob, slot + commandValue + 1, move->y);
	} else if (const auto *line = std::get_if<LineTo>(&command)) {
		writeDouble(blob, slot + commandValue, line->x);
		writeDouble(blob, slot + commandValue + 1, line->y);
	} else if (const auto *quadratic = std::get_if<QuadraticTo>(&command)) {
		writeDouble(blob, slot + commandValue, quadratic->controlX);
		writeDouble(blob, slot + commandValue + 1, quadratic->controlY);
		writeDouble(blob, slot + commandValue + 2, quadratic->x);
		writeDouble(blob, slot + commandValue + 3, quadratic->y);
	} else if (const auto *cubic = std::get_if<CubicTo>(&command)) {
		writeDouble(blob, slot + commandValue, cubic->control1X);
		writeDouble(blob, slot + commandValue + 1, cubic->control1Y);
		writeDouble(blob, slot + commandValue + 2, cubic->control2X);
		writeDouble(blob, slot + commandValue + 3, cubic->control2Y);
		writeDouble(blob, slot + commandValue + 4, cubic->x);
		writeDouble(blob, slot + commandValue + offset5, cubic->y);
	} else if (const auto *arc = std::get_if<ArcTo>(&command)) {
		writeDouble(blob, slot + commandValue, arc->centerX);
		writeDouble(blob, slot + commandValue + 1, arc->centerY);
		writeDouble(blob, slot + commandValue + 2, arc->radiusX);
		writeDouble(blob, slot + commandValue + 3, arc->radiusY);
		writeDouble(blob, slot + commandValue + 4, arc->rotation);
		writeDouble(blob, slot + commandValue + offset5, arc->startAngle);
		writeDouble(blob, slot + commandValue + offset6, arc->sweepAngle);
	}
}

constexpr void tally(const std::vector<Element> &elements, Census &census) {
	for (const Element &element : elements) {
		++census.nodes;
		if (const auto *group = std::get_if<Element::Group>(&element.value)) {
			tally(group->children, census);
		} else {
			census.commands += std::get<Element::Path>(element.value).commands.size();
		}
	}
}

template <Artwork artwork, bool mirrors> consteval auto measure() -> Census {
	const Asset asset = compileSource(artwork.view(), mirrors);
	Census census;
	census.roots = asset.elements().size();
	tally(asset.elements(), census);
	return census;
}

template <std::size_t Words>
constexpr void writeElements(
	Blob<Words> &blob,
	const std::vector<Element> &elements,
	std::size_t nodeCount,
	std::size_t &nodeCursor,
	std::size_t &commandCursor
) {
	for (const Element &element : elements) {
		const std::size_t index = nodeCursor;
		++nodeCursor;
		const std::size_t slot = headerWords + index * nodeStride;
		if (const auto *group = std::get_if<Element::Group>(&element.value)) {
			blob.words.at(slot + nodeKind) = kindGroup;
			writeTransform(blob, slot + nodeA, group->transform);
			writeDouble(blob, slot + nodeOpacity, group->opacity);
			const std::size_t childCursor = nodeCursor;
			writeElements(blob, group->children, nodeCount, nodeCursor, commandCursor);
			blob.words.at(slot + nodeDescendants) =
				static_cast<std::uint64_t>(nodeCursor - childCursor);
			blob.words.at(slot + nodeChildren) = static_cast<std::uint64_t>(group->children.size());
		} else {
			const auto &path = std::get<Element::Path>(element.value);
			blob.words.at(slot + nodeKind) = kindPath;
			writeTransform(blob, slot + nodeA, path.transform);
			writeDouble(blob, slot + nodeOpacity, path.opacity);
			blob.words.at(slot + nodeFill) = paintKind(path.fill);
			blob.words.at(slot + nodeStroke) = paintKind(path.stroke);
			blob.words.at(slot + nodeFillRule) = static_cast<std::uint64_t>(path.fillRule);
			blob.words.at(slot + nodeCap) = static_cast<std::uint64_t>(path.lineCap);
			blob.words.at(slot + nodeJoin) = static_cast<std::uint64_t>(path.lineJoin);
			writeColor(blob, slot + nodeFillRed, path.fill);
			writeColor(blob, slot + nodeStrokeRed, path.stroke);
			writeDouble(blob, slot + nodeFillOpacity, path.fillOpacity);
			writeDouble(blob, slot + nodeStrokeOpacity, path.strokeOpacity);
			writeDouble(blob, slot + nodeStrokeWidth, path.strokeWidth);
			writeDouble(blob, slot + nodeMiter, path.miterLimit);
			blob.words.at(slot + nodeCommandIndex) = static_cast<std::uint64_t>(commandCursor);
			blob.words.at(slot + nodeCommandCount) =
				static_cast<std::uint64_t>(path.commands.size());
			for (const PathCommand &command : path.commands) {
				const std::size_t commandSlot =
					headerWords + nodeCount * nodeStride + commandCursor * commandStride;
				writeCommand(blob, commandSlot, command);
				++commandCursor;
			}
		}
	}
}

template <Artwork artwork, bool mirrors, std::size_t Words>
consteval auto encode(Census census) -> Blob<Words> {
	const Asset asset = compileSource(artwork.view(), mirrors);
	Blob<Words> blob;
	blob.words.at(headerNodes) = static_cast<std::uint64_t>(census.nodes);
	blob.words.at(headerCommands) = static_cast<std::uint64_t>(census.commands);
	blob.words.at(headerRoots) = static_cast<std::uint64_t>(census.roots);
	blob.words.at(headerStroke) = asset.hasStroke() ? 1 : 0;
	blob.words.at(headerBytes) = static_cast<std::uint64_t>(asset.estimatedBytes());
	writeDouble(blob, headerOriginX, asset.viewBox().origin.x);
	writeDouble(blob, headerOriginY, asset.viewBox().origin.y);
	writeDouble(blob, headerWidth, asset.viewBox().size.width);
	writeDouble(blob, headerHeight, asset.viewBox().size.height);
	std::size_t nodeCursor = 0;
	std::size_t commandCursor = 0;
	writeElements(blob, asset.elements(), census.nodes, nodeCursor, commandCursor);
	if (nodeCursor != census.nodes || commandCursor != census.commands) {
		throw std::invalid_argument("Stana: symbol geometry encoding did not match its census");
	}
	return blob;
}

template <std::size_t Words>
[[nodiscard]] auto readTransform(const Blob<Words> &blob, std::size_t index) -> AffineTransform {
	return {
		.a = readDouble(blob, index),
		.b = readDouble(blob, index + 1),
		.c = readDouble(blob, index + 2),
		.d = readDouble(blob, index + 3),
		.tx = readDouble(blob, index + 4),
		.ty = readDouble(blob, index + offset5)
	};
}

struct PaintAt {
	std::uint64_t kind = paintNone;
	std::size_t color = 0;
};

template <std::size_t Words>
[[nodiscard]] auto readPaint(const Blob<Words> &blob, PaintAt at) -> Paint {
	if (at.kind == paintNone) {
		return NoPaint{};
	}
	if (at.kind == paintCurrent) {
		return CurrentColor{};
	}
	return RgbaColor{
		.red = readDouble(blob, at.color),
		.green = readDouble(blob, at.color + 1),
		.blue = readDouble(blob, at.color + 2),
		.alpha = readDouble(blob, at.color + 3)
	};
}

template <std::size_t Words>
[[nodiscard]] auto readCommand(const Blob<Words> &blob, std::size_t slot) -> PathCommand {
	const std::uint64_t kind = blob.words.at(slot + commandKind);
	if (kind == commandMove) {
		return MoveTo{
			.x = readDouble(blob, slot + commandValue),
			.y = readDouble(blob, slot + commandValue + 1)
		};
	}
	if (kind == commandLine) {
		return LineTo{
			.x = readDouble(blob, slot + commandValue),
			.y = readDouble(blob, slot + commandValue + 1)
		};
	}
	if (kind == commandQuadratic) {
		return QuadraticTo{
			.controlX = readDouble(blob, slot + commandValue),
			.controlY = readDouble(blob, slot + commandValue + 1),
			.x = readDouble(blob, slot + commandValue + 2),
			.y = readDouble(blob, slot + commandValue + 3)
		};
	}
	if (kind == commandCubic) {
		return CubicTo{
			.control1X = readDouble(blob, slot + commandValue),
			.control1Y = readDouble(blob, slot + commandValue + 1),
			.control2X = readDouble(blob, slot + commandValue + 2),
			.control2Y = readDouble(blob, slot + commandValue + 3),
			.x = readDouble(blob, slot + commandValue + 4),
			.y = readDouble(blob, slot + commandValue + offset5)
		};
	}
	if (kind == commandArc) {
		return ArcTo{
			.centerX = readDouble(blob, slot + commandValue),
			.centerY = readDouble(blob, slot + commandValue + 1),
			.radiusX = readDouble(blob, slot + commandValue + 2),
			.radiusY = readDouble(blob, slot + commandValue + 3),
			.rotation = readDouble(blob, slot + commandValue + 4),
			.startAngle = readDouble(blob, slot + commandValue + offset5),
			.sweepAngle = readDouble(blob, slot + commandValue + offset6)
		};
	}
	return ClosePath{};
}

template <std::size_t Words>
[[nodiscard]] auto
readElements(const Blob<Words> &blob, std::size_t nodeCount, std::size_t &cursor, std::size_t count)
	-> std::vector<Element> {
	std::vector<Element> elements;
	for (std::size_t index = 0; index < count; ++index) {
		const std::size_t slot = headerWords + cursor * nodeStride;
		++cursor;
		if (blob.words.at(slot + nodeKind) == kindGroup) {
			Element::Group group;
			group.transform = readTransform(blob, slot + nodeA);
			group.opacity = readDouble(blob, slot + nodeOpacity);
			const auto children = static_cast<std::size_t>(blob.words.at(slot + nodeChildren));
			group.children = readElements(blob, nodeCount, cursor, children);
			elements.push_back(Element{.value = std::move(group)});
		} else {
			Element::Path path;
			path.transform = readTransform(blob, slot + nodeA);
			path.opacity = readDouble(blob, slot + nodeOpacity);
			path.fill = readPaint(
				blob,
				{.kind = blob.words.at(slot + nodeFill), .color = slot + nodeFillRed}
			);
			path.stroke = readPaint(
				blob,
				{.kind = blob.words.at(slot + nodeStroke), .color = slot + nodeStrokeRed}
			);
			path.fillRule = static_cast<FillRule>(blob.words.at(slot + nodeFillRule));
			path.lineCap = static_cast<LineCap>(blob.words.at(slot + nodeCap));
			path.lineJoin = static_cast<LineJoin>(blob.words.at(slot + nodeJoin));
			path.fillOpacity = readDouble(blob, slot + nodeFillOpacity);
			path.strokeOpacity = readDouble(blob, slot + nodeStrokeOpacity);
			path.strokeWidth = readDouble(blob, slot + nodeStrokeWidth);
			path.miterLimit = readDouble(blob, slot + nodeMiter);
			const auto commandIndex =
				static_cast<std::size_t>(blob.words.at(slot + nodeCommandIndex));
			const auto commandCount =
				static_cast<std::size_t>(blob.words.at(slot + nodeCommandCount));
			// vector<PathCommand>::emplace_back is miscompiled by this Clang: the
			// end cursor captured by emplace_back is restored, so each command is
			// constructed and then dropped. Assign into a sized vector instead.
			std::vector<PathCommand> commands(commandCount);
			for (std::size_t command = 0; command < commandCount; ++command) {
				const std::size_t commandSlot =
					headerWords + nodeCount * nodeStride + (commandIndex + command) * commandStride;
				commands.at(command) = readCommand(blob, commandSlot);
			}
			path.commands = std::move(commands);
			elements.push_back(Element{.value = std::move(path)});
		}
	}
	return elements;
}

template <std::size_t Words>
[[nodiscard]] auto materialize(const Blob<Words> &blob) -> std::vector<Element> {
	std::size_t cursor = 0;
	const auto roots = static_cast<std::size_t>(blob.words.at(headerRoots));
	const auto nodeCount = static_cast<std::size_t>(blob.words.at(headerNodes));
	return readElements(blob, nodeCount, cursor, roots);
}

template <Blob blob> struct Published {
	static constexpr Blob value = blob;

	static auto load() -> const std::vector<Element> & {
		static const std::vector<Element> elements = materialize(value);
		return elements;
	}
};

template <Blob blob, bool mirrors> consteval auto publish() -> Asset {
	ViewHeader header;
	header.viewBox = {
		.origin = {.x = readDouble(blob, headerOriginX), .y = readDouble(blob, headerOriginY)},
		.size = {.width = readDouble(blob, headerWidth), .height = readDouble(blob, headerHeight)}
	};
	header.estimatedBytes = static_cast<std::size_t>(blob.words.at(headerBytes));
	header.mirrorsInRTL = mirrors;
	header.hasStroke = blob.words.at(headerStroke) != 0;
	return adoptView(header, &Published<blob>::load);
}

} // namespace detail

/// @brief Compiles an SVG document into a symbol asset during constant evaluation.
///
/// Pass the document as a template argument: `compile<R"(<svg .../>)">()`.
/// Programmer errors throw std::invalid_argument. The message names invalidXML,
/// unsupportedFeature, invalidValue, or budgetExceeded and includes a 1-based line and column.
/// @param mirrorsInRTL Mirror fitted artwork when the layout direction is right to left.
template <Artwork artwork, bool mirrorsInRTL = false>
[[nodiscard]] consteval auto compile() -> Asset {
	constexpr auto census = detail::measure<artwork, mirrorsInRTL>();
	constexpr std::size_t words = detail::headerWords + census.nodes * detail::nodeStride +
		census.commands * detail::commandStride;
	constexpr auto blob = detail::encode<artwork, mirrorsInRTL, words>(census);
	return detail::publish<blob, mirrorsInRTL>();
}

} // namespace stana::symbol
