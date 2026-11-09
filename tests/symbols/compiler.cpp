#include <array>
#include <cmath>
#include <compare>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

import stana;

namespace {
constexpr double tolerance = 1e-9;
constexpr double arcCenter = 5;
constexpr double fullTurn = 2 * std::numbers::pi;
constexpr double strokeWidth = 2;
constexpr double viewSide = 24;
constexpr double moveX = 5;
constexpr double moveY = 12;
constexpr double lineEnd = 19;
constexpr double verticalX = 12;
constexpr double verticalStart = 5;
constexpr double colorByte = 255;
constexpr double greenByte = 128;
constexpr double shortNibble = 10;
constexpr double nibbleB = 11;
constexpr double nibbleC = 12;
constexpr double nibbleScale = 16;
constexpr double translatedX = 1;
constexpr double translatedY = 2;
constexpr double boxSide = 2;
constexpr double unit = 1;
constexpr std::array<double, 2> reflectedMove{1, 2};
constexpr std::array<double, 6> reflectedCubic{2, 4, 4, 6, 6, 8};
constexpr std::array<double, 6> reflectedSmooth{8, 10, 7, 10, 9, 12};
constexpr std::array<double, 4> reflectedQuad{10, 14, 12, 16};
constexpr std::array<double, 4> reflectedSmoothQuad{14, 18, 17, 22};
constexpr std::size_t reflectedCount = 5;
constexpr std::size_t cubicEndY = 5;
constexpr std::size_t emptyShapeCount = 3;
constexpr std::size_t circleAndGroup = 2;
constexpr std::array<double, 2> degenerateLine{2, 3};
constexpr double hexRed = (shortNibble * nibbleScale + shortNibble) / colorByte;
constexpr double hexGreen = (nibbleB * nibbleScale + nibbleB) / colorByte;
constexpr double hexBlue = (nibbleC * nibbleScale + nibbleC) / colorByte;
constexpr double greenChannel = greenByte / colorByte;
constexpr double rgbRed = 1 / colorByte;
constexpr double rgbGreen = 2 / colorByte;

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] auto same(double left, double right) -> bool {
	return std::is_eq(left <=> right) || std::abs(left - right) < tolerance;
}

[[nodiscard]] auto pathAt(const stana::symbol::Asset &asset, std::size_t index)
	-> const stana::symbol::Element::Path & {
	return std::get<stana::symbol::Element::Path>(asset.elements().at(index).value);
}

void compiledIconMatchesTheAuthoredPath() {
	constexpr auto icon = stana::symbol::compile<R"(
<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
  <path d="M5 12h14M12 5v14"/>
</svg>
)">();
	require(icon.valid(), "icon compiles");
	require(icon.hasStroke(), "stroke retained");
	require(
		same(icon.viewBox().size.width.value(), viewSide) &&
			same(icon.viewBox().size.height.value(), viewSide),
		"viewBox"
	);
	require(icon.elements().size() == 1, "one path");
	const auto &path = std::get<stana::symbol::Element::Path>(icon.elements().at(0).value);
	require(path.commands.size() == 4, "two strokes");
	require(std::holds_alternative<stana::symbol::NoPaint>(path.fill), "fill none");
	require(std::holds_alternative<stana::symbol::CurrentColor>(path.stroke), "currentColor");
	require(same(path.strokeWidth.value(), strokeWidth), "stroke width");
	const auto &move = std::get<stana::symbol::MoveTo>(path.commands.at(0));
	const auto &line = std::get<stana::symbol::LineTo>(path.commands.at(1));
	require(same(move.x.value(), moveX) && same(move.y.value(), moveY), "first move");
	require(same(line.x.value(), lineEnd) && same(line.y.value(), moveY), "first line");
	const auto &down = std::get<stana::symbol::MoveTo>(path.commands.at(2));
	const auto &up = std::get<stana::symbol::LineTo>(path.commands.at(3));
	require(same(down.x.value(), verticalX) && same(down.y.value(), verticalStart), "second move");
	require(same(up.x.value(), verticalX) && same(up.y.value(), lineEnd), "second line");
}

void ignoredMetadataMatchesTheNormalizedPath() {
	constexpr auto plain = stana::symbol::compile<R"(
<svg viewBox="-12 -12 24 24" fill="none" stroke="currentColor">
  <path d="M0 0h4v5z"/>
</svg>
)">();
	constexpr auto noisy = stana::symbol::compile<R"(
<svg viewBox="-12 -12 24 24" fill="none" stroke="currentColor">
  <!--ignored--><title>Ignored &amp; title</title>
  <path id="one" aria-label="Not a name" data-test="yes" d="M-0 0L4 0L4 5Z"/>
</svg>
)">();
	require(plain == noisy, "metadata does not change geometry");
	const auto &move = std::get<stana::symbol::MoveTo>(pathAt(noisy, 0).commands.at(0));
	require(
		!std::signbit(move.x.value()) && !std::signbit(move.y.value()),
		"negative zero cleared"
	);
	require(same(move.x.value(), 0) && same(move.y.value(), 0), "move origin");
}

void reflectedControlsMatchSwift() {
	constexpr auto reflected = stana::symbol::compile<R"(
<svg viewBox="0 0 24 24"><path d="M1 2c1 2 3 4 5 6s1 2 3 4q1 2 3 4t5 6"/></svg>
)">();
	const auto &commands = pathAt(reflected, 0).commands;
	require(commands.size() == reflectedCount, "reflected command count");
	const auto &move = std::get<stana::symbol::MoveTo>(commands.at(0));
	const auto &cubic = std::get<stana::symbol::CubicTo>(commands.at(1));
	const auto &smooth = std::get<stana::symbol::CubicTo>(commands.at(2));
	const auto &quad = std::get<stana::symbol::QuadraticTo>(commands.at(3));
	const auto &smoothQuad = std::get<stana::symbol::QuadraticTo>(commands.at(4));
	require(
		same(move.x.value(), reflectedMove.at(0)) && same(move.y.value(), reflectedMove.at(1)),
		"reflected move"
	);
	require(
		same(cubic.control1X.value(), reflectedCubic.at(0)) &&
			same(cubic.control1Y.value(), reflectedCubic.at(1)) &&
			same(cubic.control2X.value(), reflectedCubic.at(2)) &&
			same(cubic.control2Y.value(), reflectedCubic.at(3)) &&
			same(cubic.x.value(), reflectedCubic.at(4)) &&
			same(cubic.y.value(), reflectedCubic.at(cubicEndY)),
		"first cubic"
	);
	require(
		same(smooth.control1X.value(), reflectedSmooth.at(0)) &&
			same(smooth.control1Y.value(), reflectedSmooth.at(1)) &&
			same(smooth.control2X.value(), reflectedSmooth.at(2)) &&
			same(smooth.control2Y.value(), reflectedSmooth.at(3)) &&
			same(smooth.x.value(), reflectedSmooth.at(4)) &&
			same(smooth.y.value(), reflectedSmooth.at(cubicEndY)),
		"smooth cubic"
	);
	require(
		same(quad.controlX.value(), reflectedQuad.at(0)) &&
			same(quad.controlY.value(), reflectedQuad.at(1)) &&
			same(quad.x.value(), reflectedQuad.at(2)) && same(quad.y.value(), reflectedQuad.at(3)),
		"quadratic"
	);
	require(
		same(smoothQuad.controlX.value(), reflectedSmoothQuad.at(0)) &&
			same(smoothQuad.controlY.value(), reflectedSmoothQuad.at(1)) &&
			same(smoothQuad.x.value(), reflectedSmoothQuad.at(2)) &&
			same(smoothQuad.y.value(), reflectedSmoothQuad.at(3)),
		"smooth quadratic"
	);
}

void degenerateArcBecomesALine() {
	constexpr auto arc = stana::symbol::compile<R"(
<svg viewBox="0 0 24 24"><path d="M0 0A0 1 0 0 1 2 3A1 1 0 1 0 2 3"/></svg>
)">();
	const auto &commands = pathAt(arc, 0).commands;
	require(commands.size() == degenerateLine.size(), "degenerate arc commands");
	const auto &move = std::get<stana::symbol::MoveTo>(commands.at(0));
	const auto &line = std::get<stana::symbol::LineTo>(commands.at(1));
	require(same(move.x.value(), 0) && same(move.y.value(), 0), "degenerate move");
	require(
		same(line.x.value(), degenerateLine.at(0)) && same(line.y.value(), degenerateLine.at(1)),
		"degenerate line"
	);
}

void zeroAreaShapesKeepEmptyPaths() {
	constexpr auto shapes = stana::symbol::compile<R"(
<svg viewBox="0 0 1 1"><rect/><circle/><ellipse/></svg>
)">();
	require(shapes.elements().size() == emptyShapeCount, "three empty shapes");
	require(pathAt(shapes, 0).commands.empty(), "empty rect");
	require(pathAt(shapes, 1).commands.empty(), "empty circle");
	require(pathAt(shapes, 2).commands.empty(), "empty ellipse");
}

void shapesAndTransformsProduceGeometry() {
	constexpr auto artwork = stana::symbol::compile<R"(
<svg viewBox="0 0 24 24">
  <circle cx="2" cy="3" r="4"/>
  <g transform='translate(1 2)'><path d='M0 0L2 3'/></g>
</svg>
)">();
	require(artwork.elements().size() == circleAndGroup, "circle and group");
	require(!pathAt(artwork, 0).commands.empty(), "circle has commands");
	const auto &group = std::get<stana::symbol::Element::Group>(artwork.elements().at(1).value);
	require(
		same(group.transform.tx.value(), translatedX) &&
			same(group.transform.ty.value(), translatedY),
		"translation"
	);
	require(group.children.size() == 1, "grouped path");
}

void paintGrammarKeepsLiteralColors() {
	constexpr auto paints = stana::symbol::compile<R"(
<svg viewBox="0 0 1 1">
  <path d='M0 0L1 1' fill='#abc'/>
  <path d='M0 0L1 1' fill='green'/>
  <path d='M0 0L1 1' fill='rgb(1, 2, 255)' stroke='none'/>
</svg>
)">();
	const auto *hex = std::get_if<stana::symbol::RgbaColor>(&pathAt(paints, 0).fill);
	const auto *green = std::get_if<stana::symbol::RgbaColor>(&pathAt(paints, 1).fill);
	const auto *rgb = std::get_if<stana::symbol::RgbaColor>(&pathAt(paints, 2).fill);
	require(hex != nullptr && green != nullptr && rgb != nullptr, "literal paints");
	require(
		same(hex->red.value(), hexRed) && same(hex->green.value(), hexGreen) &&
			same(hex->blue.value(), hexBlue),
		"hex"
	);
	require(same(green->green.value(), greenChannel) && same(green->red.value(), 0), "green");
	require(
		same(rgb->red.value(), rgbRed) && same(rgb->green.value(), rgbGreen) &&
			same(rgb->blue.value(), unit),
		"rgb"
	);
	require(
		std::holds_alternative<stana::symbol::NoPaint>(pathAt(paints, 2).stroke),
		"stroke none"
	);
}

void prefixedSvgMatchesTheDefaultNamespace() {
	constexpr auto prefixed = stana::symbol::compile<R"(<?xml version="1.0"?>
<s:svg xmlns:s="http://www.w3.org/2000/svg" viewBox="0 0 2 2">
  <s:title>&#65; &#x41; &lt; &amp; &quot; &apos;</s:title>
  <s:path d="M0 0L1 1"/>
</s:svg>
)">();
	constexpr auto plain = stana::symbol::compile<R"(
<svg viewBox="0 0 2 2"><path d="M0 0L1 1"/></svg>
)">();
	require(prefixed == plain, "prefixed svg");
	require(same(prefixed.viewBox().size.width.value(), boxSide), "prefixed viewBox");
}

void unicodeAndDecodedPrefixesCompile() {
	constexpr auto unicode = stana::symbol::compile<R"(
<图:svg xmlns:图="http://www.w3.org/2000/svg" viewBox="0 0 1 1"/>
)">();
	constexpr auto decoded = stana::symbol::compile<R"(
<s:svg xmlns:s="http://www.w3.org/2000/sv&#103;" viewBox="0 0 1 1"/>
)">();
	require(unicode.valid() && decoded.valid(), "namespace prefixes");
	require(
		!unicode.mirrorsInRTL() && same(unicode.viewBox().size.width.value(), unit),
		"unicode viewBox"
	);
}

void rightToLeftMirroringIsRetained() {
	constexpr auto mirrored = stana::symbol::compile<R"(<svg viewBox="0 0 1 1"/>)", true>();
	require(mirrored.mirrorsInRTL() && mirrored.valid(), "rtl mirror");
}

void normalizedArcKeepsTheSwiftCenter() {
	constexpr auto arc = stana::symbol::compile<
		R"(<svg viewBox="0 0 24 24"><path d="M0 0A1 1 0 0 1 10 0"/></svg>)">();
	const auto &path = std::get<stana::symbol::Element::Path>(arc.elements().at(0).value);
	const auto &curve = std::get<stana::symbol::ArcTo>(path.commands.at(1));
	require(same(curve.centerX.value(), arcCenter) && same(curve.centerY.value(), 0), "arc center");
	require(
		same(curve.radiusX.value(), arcCenter) && same(curve.radiusY.value(), arcCenter),
		"arc radii"
	);
	require(same(curve.sweepAngle.radians(), fullTurn / 2), "arc sweep");
}

} // namespace

auto main() -> int {
	try {
		compiledIconMatchesTheAuthoredPath();
		normalizedArcKeepsTheSwiftCenter();
		ignoredMetadataMatchesTheNormalizedPath();
		reflectedControlsMatchSwift();
		degenerateArcBecomesALine();
		zeroAreaShapesKeepEmptyPaths();
		shapesAndTransformsProduceGeometry();
		paintGrammarKeepsLiteralColors();
		prefixedSvgMatchesTheDefaultNamespace();
		unicodeAndDecodedPrefixesCompile();
		rightToLeftMirroringIsRetained();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
