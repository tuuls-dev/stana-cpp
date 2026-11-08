module;

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module stana:symbol_compiler_detail;

import :primitives;
import :symbol_asset;

// Compile-time SVG compiler used by stana::symbol::compile.
// Helpers stay in this namespace. An anonymous namespace is TU-local, and a
// constexpr function in a module interface cannot call a TU-local entity.
namespace stana::symbol::detail {

inline constexpr int decimalBase = 10;
inline constexpr int hexLetterOffset = 10;
inline constexpr int shortHexDigits = 3;
inline constexpr int shortHexAlphaDigits = 4;
inline constexpr int longHexDigits = 6;
inline constexpr int longHexAlphaDigits = 8;
inline constexpr int shortHexLimit = 5;
inline constexpr int rgbChannelCount = 3;
inline constexpr int rgbaChannelCount = 4;
inline constexpr int rgbPrefixLength = 4;
inline constexpr int rgbaPrefixLength = 5;
inline constexpr int colorByteMaximum = 255;
inline constexpr int svgGreenByte = 128;
inline constexpr int maximumExponent = 308;
inline constexpr int taylorTerms = 16;
inline constexpr int squareRootIterations = 50;
inline constexpr int transformLimit = 64;
inline constexpr int transformArgumentLimit = 6;
inline constexpr int diagnosticNameLimit = 80;
inline constexpr int xmlnsPrefixLength = 6;
inline constexpr std::size_t sourceBudget = 1048576;
inline constexpr std::size_t bomSize = 3;
inline constexpr std::uint8_t bomFirst = 0xEF;
inline constexpr std::uint8_t bomSecond = 0xBB;
inline constexpr std::uint8_t bomThird = 0xBF;
inline constexpr std::uint8_t utf8Continuation = 0x80;
inline constexpr std::uint8_t utf8ContinuationMask = 0xC0;
inline constexpr std::uint32_t unicodeMaximum = 0x10FFFF;
inline constexpr std::uint32_t surrogateFirst = 0xD800;
inline constexpr std::uint32_t surrogateLast = 0xDFFF;
inline constexpr double numberMagnitude = 1e9;
inline constexpr double significandLimit = 1e18;
inline constexpr double powerCeiling = 1e300;
inline constexpr double infinityProbe = 1.8;
inline constexpr int finiteExponent = 308;
inline constexpr int hugeExponent = 10;
inline constexpr int underflowExponent = -300;
inline constexpr double fullCircleDegrees = 360;
inline constexpr double straightDegrees = 180;
inline constexpr double defaultMiter = 4;
inline constexpr double negativeUnit = -1;
inline constexpr unsigned char asciiLowerBit = 0x20;
inline constexpr std::string_view xmlEllipsis = "…";

struct Failure {
	std::string_view code;
	std::string_view explanation;
};

struct SourceIndex {
	std::size_t value = 0;
	constexpr SourceIndex() noexcept = default;

	constexpr SourceIndex(std::size_t index) noexcept : value(index) {}
};

[[nodiscard]] constexpr auto isZero(double value) noexcept -> bool {
	return std::is_eq(value <=> 0.0);
}

[[nodiscard]] constexpr auto decimalText(std::size_t value) -> std::string {
	if (value == 0) {
		return "0";
	}
	std::string reversed;
	while (value > 0) {
		const auto digit = static_cast<int>(value % static_cast<std::size_t>(decimalBase));
		reversed.push_back(static_cast<char>('0' + digit));
		value /= static_cast<std::size_t>(decimalBase);
	}
	std::string text;
	for (std::size_t cursor = reversed.size(); cursor > 0; --cursor) {
		text.push_back(reversed.at(cursor - 1));
	}
	return text;
}

struct SourceLocation {
	std::size_t line = 1;
	std::size_t column = 1;
};

[[nodiscard]] constexpr auto locate(std::string_view source, std::size_t index) -> SourceLocation {
	SourceLocation location;
	const std::size_t end = index < source.size() ? index : source.size();
	for (std::size_t cursor = 0; cursor < end; ++cursor) {
		if (source.at(cursor) == '\n') {
			++location.line;
			location.column = 1;
		} else {
			++location.column;
		}
	}
	return location;
}

struct Context {
	std::string_view source;

	[[noreturn]] constexpr void fail(Failure failure, std::size_t index) const {
		const SourceLocation location = locate(source, index);
		std::string text = "Stana: ";
		text.append(failure.code);
		text.append(" at ");
		text.append(decimalText(location.line));
		text.append(":");
		text.append(decimalText(location.column));
		text.append(": ");
		text.append(failure.explanation);
		throw std::invalid_argument(text);
	}

	[[nodiscard]] constexpr auto checked(double value, SourceIndex where) const -> double {
		if (!std::isfinite(value)) {
			fail(
				{.code = "invalidValue", .explanation = "Nonfinite or overflowing SVG number"},
				where.value
			);
		}
		if (magnitude(value) > numberMagnitude) {
			fail(
				{.code = "budgetExceeded", .explanation = "SVG numeric magnitude exceeds 1e9"},
				where.value
			);
		}
		return isZero(value) ? 0.0 : value;
	}
};

[[nodiscard]] constexpr auto truncateTowardZero(double value) noexcept -> double {
	constexpr double integerLimit = 9223372036854775807.0;
	if (magnitude(value) >= integerLimit) {
		return value;
	}
	return static_cast<double>(static_cast<long long>(value));
}

[[nodiscard]] constexpr auto truncatingRemainder(double value, double period) noexcept -> double {
	if (isZero(period)) {
		return value;
	}
	return value - truncateTowardZero(value / period) * period;
}

[[nodiscard]] constexpr auto squareRoot(double value) noexcept -> double {
	if (!(value > 0)) {
		return 0;
	}
	if (std::is_eq(value <=> 1.0)) {
		return 1;
	}
	double guess = value > 1 ? value : 1;
	for (int step = 0; step < squareRootIterations; ++step) {
		guess = (guess + value / guess) / 2;
	}
	return guess;
}

[[nodiscard]] constexpr auto wrapSignedPi(double radians) noexcept -> double {
	const double turn = 2 * std::numbers::pi;
	double reduced = radians - truncateTowardZero(radians / turn) * turn;
	if (reduced > std::numbers::pi) {
		reduced -= turn;
	} else if (reduced < -std::numbers::pi) {
		reduced += turn;
	}
	return reduced;
}

[[nodiscard]] constexpr auto sineQuarter(double radians) noexcept -> double {
	double term = radians;
	double sum = radians;
	const double square = radians * radians;
	for (int index = 1; index <= taylorTerms; ++index) {
		const auto factor = static_cast<double>(2 * index * (2 * index + 1));
		term = -term * square / factor;
		sum += term;
	}
	return sum;
}

[[nodiscard]] constexpr auto sineOf(double radians) noexcept -> double {
	if (isZero(radians)) {
		return 0;
	}
	const double wrapped = wrapSignedPi(radians);
	const double sign = wrapped < 0 ? negativeUnit : 1;
	double angle = magnitude(wrapped);
	const double halfTurn = std::numbers::pi / 2;
	if (angle > halfTurn) {
		angle = std::numbers::pi - angle;
	}
	return sign * sineQuarter(angle);
}

[[nodiscard]] constexpr auto cosineOf(double radians) noexcept -> double {
	return sineOf(radians + std::numbers::pi / 2);
}

[[nodiscard]] constexpr auto arcTangentPositive(double value) noexcept -> double {
	if (value > 1) {
		return std::numbers::pi / 2 - arcTangentPositive(1 / value);
	}
	double term = value;
	double sum = value;
	const double square = value * value;
	for (int index = 1; index <= taylorTerms; ++index) {
		term *= -square;
		const auto denominator = static_cast<double>(2 * index + 1);
		sum += term / denominator;
	}
	return sum;
}

[[nodiscard]] constexpr auto arcTangent(double y, double x) noexcept -> double {
	if (isZero(x) && isZero(y)) {
		return 0;
	}
	if (isZero(x)) {
		return y > 0 ? std::numbers::pi / 2 : -std::numbers::pi / 2;
	}
	if (isZero(y)) {
		return x > 0 ? 0.0 : std::numbers::pi;
	}
	const double angle = arcTangentPositive(magnitude(y / x));
	if (x > 0) {
		return y > 0 ? angle : -angle;
	}
	return y > 0 ? std::numbers::pi - angle : -std::numbers::pi + angle;
}

[[nodiscard]] constexpr auto tangentOf(double radians) noexcept -> double {
	const double adjacent = cosineOf(radians);
	if (isZero(adjacent)) {
		return powerCeiling;
	}
	return sineOf(radians) / adjacent;
}

[[nodiscard]] constexpr auto hypotenuse(double x, double y) noexcept -> double {
	const double horizontal = magnitude(x);
	const double vertical = magnitude(y);
	const double larger = horizontal > vertical ? horizontal : vertical;
	if (isZero(larger)) {
		return 0;
	}
	const double scaledX = horizontal / larger;
	const double scaledY = vertical / larger;
	return larger * squareRoot(scaledX * scaledX + scaledY * scaledY);
}

[[nodiscard]] constexpr auto isSpace(char byte) noexcept -> bool {
	return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r';
}

[[nodiscard]] constexpr auto trim(std::string_view text) -> std::string_view {
	std::size_t first = 0;
	while (first < text.size() && isSpace(text.at(first))) {
		++first;
	}
	std::size_t last = text.size();
	while (last > first && isSpace(text.at(last - 1))) {
		--last;
	}
	return text.substr(first, last - first);
}

[[nodiscard]] constexpr auto startsWith(std::string_view text, std::string_view prefix) noexcept
	-> bool {
	return text.starts_with(prefix);
}

[[nodiscard]] constexpr auto endsWith(std::string_view text, std::string_view suffix) noexcept
	-> bool {
	return text.ends_with(suffix);
}

[[nodiscard]] constexpr auto asciiLower(std::string_view text) -> std::string {
	std::string lower;
	lower.reserve(text.size());
	for (const char byte : text) {
		if (byte >= 'A' && byte <= 'Z') {
			lower.push_back(static_cast<char>(byte - 'A' + 'a'));
		} else {
			lower.push_back(byte);
		}
	}
	return lower;
}

struct Scanner {
	std::string_view text;
	std::size_t index = 0;
	const Context *context = nullptr;
	std::size_t origin = 0;

	[[nodiscard]] constexpr auto position() const noexcept -> std::size_t { return origin + index; }

	constexpr void spaces() {
		while (index < text.size() && isSpace(text.at(index))) {
			++index;
		}
	}

	constexpr void separator() {
		spaces();
		if (index < text.size() && text.at(index) == ',') {
			++index;
			spaces();
			if (index < text.size() && text.at(index) == ',') {
				context->fail(
					{.code = "invalidValue", .explanation = "Invalid SVG number separator"},
					position()
				);
			}
		}
	}

	[[nodiscard]] constexpr auto number(bool separated) -> double {
		if (separated) {
			separator();
		} else {
			spaces();
		}
		const std::size_t start = position();
		bool negative = false;
		if (index < text.size() && (text.at(index) == '+' || text.at(index) == '-')) {
			negative = text.at(index) == '-';
			++index;
		}

		struct Digits {
			double significand = 0;
			int exponentAdjust = 0;
			int digits = 0;

			constexpr void take(int digit, bool fraction) {
				++digits;
				if (significand < significandLimit) {
					significand = significand * decimalBase + static_cast<double>(digit);
					if (fraction) {
						--exponentAdjust;
					}
				} else if (!fraction) {
					++exponentAdjust;
				}
			}
		};

		Digits parsed;
		while (index < text.size() && text.at(index) >= '0' && text.at(index) <= '9') {
			parsed.take(text.at(index) - '0', false);
			++index;
		}
		if (index < text.size() && text.at(index) == '.') {
			++index;
			while (index < text.size() && text.at(index) >= '0' && text.at(index) <= '9') {
				parsed.take(text.at(index) - '0', true);
				++index;
			}
		}
		if (parsed.digits == 0) {
			context->fail({.code = "invalidValue", .explanation = "Expected SVG number"}, start);
		}
		int exponent = 0;
		bool exponentOverflow = false;
		if (index < text.size() && (text.at(index) == 'e' || text.at(index) == 'E')) {
			++index;
			bool exponentNegative = false;
			if (index < text.size() && (text.at(index) == '+' || text.at(index) == '-')) {
				exponentNegative = text.at(index) == '-';
				++index;
			}
			const std::size_t exponentStart = index;
			while (index < text.size() && text.at(index) >= '0' && text.at(index) <= '9') {
				const int digit = text.at(index) - '0';
				if (exponent > maximumExponent / decimalBase) {
					exponentOverflow = true;
				} else {
					exponent = exponent * decimalBase + digit;
				}
				++index;
			}
			if (index == exponentStart) {
				context->fail(
					{.code = "invalidValue", .explanation = "Invalid SVG number exponent"},
					start
				);
			}
			if (exponentOverflow) {
				if (exponentNegative) {
					return context->checked(0, start);
				}
				context->fail(
					{.code = "invalidValue", .explanation = "Nonfinite or overflowing SVG number"},
					start
				);
			}
			if (exponentNegative) {
				exponent = -exponent;
			}
		}
		const int totalExponent = exponent + parsed.exponentAdjust;
		double magnitudeValue = 0;
		if (!isZero(parsed.significand)) {
			int scientific = totalExponent;
			double probe = parsed.significand;
			while (probe >= decimalBase) {
				probe /= decimalBase;
				++scientific;
			}
			if (scientific > finiteExponent ||
				(scientific == finiteExponent && probe >= infinityProbe)) {
				context->fail(
					{.code = "invalidValue", .explanation = "Nonfinite or overflowing SVG number"},
					start
				);
			}
			if (scientific >= hugeExponent) {
				context->fail(
					{.code = "budgetExceeded", .explanation = "SVG numeric magnitude exceeds 1e9"},
					start
				);
			}
			if (scientific >= underflowExponent) {
				int count = totalExponent < 0 ? -totalExponent : totalExponent;
				double scale = 1;
				double base = decimalBase;
				while (count > 0) {
					if (count % 2 == 1) {
						scale *= base;
					}
					count /= 2;
					if (count > 0) {
						base *= base;
					}
				}
				magnitudeValue =
					totalExponent < 0 ? parsed.significand / scale : parsed.significand * scale;
			}
		}
		const double signedValue = negative ? -magnitudeValue : magnitudeValue;
		return context->checked(signedValue, start);
	}

	[[nodiscard]] constexpr auto flag() -> bool {
		separator();
		if (index >= text.size() || (text.at(index) != '0' && text.at(index) != '1')) {
			context->fail(
				{.code = "invalidValue", .explanation = "Arc flag must be 0 or 1"},
				position()
			);
		}
		const bool value = text.at(index) == '1';
		++index;
		return value;
	}

	[[nodiscard]] constexpr auto list() -> std::vector<double> {
		spaces();
		std::vector<double> values;
		while (index < text.size()) {
			values.push_back(number(!values.empty()));
			spaces();
		}
		return values;
	}
};

[[nodiscard]] constexpr auto
makeScanner(std::string_view text, const Context &context, std::size_t origin) -> Scanner {
	return {.text = text, .index = 0, .context = &context, .origin = origin};
}

[[nodiscard]] constexpr auto
svgLength(std::string_view raw, const Context &context, std::size_t origin) -> double {
	const std::string_view trimmed = trim(raw);
	if (endsWith(trimmed, "rem") || endsWith(trimmed, "em") || endsWith(trimmed, "ex") ||
		endsWith(trimmed, "ch")) {
		context.fail(
			{.code = "unsupportedFeature",
				.explanation = "Font-relative SVG lengths are unsupported"},
			origin
		);
	}
	Scanner scanner = makeScanner(raw, context, origin);
	const double value = scanner.number(false);
	if (startsWith(scanner.text.substr(scanner.index), "px")) {
		scanner.index += 2;
	}
	scanner.spaces();
	if (scanner.index != scanner.text.size()) {
		context.fail(
			{.code = "unsupportedFeature",
				.explanation = "Only unitless and px SVG lengths are supported"},
			scanner.position()
		);
	}
	return value;
}

[[nodiscard]] constexpr auto
svgScalar(std::string_view raw, const Context &context, std::size_t origin) -> double {
	Scanner scanner = makeScanner(raw, context, origin);
	const double value = scanner.number(false);
	scanner.spaces();
	if (scanner.index != scanner.text.size()) {
		context.fail(
			{.code = "invalidValue", .explanation = "Expected a unitless SVG number"},
			scanner.position()
		);
	}
	return value;
}

[[nodiscard]] constexpr auto
checkedTransform(AssetTransform transform, const Context &context, std::size_t index)
	-> AssetTransform {
	transform.a = ScaleFactor{context.checked(transform.a.value(), index)};
	transform.b = ScaleFactor{context.checked(transform.b.value(), index)};
	transform.c = ScaleFactor{context.checked(transform.c.value(), index)};
	transform.d = ScaleFactor{context.checked(transform.d.value(), index)};
	transform.tx = AssetUnits{context.checked(transform.tx.value(), index)};
	transform.ty = AssetUnits{context.checked(transform.ty.value(), index)};
	return transform;
}

[[nodiscard]] constexpr auto
svgTransform(std::string_view raw, const Context &context, std::size_t origin) -> AssetTransform {
	Scanner scanner = makeScanner(raw, context, origin);
	AssetTransform result{};
	int count = 0;
	scanner.spaces();
	while (scanner.index < scanner.text.size()) {
		++count;
		if (count > transformLimit) {
			context.fail(
				{.code = "budgetExceeded",
					.explanation = "More than 64 transforms in an attribute"},
				scanner.position()
			);
		}
		const std::size_t nameStart = scanner.index;
		while (scanner.index < scanner.text.size()) {
			const char byte = scanner.text.at(scanner.index);
			if (!((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z'))) {
				break;
			}
			++scanner.index;
		}
		const std::string name(scanner.text.substr(nameStart, scanner.index - nameStart));
		scanner.spaces();
		if (scanner.index >= scanner.text.size() || scanner.text.at(scanner.index) != '(') {
			context.fail(
				{.code = "invalidValue", .explanation = "Invalid SVG transform function"},
				scanner.position()
			);
		}
		++scanner.index;
		scanner.spaces();
		std::vector<double> arguments;
		while (scanner.index < scanner.text.size() && scanner.text.at(scanner.index) != ')') {
			arguments.push_back(scanner.number(!arguments.empty()));
			if (static_cast<int>(arguments.size()) > transformArgumentLimit) {
				context.fail(
					{.code = "invalidValue", .explanation = "Too many SVG transform arguments"},
					scanner.position()
				);
			}
			scanner.spaces();
		}
		if (scanner.index >= scanner.text.size()) {
			context.fail(
				{.code = "invalidValue", .explanation = "Unclosed SVG transform"},
				scanner.position()
			);
		}
		++scanner.index;
		AssetTransform next{};
		const int argumentCount = static_cast<int>(arguments.size());
		if (name == "matrix" && argumentCount == transformArgumentLimit) {
			next = {
				.a = ScaleFactor{arguments.at(0)},
				.b = ScaleFactor{arguments.at(1)},
				.c = ScaleFactor{arguments.at(2)},
				.d = ScaleFactor{arguments.at(3)},
				.tx = AssetUnits{arguments.at(4)},
				.ty = AssetUnits{arguments.at(static_cast<std::size_t>(transformArgumentLimit - 1))}
			};
		} else if (name == "translate" && (argumentCount == 1 || argumentCount == 2)) {
			const double vertical = argumentCount == 2 ? arguments.at(1) : 0.0;
			next = AssetTransform::translation(AssetUnits{arguments.at(0)}, AssetUnits{vertical});
		} else if (name == "scale" && (argumentCount == 1 || argumentCount == 2)) {
			const double vertical = argumentCount == 2 ? arguments.at(1) : arguments.at(0);
			next = AssetTransform::scale(ScaleFactor{arguments.at(0)}, ScaleFactor{vertical});
		} else if (name == "rotate" && (argumentCount == 1 || argumentCount == rgbChannelCount)) {
			const double radians = truncatingRemainder(arguments.at(0), fullCircleDegrees) *
				std::numbers::pi / straightDegrees;
			const double sine = sineOf(radians);
			const double cosine = cosineOf(radians);
			const AssetTransform rotation{
				.a = ScaleFactor{cosine},
				.b = ScaleFactor{sine},
				.c = ScaleFactor{-sine},
				.d = ScaleFactor{cosine}
			};
			if (argumentCount == rgbChannelCount) {
				const AssetTransform around = AssetTransform::translation(
					AssetUnits{arguments.at(1)},
					AssetUnits{arguments.at(2)}
				);
				const AssetTransform back = AssetTransform::translation(
					AssetUnits{-arguments.at(1)},
					AssetUnits{-arguments.at(2)}
				);
				next = around.concatenating(rotation).concatenating(back);
			} else {
				next = rotation;
			}
		} else if (name == "skewX" && argumentCount == 1) {
			const double radians = truncatingRemainder(arguments.at(0), fullCircleDegrees) *
				std::numbers::pi / straightDegrees;
			next = {
				.a = ScaleFactor{1},
				.c = ScaleFactor{context.checked(tangentOf(radians), scanner.position())}
			};
		} else if (name == "skewY" && argumentCount == 1) {
			const double radians = truncatingRemainder(arguments.at(0), fullCircleDegrees) *
				std::numbers::pi / straightDegrees;
			next = {
				.a = ScaleFactor{1},
				.b = ScaleFactor{context.checked(tangentOf(radians), scanner.position())}
			};
		} else {
			std::string explanation = "Unsupported transform or invalid argument count: ";
			explanation.append(name);
			context.fail({.code = "invalidValue", .explanation = explanation}, scanner.position());
		}
		result = checkedTransform(result.concatenating(next), context, scanner.position());
		scanner.spaces();
		if (scanner.index < scanner.text.size() && scanner.text.at(scanner.index) == ',') {
			++scanner.index;
			scanner.spaces();
			if (scanner.index >= scanner.text.size()) {
				context.fail(
					{.code = "invalidValue", .explanation = "Trailing transform separator"},
					scanner.position()
				);
			}
		}
	}
	return result;
}

struct ArcRequest {
	double startX = 0;
	double startY = 0;
	double endX = 0;
	double endY = 0;
	double radiusX = 0;
	double radiusY = 0;
	double degrees = 0;
	bool large = false;
	bool clockwise = false;
};

[[nodiscard]] constexpr auto
normalizedArc(const ArcRequest &arc, const Context &context, std::size_t index) -> ArcTo {
	const double phi =
		truncatingRemainder(arc.degrees, fullCircleDegrees) * std::numbers::pi / straightDegrees;
	const double cosine = cosineOf(phi);
	const double sine = sineOf(phi);
	const double deltaX = (arc.startX - arc.endX) / 2;
	const double deltaY = (arc.startY - arc.endY) / 2;
	const double primeX = cosine * deltaX + sine * deltaY;
	const double primeY = -sine * deltaX + cosine * deltaY;
	double radiusX = arc.radiusX;
	double radiusY = arc.radiusY;
	const double correction = hypotenuse(primeX / radiusX, primeY / radiusY);
	if (correction > 1) {
		radiusX *= correction;
		radiusY *= correction;
	}
	radiusX = context.checked(radiusX, index);
	radiusY = context.checked(radiusY, index);
	const double unitX = primeX / radiusX;
	const double unitY = primeY / radiusY;
	const double norm = unitX * unitX + unitY * unitY;
	const double ratio = isZero(norm) ? 0.0 : (1 - norm) / norm;
	const double root = squareRoot(ratio > 0 ? ratio : 0);
	const double direction = arc.large == arc.clockwise ? negativeUnit : 1;
	const double coefficient = direction * root;
	const double localX = coefficient * radiusX * unitY;
	const double localY = -coefficient * radiusY * unitX;
	const double midX = (arc.startX + arc.endX) / 2;
	const double midY = (arc.startY + arc.endY) / 2;
	const double centerX = context.checked(cosine * localX - sine * localY + midX, index);
	const double centerY = context.checked(sine * localX + cosine * localY + midY, index);
	const double axisAX = (primeX - localX) / radiusX;
	const double axisAY = (primeY - localY) / radiusY;
	const double axisBX = (-primeX - localX) / radiusX;
	const double axisBY = (-primeY - localY) / radiusY;
	const double start = arcTangent(axisAY, axisAX);
	double sweep = arcTangent(axisAX * axisBY - axisAY * axisBX, axisAX * axisBX + axisAY * axisBY);
	const double turn = 2 * std::numbers::pi;
	if (arc.clockwise && sweep < 0) {
		sweep += turn;
	}
	if (!arc.clockwise && sweep > 0) {
		sweep -= turn;
	}
	return {
		.centerX = AssetUnits{centerX},
		.centerY = AssetUnits{centerY},
		.radiusX = AssetUnits{radiusX},
		.radiusY = AssetUnits{radiusY},
		.rotation = Angle{context.checked(phi, index)},
		.startAngle = Angle{context.checked(start, index)},
		.sweepAngle = Angle{context.checked(sweep, index)}
	};
}

struct PlotPoint {
	double x = 0;
	double y = 0;
};

[[nodiscard]] constexpr auto
svgPath(std::string_view source, int limit, const Context &context, std::size_t origin)
	-> std::vector<PathCommand> {
	Scanner scanner = makeScanner(source, context, origin);
	std::vector<PathCommand> commands;
	unsigned char command = 0;
	double x = 0;
	double y = 0;
	double startX = 0;
	double startY = 0;
	std::optional<PlotPoint> cubicControl;
	std::optional<PlotPoint> quadraticControl;
	auto add = [&](PathCommand value) -> void {
		if (std::cmp_greater_equal(commands.size(), limit)) {
			context.fail(
				{.code = "budgetExceeded",
					.explanation = "Normalized SVG path command limit exceeded"},
				scanner.position()
			);
		}
		commands.push_back(value);
	};
	scanner.spaces();
	while (scanner.index < scanner.text.size()) {
		const auto byte = static_cast<unsigned char>(scanner.text.at(scanner.index));
		const bool explicitCommand = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
		if (explicitCommand) {
			command = byte;
			++scanner.index;
		}
		const auto lower = static_cast<unsigned char>(command | asciiLowerBit);
		const bool relative = command >= 'a';
		const bool known = lower == 'm' || lower == 'l' || lower == 'h' || lower == 'v' ||
			lower == 'c' || lower == 's' || lower == 'q' || lower == 't' || lower == 'a' ||
			lower == 'z';
		if (!known) {
			context.fail(
				{.code = "invalidValue", .explanation = "Unsupported or missing SVG path command"},
				scanner.position()
			);
		}
		if (commands.empty() && lower != 'm') {
			context.fail(
				{.code = "invalidValue", .explanation = "SVG path must begin with moveto"},
				scanner.position()
			);
		}
		if (lower == 'z') {
			if (!explicitCommand) {
				context.fail(
					{.code = "invalidValue", .explanation = "Unexpected arguments after closepath"},
					scanner.position()
				);
			}
			add(ClosePath{});
			x = startX;
			y = startY;
			cubicControl = std::nullopt;
			quadraticControl = std::nullopt;
			command = 0;
			scanner.spaces();
			continue;
		}

		struct Coordinate {
			Scanner *scanner = nullptr;
			bool explicitCommand = false;
			bool first = true;

			constexpr auto operator()() -> double {
				const double value = scanner->number(!(explicitCommand && first));
				first = false;
				return value;
			}
		};

		Coordinate coordinate{.scanner = &scanner, .explicitCommand = explicitCommand};
		auto point = [&]() -> PlotPoint {
			const double rawX = coordinate();
			const double rawY = coordinate();
			return {
				.x = context.checked(rawX + (relative ? x : 0), scanner.position()),
				.y = context.checked(rawY + (relative ? y : 0), scanner.position())
			};
		};
		std::optional<PlotPoint> nextCubic;
		std::optional<PlotPoint> nextQuadratic;
		if (lower == 'm' || lower == 'l') {
			const PlotPoint plotted = point();
			if (lower == 'm') {
				add(MoveTo{.x = AssetUnits{plotted.x}, .y = AssetUnits{plotted.y}});
			} else {
				add(LineTo{.x = AssetUnits{plotted.x}, .y = AssetUnits{plotted.y}});
			}
			x = plotted.x;
			y = plotted.y;
			if (lower == 'm') {
				startX = x;
				startY = y;
				command =
					relative ? static_cast<unsigned char>('l') : static_cast<unsigned char>('L');
			}
		} else if (lower == 'h') {
			x = context.checked(coordinate() + (relative ? x : 0), scanner.position());
			add(LineTo{.x = AssetUnits{x}, .y = AssetUnits{y}});
		} else if (lower == 'v') {
			y = context.checked(coordinate() + (relative ? y : 0), scanner.position());
			add(LineTo{.x = AssetUnits{x}, .y = AssetUnits{y}});
		} else if (lower == 'c' || lower == 's') {
			PlotPoint control{};
			if (lower == 'c') {
				control = point();
			} else if (cubicControl.has_value()) {
				const PlotPoint previous = cubicControl.value_or(PlotPoint{});
				control = {
					.x = context.checked(2 * x - previous.x, scanner.position()),
					.y = context.checked(2 * y - previous.y, scanner.position())
				};
			} else {
				control = {.x = x, .y = y};
			}
			const PlotPoint second = point();
			const PlotPoint plotted = point();
			add(CubicTo{
				.control1X = AssetUnits{control.x},
				.control1Y = AssetUnits{control.y},
				.control2X = AssetUnits{second.x},
				.control2Y = AssetUnits{second.y},
				.x = AssetUnits{plotted.x},
				.y = AssetUnits{plotted.y}
			});
			nextCubic = second;
			x = plotted.x;
			y = plotted.y;
		} else if (lower == 'q' || lower == 't') {
			PlotPoint control{};
			if (lower == 'q') {
				control = point();
			} else if (quadraticControl.has_value()) {
				const PlotPoint previous = quadraticControl.value_or(PlotPoint{});
				control = {
					.x = context.checked(2 * x - previous.x, scanner.position()),
					.y = context.checked(2 * y - previous.y, scanner.position())
				};
			} else {
				control = {.x = x, .y = y};
			}
			const PlotPoint plotted = point();
			add(QuadraticTo{
				.controlX = AssetUnits{control.x},
				.controlY = AssetUnits{control.y},
				.x = AssetUnits{plotted.x},
				.y = AssetUnits{plotted.y}
			});
			nextQuadratic = control;
			x = plotted.x;
			y = plotted.y;
		} else if (lower == 'a') {
			const double radiusX = coordinate();
			const double radiusY = coordinate();
			const double degrees = coordinate();
			const bool large = scanner.flag();
			const bool clockwise = scanner.flag();
			const PlotPoint plotted = point();
			if (radiusX < 0 || radiusY < 0) {
				context.fail(
					{.code = "invalidValue", .explanation = "Negative SVG arc radius"},
					scanner.position()
				);
			}
			if (!std::is_eq(plotted.x <=> x) || !std::is_eq(plotted.y <=> y)) {
				if (isZero(radiusX) || isZero(radiusY)) {
					add(LineTo{.x = AssetUnits{plotted.x}, .y = AssetUnits{plotted.y}});
				} else {
					add(normalizedArc(
						{.startX = x,
							.startY = y,
							.endX = plotted.x,
							.endY = plotted.y,
							.radiusX = radiusX,
							.radiusY = radiusY,
							.degrees = degrees,
							.large = large,
							.clockwise = clockwise},
						context,
						scanner.position()
					));
				}
			}
			x = plotted.x;
			y = plotted.y;
		}
		cubicControl = nextCubic;
		quadraticControl = nextQuadratic;
		scanner.spaces();
	}
	return commands;
}

struct Style {
	Paint fill = RgbaColor{};
	Paint stroke = NoPaint{};
	FillRule fillRule = FillRule::nonzero;
	Opacity fillOpacity{1};
	Opacity strokeOpacity{1};
	AssetUnits strokeWidth{1};
	LineCap lineCap = LineCap::butt;
	LineJoin lineJoin = LineJoin::miter;
	Ratio miterLimit{defaultMiter};
};

struct Attribute {
	std::string name;
	std::string value;
	std::size_t position = 0;
};

using Attributes = std::vector<Attribute>;
using Dictionary = std::vector<std::pair<std::string, std::string>>;

struct Frame {
	std::string qualified;
	std::string name;
	Dictionary namespaces;
	Style style{};
	AssetTransform transform{};
	AssetTransform cumulative{};
	Opacity opacity{1};
	std::vector<Element> children;
	std::optional<std::vector<PathCommand>> commands;
};

[[nodiscard]] constexpr auto diagnosticName(std::string_view value) -> std::string {
	if (value.size() <= static_cast<std::size_t>(diagnosticNameLimit)) {
		return std::string(value);
	}
	auto end = static_cast<std::size_t>(diagnosticNameLimit);
	while (end > 0 &&
		(static_cast<unsigned char>(value.at(end)) & utf8ContinuationMask) == utf8Continuation) {
		--end;
	}
	std::string text(value.substr(0, end));
	text.append(xmlEllipsis);
	return text;
}

[[nodiscard]] constexpr auto blank(std::string_view text) noexcept -> bool {
	for (const char byte : text) {
		if (!isSpace(byte)) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] constexpr auto isScalar(std::uint32_t code) noexcept -> bool {
	return code <= unicodeMaximum && !(code >= surrogateFirst && code <= surrogateLast);
}

constexpr void appendScalar(std::string &text, std::uint32_t code) {
	constexpr std::uint32_t oneByte = 0x80;
	constexpr std::uint32_t twoBytes = 0x800;
	constexpr std::uint32_t threeBytes = 0x10000;
	constexpr std::uint32_t leadTwo = 0xC0;
	constexpr std::uint32_t leadThree = 0xE0;
	constexpr std::uint32_t leadFour = 0xF0;
	constexpr std::uint32_t payload = 0x3F;
	constexpr int shift = 6;
	if (code < oneByte) {
		text.push_back(static_cast<char>(code));
	} else if (code < twoBytes) {
		text.push_back(static_cast<char>(leadTwo | (code >> shift)));
		text.push_back(static_cast<char>(utf8Continuation | (code & payload)));
	} else if (code < threeBytes) {
		text.push_back(static_cast<char>(leadThree | (code >> (shift * 2))));
		text.push_back(static_cast<char>(utf8Continuation | ((code >> shift) & payload)));
		text.push_back(static_cast<char>(utf8Continuation | (code & payload)));
	} else {
		text.push_back(static_cast<char>(leadFour | (code >> (shift * rgbChannelCount))));
		text.push_back(static_cast<char>(utf8Continuation | ((code >> (shift * 2)) & payload)));
		text.push_back(static_cast<char>(utf8Continuation | ((code >> shift) & payload)));
		text.push_back(static_cast<char>(utf8Continuation | (code & payload)));
	}
}

[[nodiscard]] constexpr auto hexDigit(char byte) noexcept -> int {
	if (byte >= '0' && byte <= '9') {
		return byte - '0';
	}
	if (byte >= 'a' && byte <= 'f') {
		return byte - 'a' + hexLetterOffset;
	}
	if (byte >= 'A' && byte <= 'F') {
		return byte - 'A' + hexLetterOffset;
	}
	constexpr int invalidHex = -1;
	return invalidHex;
}

[[nodiscard]] constexpr auto
decodeReferences(std::string_view raw, bool attributes, const Context &context, std::size_t origin)
	-> std::string {
	std::string text;
	std::size_t cursor = 0;
	while (cursor < raw.size()) {
		if (raw.at(cursor) != '&') {
			text.push_back(raw.at(cursor));
			++cursor;
			continue;
		}
		const std::size_t reference = cursor + 1;
		std::size_t end = reference;
		while (end < raw.size() && raw.at(end) != ';') {
			++end;
		}
		if (end >= raw.size()) {
			context.fail(
				{.code = "invalidXML", .explanation = "Unclosed XML character reference"},
				origin + cursor
			);
		}
		const std::string_view body = raw.substr(reference, end - reference);
		if (body == "amp") {
			text.push_back('&');
		} else if (body == "lt") {
			text.push_back('<');
		} else if (body == "gt") {
			text.push_back('>');
		} else if (body == "quot") {
			text.push_back('"');
		} else if (body == "apos") {
			text.push_back('\'');
		} else if (startsWith(body, "#")) {
			const bool hex = body.size() > 1 && (body.at(1) == 'x' || body.at(1) == 'X');
			const std::string_view digits = body.substr(hex ? 2 : 1);
			if (digits.empty()) {
				context.fail(
					{.code = "invalidXML",
						.explanation = "Invalid XML numeric character reference"},
					origin + cursor
				);
			}
			std::uint32_t code = 0;
			const int base = hex ? 16 : decimalBase;
			for (const char digit : digits) {
				const int value = hex ? hexDigit(digit) : digit - '0';
				if (value < 0 || value >= base) {
					context.fail(
						{.code = "invalidXML",
							.explanation = "Invalid XML numeric character reference"},
						origin + cursor
					);
				}
				code = code * static_cast<std::uint32_t>(base) + static_cast<std::uint32_t>(value);
			}
			if (!isScalar(code)) {
				context.fail(
					{.code = "invalidXML",
						.explanation = "Invalid XML numeric character reference"},
					origin + cursor
				);
			}
			appendScalar(text, code);
		} else {
			context.fail(
				{.code = "unsupportedFeature",
					.explanation =
						"Only predefined and numeric XML character references are permitted"},
				origin + cursor
			);
		}
		cursor = end + 1;
	}
	if (attributes) {
		std::string normalized;
		std::size_t index = 0;
		while (index < text.size()) {
			if (index + 1 < text.size() && text.at(index) == '\r' && text.at(index + 1) == '\n') {
				normalized.push_back(' ');
				index += 2;
			} else if (text.at(index) == '\r' || text.at(index) == '\n' || text.at(index) == '\t') {
				normalized.push_back(' ');
				++index;
			} else {
				normalized.push_back(text.at(index));
				++index;
			}
		}
		return normalized;
	}
	return text;
}

[[nodiscard]] constexpr auto lookup(const Dictionary &map, std::string_view key)
	-> std::optional<std::string> {
	for (const auto &entry : map) {
		if (entry.first == key) {
			return entry.second;
		}
	}
	return std::nullopt;
}

constexpr void assign(Dictionary &map, std::string key, std::string value) {
	for (auto &entry : map) {
		if (entry.first == key) {
			entry.second = std::move(value);
			return;
		}
	}
	map.emplace_back(std::move(key), std::move(value));
}

struct CodeRange {
	std::uint32_t first = 0;
	std::uint32_t last = 0;
};

inline constexpr std::uint32_t latinUpperFirst = 0x41;
inline constexpr std::uint32_t latinUpperLast = 0x5A;
inline constexpr std::uint32_t latinLowerFirst = 0x61;
inline constexpr std::uint32_t latinLowerLast = 0x7A;
inline constexpr std::uint32_t latinSupplementFirst = 0xC0;
inline constexpr std::uint32_t latinSupplementLast = 0xD6;
inline constexpr std::uint32_t latinExtendedFirst = 0xD8;
inline constexpr std::uint32_t latinExtendedLast = 0xF6;
inline constexpr std::uint32_t latinFurtherFirst = 0xF8;
inline constexpr std::uint32_t latinFurtherLast = 0x2FF;
inline constexpr std::uint32_t greekFirst = 0x370;
inline constexpr std::uint32_t greekLast = 0x37D;
inline constexpr std::uint32_t greekExtendedFirst = 0x37F;
inline constexpr std::uint32_t greekExtendedLast = 0x1FFF;
inline constexpr std::uint32_t joiningFirst = 0x200C;
inline constexpr std::uint32_t joiningLast = 0x200D;
inline constexpr std::uint32_t superScriptFirst = 0x2070;
inline constexpr std::uint32_t superScriptLast = 0x218F;
inline constexpr std::uint32_t cjkRadicalFirst = 0x2C00;
inline constexpr std::uint32_t cjkRadicalLast = 0x2FEF;
inline constexpr std::uint32_t cjkFirst = 0x3001;
inline constexpr std::uint32_t cjkLast = 0xD7FF;
inline constexpr std::uint32_t compatFirst = 0xF900;
inline constexpr std::uint32_t compatLast = 0xFDCF;
inline constexpr std::uint32_t compatFormFirst = 0xFDF0;
inline constexpr std::uint32_t compatFormLast = 0xFFFD;
inline constexpr std::uint32_t planeOneFirst = 0x10000;
inline constexpr std::uint32_t planeOneLast = 0xEFFFF;
inline constexpr std::size_t nameStartCount = 14;

[[nodiscard]] constexpr auto startRanges() noexcept -> std::array<CodeRange, nameStartCount> {
	return {{
		{.first = latinUpperFirst, .last = latinUpperLast},
		{.first = latinLowerFirst, .last = latinLowerLast},
		{.first = latinSupplementFirst, .last = latinSupplementLast},
		{.first = latinExtendedFirst, .last = latinExtendedLast},
		{.first = latinFurtherFirst, .last = latinFurtherLast},
		{.first = greekFirst, .last = greekLast},
		{.first = greekExtendedFirst, .last = greekExtendedLast},
		{.first = joiningFirst, .last = joiningLast},
		{.first = superScriptFirst, .last = superScriptLast},
		{.first = cjkRadicalFirst, .last = cjkRadicalLast},
		{.first = cjkFirst, .last = cjkLast},
		{.first = compatFirst, .last = compatLast},
		{.first = compatFormFirst, .last = compatFormLast},
		{.first = planeOneFirst, .last = planeOneLast},
	}};
}

[[nodiscard]] constexpr auto
inRanges(std::uint32_t value, const std::array<CodeRange, nameStartCount> &ranges) noexcept
	-> bool {
	for (const CodeRange range : ranges) {
		if (value >= range.first && value <= range.last) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] constexpr auto isNameStart(std::uint32_t value) noexcept -> bool {
	constexpr std::uint32_t underscore = 0x5F;
	return value == underscore || inRanges(value, startRanges());
}

[[nodiscard]] constexpr auto isNameChar(std::uint32_t value) noexcept -> bool {
	constexpr std::uint32_t hyphen = 0x2D;
	constexpr std::uint32_t dot = 0x2E;
	constexpr std::uint32_t middleDot = 0xB7;
	constexpr std::uint32_t digitFirst = 0x30;
	constexpr std::uint32_t digitLast = 0x39;
	constexpr std::uint32_t combiningFirst = 0x300;
	constexpr std::uint32_t combiningLast = 0x36F;
	constexpr std::uint32_t extenderFirst = 0x203F;
	constexpr std::uint32_t extenderLast = 0x2040;
	return isNameStart(value) || value == hyphen || value == dot || value == middleDot ||
		(value >= digitFirst && value <= digitLast) ||
		(value >= combiningFirst && value <= combiningLast) ||
		(value >= extenderFirst && value <= extenderLast);
}

[[nodiscard]] constexpr auto decodeScalar(std::string_view text, std::size_t index)
	-> std::optional<std::pair<std::uint32_t, std::size_t>> {
	if (index >= text.size()) {
		return std::nullopt;
	}
	constexpr unsigned char leadTwo = 0xC0;
	constexpr unsigned char leadThree = 0xE0;
	constexpr unsigned char leadFour = 0xF0;
	constexpr unsigned char leadFourLimit = 0xF8;
	constexpr unsigned char maskTwo = 0x1F;
	constexpr unsigned char maskThree = 0x0F;
	constexpr unsigned char maskFour = 0x07;
	const auto lead = static_cast<unsigned char>(text.at(index));
	int count = 1;
	std::uint32_t code = lead;
	if (lead >= leadFourLimit) {
		return std::nullopt;
	}
	if (lead >= leadFour) {
		count = longHexAlphaDigits;
		code = lead & maskFour;
	} else if (lead >= leadThree) {
		count = shortHexDigits;
		code = lead & maskThree;
	} else if (lead >= leadTwo) {
		count = 2;
		code = lead & maskTwo;
	} else if (lead >= utf8Continuation) {
		return std::nullopt;
	}
	if (index + static_cast<std::size_t>(count) > text.size()) {
		return std::nullopt;
	}
	for (int extra = 1; extra < count; ++extra) {
		const auto next =
			static_cast<unsigned char>(text.at(index + static_cast<std::size_t>(extra)));
		if ((next & utf8ContinuationMask) != utf8Continuation) {
			return std::nullopt;
		}
		constexpr unsigned char payload = 0x3F;
		constexpr int shift = 6;
		code = (code << shift) | (next & payload);
	}
	return std::pair<std::uint32_t, std::size_t>{code, index + static_cast<std::size_t>(count)};
}

[[nodiscard]] constexpr auto isNcName(std::string_view name) -> bool {
	if (name.empty()) {
		return false;
	}
	auto first = decodeScalar(name, 0);
	if (!first.has_value()) {
		return false;
	}
	const auto decoded = first.value_or(std::pair<std::uint32_t, std::size_t>{});
	if (!isNameStart(decoded.first)) {
		return false;
	}
	std::size_t cursor = decoded.second;
	while (cursor < name.size()) {
		auto next = decodeScalar(name, cursor);
		if (!next.has_value()) {
			return false;
		}
		const auto scalar = next.value_or(std::pair<std::uint32_t, std::size_t>{});
		if (!isNameChar(scalar.first)) {
			return false;
		}
		cursor = scalar.second;
	}
	return true;
}

[[nodiscard]] constexpr auto svgNamespaces(
	const Attributes &attributes,
	const Dictionary *parent,
	const Context &context,
	std::size_t index
) -> Dictionary {
	constexpr std::string_view xmlNamespace = "http://www.w3.org/XML/1998/namespace";
	constexpr std::string_view xmlnsNamespace = "http://www.w3.org/2000/xmlns/";
	Dictionary namespaces;
	if (parent == nullptr) {
		namespaces.emplace_back("xml", std::string(xmlNamespace));
	} else {
		namespaces = *parent;
	}
	Attributes declarations;
	for (const Attribute &attribute : attributes) {
		if (attribute.name == "xmlns" || startsWith(attribute.name, "xmlns:")) {
			declarations.push_back(attribute);
		}
	}
	std::ranges::sort(declarations, [](const Attribute &left, const Attribute &right) -> bool {
		return left.name < right.name;
	});
	for (const Attribute &attribute : declarations) {
		const std::string prefix =
			attribute.name == "xmlns" ? std::string{} : attribute.name.substr(xmlnsPrefixLength);
		const bool declared =
			attribute.name == "xmlns" || (isNcName(prefix) && !attribute.value.empty());
		const bool reservedOk = prefix != "xmlns" &&
			((prefix == "xml") == (attribute.value == xmlNamespace)) &&
			attribute.value != xmlnsNamespace;
		if (!declared || !reservedOk) {
			context.fail(
				{.code = "invalidXML",
					.explanation =
						"Invalid XML namespace declaration or reserved namespace binding"},
				attribute.position
			);
		}
		assign(namespaces, prefix, attribute.value);
	}
	static_cast<void>(index);
	return namespaces;
}

[[nodiscard]] constexpr auto channelByte(int byte) noexcept -> double {
	return static_cast<double>(byte) / static_cast<double>(colorByteMaximum);
}

[[nodiscard]] constexpr auto
svgPaint(std::string_view raw, const Context &context, std::size_t index) -> Paint {
	if (raw == "none") {
		return NoPaint{};
	}
	if (raw == "currentColor") {
		return CurrentColor{};
	}
	const std::string lower = asciiLower(trim(raw));
	if (lower == "black") {
		return RgbaColor{};
	}
	if (lower == "white") {
		return RgbaColor{
			.red = SdrChannel{1},
			.green = SdrChannel{1},
			.blue = SdrChannel{1},
			.alpha = Opacity{1}
		};
	}
	if (lower == "red") {
		return RgbaColor{
			.red = SdrChannel{1},
			.green = SdrChannel{0},
			.blue = SdrChannel{0},
			.alpha = Opacity{1}
		};
	}
	if (lower == "green") {
		return RgbaColor{
			.red = SdrChannel{0},
			.green = SdrChannel{channelByte(svgGreenByte)},
			.blue = SdrChannel{0},
			.alpha = Opacity{1}
		};
	}
	if (lower == "blue") {
		return RgbaColor{
			.red = SdrChannel{0},
			.green = SdrChannel{0},
			.blue = SdrChannel{1},
			.alpha = Opacity{1}
		};
	}
	if (lower == "transparent") {
		return RgbaColor{
			.red = SdrChannel{0},
			.green = SdrChannel{0},
			.blue = SdrChannel{0},
			.alpha = Opacity{0}
		};
	}
	if (startsWith(lower, "#")) {
		const std::string_view hex = std::string_view{lower}.substr(1);
		const bool widthOk = hex.size() == static_cast<std::size_t>(shortHexDigits) ||
			hex.size() == static_cast<std::size_t>(shortHexAlphaDigits) ||
			hex.size() == static_cast<std::size_t>(longHexDigits) ||
			hex.size() == static_cast<std::size_t>(longHexAlphaDigits);
		bool digitsOk = widthOk;
		if (digitsOk) {
			for (const char digit : hex) {
				if (hexDigit(digit) < 0) {
					digitsOk = false;
				}
			}
		}
		if (!digitsOk) {
			context.fail(
				{.code = "invalidValue", .explanation = "Invalid hexadecimal SVG color"},
				index
			);
		}
		const bool shortForm = static_cast<int>(hex.size()) < shortHexLimit;
		std::array<double, longHexAlphaDigits> channels{};
		int count = 0;
		for (std::size_t cursor = 0; cursor < hex.size();) {
			const int first = hexDigit(hex.at(cursor));
			const int second = shortForm ? first : hexDigit(hex.at(cursor + 1));
			constexpr int highNibble = 16;
			channels.at(static_cast<std::size_t>(count)) = channelByte(first * highNibble + second);
			++count;
			cursor += shortForm ? 1 : 2;
		}
		const double alpha = count == shortHexAlphaDigits ? channels.at(rgbChannelCount) : 1;
		return RgbaColor{
			.red = SdrChannel{channels.at(0)},
			.green = SdrChannel{channels.at(1)},
			.blue = SdrChannel{channels.at(2)},
			.alpha = Opacity{alpha}
		};
	}
	const bool alpha = startsWith(lower, "rgba(");
	if (alpha || startsWith(lower, "rgb(")) {
		if (!endsWith(lower, ")")) {
			context.fail(
				{.code = "invalidValue", .explanation = "Unclosed SVG color function"},
				index
			);
		}
		const auto prefix = static_cast<std::size_t>(alpha ? rgbaPrefixLength : rgbPrefixLength);
		const std::string body = lower.substr(prefix, lower.size() - prefix - 1);
		std::vector<std::string> parts;
		std::string current;
		for (const char byte : body) {
			if (byte == ',') {
				parts.emplace_back(trim(current));
				current.clear();
			} else {
				current.push_back(byte);
			}
		}
		parts.emplace_back(trim(current));
		const int expected = alpha ? rgbaChannelCount : rgbChannelCount;
		if (std::cmp_not_equal(parts.size(), expected)) {
			context.fail(
				{.code = "invalidValue",
					.explanation = "SVG colors require comma-separated channels"},
				index
			);
		}
		double red = 0;
		double green = 0;
		double blue = 0;
		for (int slot = 0; slot < rgbChannelCount; ++slot) {
			const std::string &part = parts.at(static_cast<std::size_t>(slot));
			if (part.empty()) {
				context.fail(
					{.code = "invalidValue",
						.explanation = "SVG RGB channels must be integers in 0...255"},
					index
				);
			}
			int value = 0;
			for (const char digit : part) {
				if (digit < '0' || digit > '9') {
					context.fail(
						{.code = "invalidValue",
							.explanation = "SVG RGB channels must be integers in 0...255"},
						index
					);
				}
				value = value * decimalBase + (digit - '0');
				if (value > colorByteMaximum) {
					context.fail(
						{.code = "invalidValue",
							.explanation = "SVG RGB channels must be integers in 0...255"},
						index
					);
				}
			}
			const double unit = channelByte(value);
			if (slot == 0) {
				red = unit;
			} else if (slot == 1) {
				green = unit;
			} else {
				blue = unit;
			}
		}
		const double opacity = alpha ? svgScalar(parts.at(rgbChannelCount), context, index) : 1;
		if (opacity < 0 || opacity > 1) {
			context.fail(
				{.code = "invalidValue", .explanation = "SVG color alpha must be in 0...1"},
				index
			);
		}
		return RgbaColor{
			.red = SdrChannel{red},
			.green = SdrChannel{green},
			.blue = SdrChannel{blue},
			.alpha = Opacity{opacity}
		};
	}
	context.fail(
		{.code = "unsupportedFeature",
			.explanation =
				"Unsupported SVG paint (use currentColor, none, or a supported literal color)"},
		index
	);
}

[[nodiscard]] constexpr auto svgStyle(Attributes attributes, Style parent, const Context &context)
	-> Style {
	std::ranges::sort(attributes, [](const Attribute &left, const Attribute &right) -> bool {
		return left.name < right.name;
	});
	for (const Attribute &attribute : attributes) {
		const std::string value(trim(attribute.value));
		if (value == "inherit") {
			continue;
		}
		if (attribute.name == "fill") {
			parent.fill = svgPaint(value, context, attribute.position);
		} else if (attribute.name == "stroke") {
			parent.stroke = svgPaint(value, context, attribute.position);
		} else if (attribute.name == "fill-rule") {
			if (value == "nonzero") {
				parent.fillRule = FillRule::nonzero;
			} else if (value == "evenodd") {
				parent.fillRule = FillRule::evenodd;
			} else {
				context.fail(
					{.code = "invalidValue", .explanation = "Invalid fill-rule"},
					attribute.position
				);
			}
		} else if (attribute.name == "stroke-linecap") {
			if (value == "butt") {
				parent.lineCap = LineCap::butt;
			} else if (value == "round") {
				parent.lineCap = LineCap::round;
			} else if (value == "square") {
				parent.lineCap = LineCap::square;
			} else {
				context.fail(
					{.code = "invalidValue", .explanation = "Invalid stroke-linecap"},
					attribute.position
				);
			}
		} else if (attribute.name == "stroke-linejoin") {
			if (value == "miter") {
				parent.lineJoin = LineJoin::miter;
			} else if (value == "round") {
				parent.lineJoin = LineJoin::round;
			} else if (value == "bevel") {
				parent.lineJoin = LineJoin::bevel;
			} else {
				context.fail(
					{.code = "invalidValue", .explanation = "Invalid stroke-linejoin"},
					attribute.position
				);
			}
		} else if (attribute.name == "fill-opacity" || attribute.name == "stroke-opacity") {
			const double opacity = svgScalar(value, context, attribute.position);
			if (opacity < 0 || opacity > 1) {
				std::string explanation = attribute.name;
				explanation.append(" must be in 0...1");
				context.fail(
					{.code = "invalidValue", .explanation = explanation},
					attribute.position
				);
			}
			if (attribute.name == "fill-opacity") {
				parent.fillOpacity = Opacity{opacity};
			} else {
				parent.strokeOpacity = Opacity{opacity};
			}
		} else if (attribute.name == "stroke-width") {
			parent.strokeWidth = AssetUnits{svgLength(value, context, attribute.position)};
			if (parent.strokeWidth.value() < 0) {
				context.fail(
					{.code = "invalidValue", .explanation = "Negative stroke-width"},
					attribute.position
				);
			}
		} else if (attribute.name == "stroke-miterlimit") {
			parent.miterLimit = Ratio{svgScalar(value, context, attribute.position)};
			if (parent.miterLimit.value() < 1) {
				context.fail(
					{.code = "invalidValue", .explanation = "stroke-miterlimit must be at least 1"},
					attribute.position
				);
			}
		}
	}
	return parent;
}

[[nodiscard]] constexpr auto attributeValue(const Attributes &attributes, std::string_view key)
	-> const Attribute * {
	for (const Attribute &attribute : attributes) {
		if (attribute.name == key) {
			return &attribute;
		}
	}
	return nullptr;
}

[[nodiscard]] constexpr auto lengthAttribute(
	const Attributes &attributes,
	std::string_view key,
	double fallback,
	const Context &context
) -> double {
	const Attribute *attribute = attributeValue(attributes, key);
	if (attribute == nullptr) {
		return fallback;
	}
	return svgLength(attribute->value, context, attribute->position);
}

[[nodiscard]] constexpr auto positiveAttribute(
	const Attributes &attributes,
	std::string_view key,
	double fallback,
	std::string_view element,
	const Context &context
) -> double {
	const double value = lengthAttribute(attributes, key, fallback, context);
	if (value < 0) {
		std::string explanation = "Negative ";
		explanation.append(element);
		explanation.append(" ");
		explanation.append(key);
		const Attribute *attribute = attributeValue(attributes, key);
		context.fail(
			{.code = "invalidValue", .explanation = explanation},
			attribute == nullptr ? 0 : attribute->position
		);
	}
	return value;
}

[[nodiscard]] constexpr auto
ellipseCommands(double centerX, double centerY, double radiusX, double radiusY)
	-> std::vector<PathCommand> {
	if (!(radiusX > 0) || !(radiusY > 0)) {
		return {};
	}
	const double turn = 2 * std::numbers::pi;
	return {
		MoveTo{.x = AssetUnits{centerX + radiusX}, .y = AssetUnits{centerY}},
		ArcTo{
			.centerX = AssetUnits{centerX},
			.centerY = AssetUnits{centerY},
			.radiusX = AssetUnits{radiusX},
			.radiusY = AssetUnits{radiusY},
			.rotation = Angle{},
			.startAngle = Angle{},
			.sweepAngle = Angle{turn}
		},
		ClosePath{}
	};
}

[[nodiscard]] constexpr auto
svgShape(std::string_view name, const Attributes &attributes, int limit, const Context &context)
	-> std::vector<PathCommand> {
	if (name == "path") {
		const Attribute *data = attributeValue(attributes, "d");
		const std::string_view source =
			data == nullptr ? std::string_view{} : std::string_view{data->value};
		const std::size_t origin = data == nullptr ? 0 : data->position;
		return svgPath(source, limit, context, origin);
	}
	if (name == "circle") {
		return ellipseCommands(
			lengthAttribute(attributes, "cx", 0, context),
			lengthAttribute(attributes, "cy", 0, context),
			positiveAttribute(attributes, "r", 0, name, context),
			positiveAttribute(attributes, "r", 0, name, context)
		);
	}
	if (name == "ellipse") {
		return ellipseCommands(
			lengthAttribute(attributes, "cx", 0, context),
			lengthAttribute(attributes, "cy", 0, context),
			positiveAttribute(attributes, "rx", 0, name, context),
			positiveAttribute(attributes, "ry", 0, name, context)
		);
	}
	if (name == "line") {
		return {
			MoveTo{
				.x = AssetUnits{lengthAttribute(attributes, "x1", 0, context)},
				.y = AssetUnits{lengthAttribute(attributes, "y1", 0, context)}
			},
			LineTo{
				.x = AssetUnits{lengthAttribute(attributes, "x2", 0, context)},
				.y = AssetUnits{lengthAttribute(attributes, "y2", 0, context)}
			}
		};
	}
	if (name == "polyline" || name == "polygon") {
		const Attribute *points = attributeValue(attributes, "points");
		Scanner scanner = makeScanner(
			points == nullptr ? std::string_view{} : std::string_view{points->value},
			context,
			points == nullptr ? 0 : points->position
		);
		std::vector<PathCommand> commands;
		scanner.spaces();
		while (scanner.index < scanner.text.size()) {
			if (std::cmp_greater_equal(commands.size(), limit)) {
				context.fail(
					{.code = "budgetExceeded",
						.explanation = "Normalized SVG command limit exceeded"},
					scanner.position()
				);
			}
			const double x = scanner.number(!commands.empty());
			const double y = scanner.number(true);
			if (commands.empty()) {
				commands.emplace_back(MoveTo{.x = AssetUnits{x}, .y = AssetUnits{y}});
			} else {
				commands.emplace_back(LineTo{.x = AssetUnits{x}, .y = AssetUnits{y}});
			}
			scanner.spaces();
		}
		if (name == "polygon" && !commands.empty()) {
			commands.emplace_back(ClosePath{});
		}
		return commands;
	}
	if (name == "rect") {
		const double x = lengthAttribute(attributes, "x", 0, context);
		const double y = lengthAttribute(attributes, "y", 0, context);
		const double width = positiveAttribute(attributes, "width", 0, name, context);
		const double height = positiveAttribute(attributes, "height", 0, name, context);
		const Attribute *rawRadiusX = attributeValue(attributes, "rx");
		const Attribute *rawRadiusY = attributeValue(attributes, "ry");
		const double radiusX = rawRadiusX == nullptr
			? (rawRadiusY == nullptr ? 0 : positiveAttribute(attributes, "ry", 0, name, context))
			: positiveAttribute(attributes, "rx", 0, name, context);
		const double radiusY =
			rawRadiusY == nullptr ? radiusX : positiveAttribute(attributes, "ry", 0, name, context);
		const double clampedX = radiusX < width / 2 ? radiusX : width / 2;
		const double clampedY = radiusY < height / 2 ? radiusY : height / 2;
		if (!(width > 0) || !(height > 0)) {
			return {};
		}
		const double right = context.checked(x + width, 0);
		const double bottom = context.checked(y + height, 0);
		if (isZero(clampedX) || isZero(clampedY)) {
			return {
				MoveTo{.x = AssetUnits{x}, .y = AssetUnits{y}},
				LineTo{.x = AssetUnits{right}, .y = AssetUnits{y}},
				LineTo{.x = AssetUnits{right}, .y = AssetUnits{bottom}},
				LineTo{.x = AssetUnits{x}, .y = AssetUnits{bottom}},
				ClosePath{}
			};
		}
		const double quarter = std::numbers::pi / 2;
		return {
			MoveTo{.x = AssetUnits{x + clampedX}, .y = AssetUnits{y}},
			LineTo{.x = AssetUnits{right - clampedX}, .y = AssetUnits{y}},
			ArcTo{
				.centerX = AssetUnits{right - clampedX},
				.centerY = AssetUnits{y + clampedY},
				.radiusX = AssetUnits{clampedX},
				.radiusY = AssetUnits{clampedY},
				.startAngle = Angle{-quarter},
				.sweepAngle = Angle{quarter}
			},
			LineTo{.x = AssetUnits{right}, .y = AssetUnits{bottom - clampedY}},
			ArcTo{
				.centerX = AssetUnits{right - clampedX},
				.centerY = AssetUnits{bottom - clampedY},
				.radiusX = AssetUnits{clampedX},
				.radiusY = AssetUnits{clampedY},
				.startAngle = Angle{},
				.sweepAngle = Angle{quarter}
			},
			LineTo{.x = AssetUnits{x + clampedX}, .y = AssetUnits{bottom}},
			ArcTo{
				.centerX = AssetUnits{x + clampedX},
				.centerY = AssetUnits{bottom - clampedY},
				.radiusX = AssetUnits{clampedX},
				.radiusY = AssetUnits{clampedY},
				.startAngle = Angle{quarter},
				.sweepAngle = Angle{quarter}
			},
			LineTo{.x = AssetUnits{x}, .y = AssetUnits{y + clampedY}},
			ArcTo{
				.centerX = AssetUnits{x + clampedX},
				.centerY = AssetUnits{y + clampedY},
				.radiusX = AssetUnits{clampedX},
				.radiusY = AssetUnits{clampedY},
				.startAngle = Angle{std::numbers::pi},
				.sweepAngle = Angle{quarter}
			},
			ClosePath{}
		};
	}
	return {};
}

[[nodiscard]] constexpr auto isPresentation(std::string_view key) noexcept -> bool {
	return key == "fill" || key == "fill-rule" || key == "fill-opacity" || key == "stroke" ||
		key == "stroke-opacity" || key == "stroke-width" || key == "stroke-linecap" ||
		key == "stroke-linejoin" || key == "stroke-miterlimit" || key == "opacity" ||
		key == "transform";
}

[[nodiscard]] constexpr auto isGeometryKey(const std::string &name, std::string_view key) noexcept
	-> bool {
	if (name == "path") {
		return key == "d";
	}
	if (name == "rect") {
		return key == "x" || key == "y" || key == "width" || key == "height" || key == "rx" ||
			key == "ry";
	}
	if (name == "circle") {
		return key == "cx" || key == "cy" || key == "r";
	}
	if (name == "ellipse") {
		return key == "cx" || key == "cy" || key == "rx" || key == "ry";
	}
	if (name == "line") {
		return key == "x1" || key == "y1" || key == "x2" || key == "y2";
	}
	if (name == "polyline" || name == "polygon") {
		return key == "points";
	}
	return false;
}

[[nodiscard]] constexpr auto isGeometryElement(std::string_view name) noexcept -> bool {
	return name == "path" || name == "rect" || name == "circle" || name == "ellipse" ||
		name == "line" || name == "polyline" || name == "polygon";
}

[[nodiscard]] constexpr auto isIdentity(const AssetTransform &transform) noexcept -> bool {
	constexpr double identityScale = 1;
	constexpr double zero = 0;
	return std::is_eq(transform.a.value() <=> identityScale) &&
		std::is_eq(transform.b.value() <=> zero) && std::is_eq(transform.c.value() <=> zero) &&
		std::is_eq(transform.d.value() <=> identityScale) &&
		std::is_eq(transform.tx.value() <=> zero) && std::is_eq(transform.ty.value() <=> zero);
}

constexpr void requireMapped(
	const AssetTransform &cumulative,
	AssetPoint point,
	const Context &context,
	std::size_t index
) {
	const AssetPoint mapped = cumulative.apply(point);
	static_cast<void>(context.checked(mapped.x.value(), index));
	static_cast<void>(context.checked(mapped.y.value(), index));
}

constexpr void requireGeometry(
	const std::vector<PathCommand> &commands,
	const AssetTransform &cumulative,
	const Context &context,
	std::size_t index
) {
	for (const PathCommand &command : commands) {
		if (const auto *move = std::get_if<MoveTo>(&command)) {
			requireMapped(cumulative, {.x = move->x, .y = move->y}, context, index);
		} else if (const auto *line = std::get_if<LineTo>(&command)) {
			requireMapped(cumulative, {.x = line->x, .y = line->y}, context, index);
		} else if (const auto *quadratic = std::get_if<QuadraticTo>(&command)) {
			requireMapped(
				cumulative,
				{.x = quadratic->controlX, .y = quadratic->controlY},
				context,
				index
			);
			requireMapped(cumulative, {.x = quadratic->x, .y = quadratic->y}, context, index);
		} else if (const auto *cubic = std::get_if<CubicTo>(&command)) {
			requireMapped(
				cumulative,
				{.x = cubic->control1X, .y = cubic->control1Y},
				context,
				index
			);
			requireMapped(
				cumulative,
				{.x = cubic->control2X, .y = cubic->control2Y},
				context,
				index
			);
			requireMapped(cumulative, {.x = cubic->x, .y = cubic->y}, context, index);
		} else if (const auto *arc = std::get_if<ArcTo>(&command)) {
			requireMapped(cumulative, {.x = arc->centerX, .y = arc->centerY}, context, index);
			const double cosine = cosineOf(arc->rotation.radians());
			const double sine = sineOf(arc->rotation.radians());
			static_cast<void>(context.checked(
				arc->radiusX.value() *
					(cumulative.a.value() * cosine + cumulative.c.value() * sine),
				index
			));
			static_cast<void>(context.checked(
				arc->radiusX.value() *
					(cumulative.b.value() * cosine + cumulative.d.value() * sine),
				index
			));
			static_cast<void>(context.checked(
				arc->radiusY.value() *
					(-cumulative.a.value() * sine + cumulative.c.value() * cosine),
				index
			));
			static_cast<void>(context.checked(
				arc->radiusY.value() *
					(-cumulative.b.value() * sine + cumulative.d.value() * cosine),
				index
			));
		}
	}
}

struct Parser {
	const Context *context = nullptr;
	std::string_view source;
	std::size_t index = 0;
	std::vector<Frame> stack;
	std::vector<Element> result;
	std::optional<AssetRect> viewBox;
	std::optional<std::string> rootNamespace;
	std::vector<std::string> ids;
	int count = 0;
	int commandCount = 0;
	bool preamble = true;
	std::size_t leading = 0;
};

constexpr void skipSpaces(Parser &parser) {
	while (parser.index < parser.source.size() && isSpace(parser.source.at(parser.index))) {
		++parser.index;
	}
}

[[nodiscard]] constexpr auto readToken(Parser &parser) -> std::string {
	const std::size_t start = parser.index;
	while (parser.index < parser.source.size()) {
		const char byte = parser.source.at(parser.index);
		if (isSpace(byte) || byte == '>' || byte == '/' || byte == '=') {
			break;
		}
		++parser.index;
	}
	return std::string(parser.source.substr(start, parser.index - start));
}

constexpr void expectByte(Parser &parser, char expected, Failure failure) {
	if (parser.index >= parser.source.size() || parser.source.at(parser.index) != expected) {
		parser.context->fail(failure, parser.index);
	}
	++parser.index;
}

[[nodiscard]] constexpr auto readAttributes(Parser &parser) -> Attributes {
	Attributes attributes;
	while (parser.index < parser.source.size()) {
		skipSpaces(parser);
		if (parser.index >= parser.source.size() || parser.source.at(parser.index) == '>' ||
			parser.source.at(parser.index) == '/') {
			break;
		}
		const std::size_t nameAt = parser.index;
		const std::string name = readToken(parser);
		skipSpaces(parser);
		bool duplicate = false;
		for (const Attribute &attribute : attributes) {
			if (attribute.name == name) {
				duplicate = true;
			}
		}
		if (name.empty() || duplicate) {
			parser.context->fail(
				{.code = "invalidXML", .explanation = "Malformed or duplicate XML attribute"},
				nameAt
			);
		}
		expectByte(
			parser,
			'=',
			{.code = "invalidXML", .explanation = "Malformed or duplicate XML attribute"}
		);
		skipSpaces(parser);
		if (parser.index >= parser.source.size() ||
			(parser.source.at(parser.index) != '"' && parser.source.at(parser.index) != '\'')) {
			parser.context->fail(
				{.code = "invalidXML", .explanation = "XML attribute requires a quoted value"},
				parser.index
			);
		}
		const char quote = parser.source.at(parser.index);
		++parser.index;
		const std::size_t valueAt = parser.index;
		while (parser.index < parser.source.size() && parser.source.at(parser.index) != quote) {
			++parser.index;
		}
		if (parser.index >= parser.source.size()) {
			parser.context->fail(
				{.code = "invalidXML", .explanation = "Unclosed XML attribute value"},
				valueAt
			);
		}
		const std::string raw(parser.source.substr(valueAt, parser.index - valueAt));
		++parser.index;
		if (parser.index < parser.source.size()) {
			const char next = parser.source.at(parser.index);
			if (!isSpace(next) && next != '/' && next != '>') {
				parser.context->fail(
					{.code = "invalidXML",
						.explanation = "XML attributes must be separated by whitespace"},
					parser.index
				);
			}
		}
		attributes.push_back(
			{.name = name,
				.value = decodeReferences(raw, true, *parser.context, valueAt),
				.position = valueAt}
		);
	}
	return attributes;
}

[[nodiscard]] constexpr auto
splitQualified(std::string_view qualified, const Context &context, std::size_t index)
	-> std::pair<std::string, std::string> {
	const std::size_t colon = qualified.find(':');
	if (colon == std::string_view::npos) {
		if (!isNcName(qualified)) {
			context.fail(
				{.code = "invalidXML", .explanation = "Invalid XML qualified element name"},
				index
			);
		}
		return {std::string{}, std::string(qualified)};
	}
	if (qualified.find(':', colon + 1) != std::string_view::npos) {
		context.fail(
			{.code = "invalidXML", .explanation = "Invalid XML qualified element name"},
			index
		);
	}
	const std::string_view prefix = qualified.substr(0, colon);
	const std::string_view local = qualified.substr(colon + 1);
	if (!isNcName(prefix) || !isNcName(local)) {
		context.fail(
			{.code = "invalidXML", .explanation = "Invalid XML qualified element name"},
			index
		);
	}
	return {std::string(prefix), std::string(local)};
}

constexpr void requireCommandsMapped(
	const std::vector<PathCommand> &commands,
	const AssetTransform &cumulative,
	const Context &context,
	std::size_t index
) {
	requireGeometry(commands, cumulative, context, index);
}

[[nodiscard]] constexpr auto finishFrame(Frame frame) -> std::vector<Element> {
	if (frame.commands.has_value()) {
		Element::Path path;
		path.commands = frame.commands.value_or(std::vector<PathCommand>{});
		path.transform = frame.transform;
		path.fill = frame.style.fill;
		path.fillRule = frame.style.fillRule;
		path.fillOpacity = frame.style.fillOpacity;
		path.stroke = frame.style.stroke;
		path.strokeOpacity = frame.style.strokeOpacity;
		path.strokeWidth = frame.style.strokeWidth;
		path.lineCap = frame.style.lineCap;
		path.lineJoin = frame.style.lineJoin;
		path.miterLimit = frame.style.miterLimit;
		path.opacity = frame.opacity;
		return {Element{.value = std::move(path)}};
	}
	if (frame.name == "title" || frame.name == "desc") {
		return {};
	}
	constexpr double opaque = 1;
	if (frame.name == "svg" && isIdentity(frame.transform) &&
		std::is_eq(frame.opacity.value() <=> opaque)) {
		return std::move(frame.children);
	}
	Element::Group group;
	group.transform = frame.transform;
	group.opacity = frame.opacity;
	group.children = std::move(frame.children);
	return {Element{.value = std::move(group)}};
}

constexpr void closeFrame(Parser &parser, std::string_view qualified) {
	if (parser.stack.empty() || parser.stack.back().qualified != qualified) {
		parser.context->fail(
			{.code = "invalidXML", .explanation = "Mismatched or unclosed XML tag"},
			parser.index
		);
	}
	Frame frame = std::move(parser.stack.back());
	parser.stack.pop_back();
	std::vector<Element> produced = finishFrame(std::move(frame));
	if (parser.stack.empty()) {
		parser.result = std::move(produced);
	} else {
		for (Element &element : produced) {
			parser.stack.back().children.push_back(std::move(element));
		}
	}
}

constexpr void openElement(
	Parser &parser,
	std::string qualified,
	const Attributes &attributes,
	std::size_t position
) {
	++parser.count;
	if (parser.count > elementLimit || std::cmp_greater_equal(parser.stack.size(), nestingLimit)) {
		parser.context->fail(
			{.code = "budgetExceeded",
				.explanation = "SVG element count or nesting limit exceeded"},
			position
		);
	}
	const Dictionary *parent = parser.stack.empty() ? nullptr : &parser.stack.back().namespaces;
	const Dictionary namespaces = svgNamespaces(attributes, parent, *parser.context, position);
	const auto [prefix, name] = splitQualified(qualified, *parser.context, position);
	if (prefix == "xmlns") {
		parser.context->fail(
			{.code = "invalidXML", .explanation = "Unbound SVG namespace prefix"},
			position
		);
	}
	if (!prefix.empty()) {
		const std::optional<std::string> bound = lookup(namespaces, prefix);
		if (!bound.has_value() || bound.value_or("").empty()) {
			parser.context->fail(
				{.code = "invalidXML", .explanation = "Unbound SVG namespace prefix"},
				position
			);
		}
	}
	const std::optional<std::string> namespaceValue = lookup(namespaces, prefix);
	const std::string elementNamespace = namespaceValue.value_or("");
	constexpr std::string_view svgNamespace = "http://www.w3.org/2000/svg";
	if (!elementNamespace.empty() && elementNamespace != svgNamespace) {
		parser.context->fail(
			{.code = "unsupportedFeature", .explanation = "Foreign SVG element namespace"},
			position
		);
	}
	if (parser.stack.empty()) {
		if (name != "svg" || parser.viewBox.has_value()) {
			parser.context->fail(
				{.code = "invalidXML", .explanation = "Expected exactly one svg root"},
				position
			);
		}
		parser.rootNamespace = elementNamespace;
	} else {
		const std::string root = parser.rootNamespace.value_or("");
		if (elementNamespace != root) {
			parser.context->fail(
				{.code = "unsupportedFeature", .explanation = "Mixed SVG element namespaces"},
				position
			);
		}
		const std::string &parentName = parser.stack.back().name;
		if (parentName != "svg" && parentName != "g") {
			std::string explanation = "Child elements are not permitted inside ";
			explanation.append(parentName);
			parser.context->fail(
				{.code = "unsupportedFeature", .explanation = explanation},
				position
			);
		}
		if (name == "svg") {
			parser.context->fail(
				{.code = "unsupportedFeature", .explanation = "Nested svg is unsupported"},
				position
			);
		}
	}
	if (name != "svg" && name != "g" && name != "title" && name != "desc" &&
		!isGeometryElement(name)) {
		std::string explanation = "Unsupported SVG element: ";
		explanation.append(diagnosticName(name));
		parser.context->fail({.code = "unsupportedFeature", .explanation = explanation}, position);
	}
	for (const Attribute &attribute : attributes) {
		if (attribute.name == "xmlns" || startsWith(attribute.name, "xmlns:")) {
			continue;
		}
		if (attribute.name == "id") {
			bool duplicate = false;
			for (const std::string &id : parser.ids) {
				if (id == attribute.value) {
					duplicate = true;
				}
			}
			if (attribute.value.empty() || duplicate) {
				parser.context->fail(
					{.code = "invalidValue", .explanation = "SVG id must be nonempty and unique"},
					attribute.position
				);
			}
			parser.ids.push_back(attribute.value);
			continue;
		}
		if (attribute.name.find(':') == std::string::npos &&
			(startsWith(attribute.name, "data-") || startsWith(attribute.name, "aria-"))) {
			continue;
		}
		const bool svgRootKey = name == "svg" &&
			(attribute.name == "viewBox" || attribute.name == "width" ||
				attribute.name == "height" || attribute.name == "preserveAspectRatio" ||
				attribute.name == "role");
		if (!(name == "title" || name == "desc") &&
			(isPresentation(attribute.name) || isGeometryKey(name, attribute.name) || svgRootKey)) {
			continue;
		}
		std::string explanation = "Unsupported ";
		explanation.append(diagnosticName(name));
		explanation.append(" attribute: ");
		explanation.append(diagnosticName(attribute.name));
		parser.context->fail(
			{.code = "unsupportedFeature", .explanation = explanation},
			attribute.position
		);
	}
	if (name == "svg") {
		const Attribute *foundBox = attributeValue(attributes, "viewBox");
		if (foundBox == nullptr) {
			parser.context->fail(
				{.code = "invalidValue", .explanation = "svg requires viewBox"},
				position
			);
		}
		const Attribute box = *foundBox;
		Scanner scanner = makeScanner(box.value, *parser.context, box.position);
		const std::vector<double> values = scanner.list();
		constexpr int viewBoxCount = 4;
		if (static_cast<int>(values.size()) != viewBoxCount || !(values.at(2) > 0) ||
			!(values.at(3) > 0)) {
			parser.context->fail(
				{.code = "invalidValue",
					.explanation = "viewBox requires four numbers and positive dimensions"},
				box.position
			);
		}
		parser.viewBox = AssetRect{
			.origin = {.x = AssetUnits{values.at(0)}, .y = AssetUnits{values.at(1)}},
			.size = {.width = AssetUnits{values.at(2)}, .height = AssetUnits{values.at(3)}}
		};
		constexpr std::array<std::string_view, 2> sides{"width", "height"};
		for (const std::string_view key : sides) {
			const Attribute *side = attributeValue(attributes, key);
			if (side != nullptr && !(svgLength(side->value, *parser.context, side->position) > 0)) {
				std::string explanation = "Root ";
				explanation.append(diagnosticName(key));
				explanation.append(" must be positive");
				parser.context->fail(
					{.code = "invalidValue", .explanation = explanation},
					side->position
				);
			}
		}
		const Attribute *aspect = attributeValue(attributes, "preserveAspectRatio");
		if (aspect != nullptr && aspect->value != "xMidYMid meet") {
			parser.context->fail(
				{.code = "unsupportedFeature",
					.explanation = "Only xMidYMid meet preserveAspectRatio is supported"},
				aspect->position
			);
		}
	}
	const Style inherited = parser.stack.empty() ? Style{} : parser.stack.back().style;
	const Style style = svgStyle(attributes, inherited, *parser.context);
	const Attribute *transformAttribute = attributeValue(attributes, "transform");
	const AssetTransform transform = transformAttribute == nullptr
		? AssetTransform{}
		: svgTransform(transformAttribute->value, *parser.context, transformAttribute->position);
	const AssetTransform parentTransform =
		parser.stack.empty() ? AssetTransform{} : parser.stack.back().cumulative;
	const AssetTransform cumulative =
		checkedTransform(parentTransform.concatenating(transform), *parser.context, position);
	const Attribute *opacityAttribute = attributeValue(attributes, "opacity");
	const Opacity opacity = opacityAttribute == nullptr
		? Opacity{1}
		: Opacity{svgScalar(opacityAttribute->value, *parser.context, opacityAttribute->position)};
	if (opacity.value() < 0 || opacity.value() > 1) {
		parser.context->fail(
			{.code = "invalidValue", .explanation = "opacity must be in 0...1"},
			position
		);
	}
	std::optional<std::vector<PathCommand>> commands;
	if (isGeometryElement(name)) {
		const int remaining = commandLimit - parser.commandCount;
		commands = svgShape(name, attributes, remaining > 0 ? remaining : 0, *parser.context);
		requireCommandsMapped(
			commands.value_or(std::vector<PathCommand>{}),
			cumulative,
			*parser.context,
			position
		);
		parser.commandCount +=
			static_cast<int>(commands.value_or(std::vector<PathCommand>{}).size());
		if (parser.commandCount > commandLimit) {
			parser.context->fail(
				{.code = "budgetExceeded", .explanation = "Normalized SVG command limit exceeded"},
				position
			);
		}
	}
	parser.stack.push_back(
		Frame{
			.qualified = std::move(qualified),
			.name = name,
			.namespaces = namespaces,
			.style = style,
			.transform = transform,
			.cumulative = cumulative,
			.opacity = opacity,
			.children = {},
			.commands = std::move(commands)
		}
	);
}

constexpr void acceptText(Parser &parser, std::string_view raw, std::size_t position) {
	const std::string decoded = decodeReferences(raw, false, *parser.context, position);
	if (!parser.stack.empty()) {
		const std::string &name = parser.stack.back().name;
		if (name == "title" || name == "desc") {
			return;
		}
	}
	if (!blank(decoded)) {
		parser.context->fail(
			{.code = "unsupportedFeature",
				.explanation = "Text is only supported inside title and desc"},
			position
		);
	}
}

constexpr void skipUntil(Parser &parser, std::string_view marker, Failure failure) {
	while (parser.index < parser.source.size() &&
		!startsWith(parser.source.substr(parser.index), marker)) {
		++parser.index;
	}
	if (parser.index >= parser.source.size()) {
		parser.context->fail(failure, parser.index);
	}
	parser.index += marker.size();
}

constexpr void parseMarkup(Parser &parser) {
	const std::size_t position = parser.index;
	constexpr std::size_t commentOpenLength = 4;
	constexpr std::size_t cdataOpenLength = 9;
	constexpr std::size_t cdataCloseLength = 3;
	constexpr std::size_t xmlDeclarationLength = 5;
	if (startsWith(parser.source.substr(parser.index), "<!--")) {
		parser.index += commentOpenLength;
		skipUntil(parser, "-->", {.code = "invalidXML", .explanation = "Unclosed XML construct"});
		return;
	}
	if (startsWith(parser.source.substr(parser.index), "<![CDATA[")) {
		parser.index += cdataOpenLength;
		const std::size_t start = parser.index;
		skipUntil(parser, "]]>", {.code = "invalidXML", .explanation = "Unclosed XML construct"});
		const std::size_t end = parser.index - cdataCloseLength;
		acceptText(parser, parser.source.substr(start, end - start), start);
		return;
	}
	if (startsWith(parser.source.substr(parser.index), "<!")) {
		parser.context->fail(
			{.code = "unsupportedFeature",
				.explanation = "SVG DTDs and entity declarations are forbidden"},
			position
		);
	}
	if (startsWith(parser.source.substr(parser.index), "<?")) {
		const bool declaration = parser.index == parser.leading &&
			startsWith(parser.source.substr(parser.index), "<?xml") &&
			parser.index + xmlDeclarationLength < parser.source.size() &&
			isSpace(parser.source.at(parser.index + xmlDeclarationLength));
		if (!declaration) {
			parser.context->fail(
				{.code = "unsupportedFeature",
					.explanation = "Only one leading XML declaration is permitted"},
				position
			);
		}
		parser.index += xmlDeclarationLength;
		skipUntil(parser, "?>", {.code = "invalidXML", .explanation = "Unclosed XML construct"});
		return;
	}
	++parser.index;
	const bool closing =
		parser.index < parser.source.size() && parser.source.at(parser.index) == '/';
	if (closing) {
		++parser.index;
	}
	const std::string qualified = readToken(parser);
	if (qualified.empty()) {
		parser.context->fail(
			{.code = "invalidXML", .explanation = "Invalid XML qualified element name"},
			position
		);
	}
	if (closing) {
		skipSpaces(parser);
		expectByte(
			parser,
			'>',
			{.code = "invalidXML", .explanation = "Unclosed XML tag or attribute"}
		);
		closeFrame(parser, qualified);
		parser.preamble = false;
		return;
	}
	const Attributes attributes = readAttributes(parser);
	skipSpaces(parser);
	bool selfClosing = false;
	if (parser.index < parser.source.size() && parser.source.at(parser.index) == '/') {
		selfClosing = true;
		++parser.index;
	}
	expectByte(parser, '>', {.code = "invalidXML", .explanation = "Unclosed XML tag or attribute"});
	parser.preamble = false;
	openElement(parser, qualified, attributes, position);
	if (selfClosing) {
		closeFrame(parser, qualified);
	}
}

constexpr void parseDocument(Parser &parser) {
	while (parser.index < parser.source.size()) {
		if (parser.source.at(parser.index) == '<') {
			parseMarkup(parser);
			continue;
		}
		const std::size_t start = parser.index;
		while (parser.index < parser.source.size() && parser.source.at(parser.index) != '<') {
			++parser.index;
		}
		acceptText(parser, parser.source.substr(start, parser.index - start), start);
	}
	if (!parser.stack.empty()) {
		parser.context->fail(
			{.code = "invalidXML", .explanation = "Unclosed XML tag or attribute"},
			parser.index
		);
	}
}

[[nodiscard]] constexpr auto compileSource(std::string_view source, bool mirrorsInRTL) -> Asset {
	const Context context{.source = source};
	if (source.size() > sourceBudget) {
		context.fail({.code = "budgetExceeded", .explanation = "Decoded SVG exceeds 1 MiB"}, 0);
	}
	std::size_t index = 0;
	if (source.size() >= bomSize && static_cast<unsigned char>(source.at(0)) == bomFirst &&
		static_cast<unsigned char>(source.at(1)) == bomSecond &&
		static_cast<unsigned char>(source.at(2)) == bomThird) {
		index = bomSize;
	}
	Parser parser{
		.context = &context,
		.source = source,
		.index = index,
		.stack = {},
		.result = {},
		.viewBox = std::nullopt,
		.rootNamespace = std::nullopt,
		.ids = {},
		.count = 0,
		.commandCount = 0,
		.preamble = true,
		.leading = index
	};
	parseDocument(parser);
	if (!parser.viewBox.has_value()) {
		context.fail(
			{.code = "invalidXML", .explanation = "Expected exactly one svg root with viewBox"},
			index
		);
	}
	constexpr std::uint32_t compiledBridgeVersion = 1;
	return Asset::compiled(
		compiledBridgeVersion,
		parser.viewBox.value_or(AssetRect{}),
		mirrorsInRTL,
		std::move(parser.result)
	);
}

} // namespace stana::symbol::detail
