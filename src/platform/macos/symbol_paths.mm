#include "symbol_paths.hpp"

#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

namespace stana::platform::detail {
namespace {
constexpr std::size_t cacheEntryLimit = 256;
constexpr std::size_t bytesPerMebibyte = std::size_t{1024} * 1024;
constexpr std::size_t cacheByteLimit = 16 * bytesPerMebibyte;
constexpr std::size_t segmentLimit = 1'048'576;
constexpr std::size_t bytesPerSegment = 96;
constexpr int depthLimit = 32;
constexpr double bucketScaleSlack = 2;
constexpr double flatteningErrorDevicePixels = 0.25;
constexpr double approximationErrorDevicePixels = flatteningErrorDevicePixels / bucketScaleSlack;
constexpr double minimumLocalScale = 1e-300;
constexpr double hermitePositionPower = 4;
constexpr double hermiteDerivativePower = 3;
constexpr double hermitePositionDenominator = 192;
constexpr double hermiteDerivativeDenominator = 12;
constexpr double hermiteControlDivisor = 3;
constexpr double singularTermCount = 4;
constexpr double midpointFactor = 0.5;
constexpr double quadraticControl = 2.0 / 3.0;
constexpr double rightAngleRadians = std::numbers::pi / 2;
constexpr double unitOpacity = 1;

struct PreparationLimit {};

struct PreparationFailed {};

struct PathPoint {
	double x = 0;
	double y = 0;

	[[nodiscard]] auto translated(PathPoint delta) const -> PathPoint {
		return {.x = x + delta.x, .y = y + delta.y};
	}

	[[nodiscard]] auto scaled(double factor) const -> PathPoint {
		return {.x = x * factor, .y = y * factor};
	}
};

struct PointPair {
	PathPoint left{};
	PathPoint right{};
};

struct ArcCurve {
	PathPoint center{};
	PathPoint radii{};
	double rotation = 0;
};

struct ArcSpan {
	double start = 0;
	double sweep = 0;
};

struct Flatness {
	double tolerance = 0;
	double strokeWidth = 0;
};

struct CubicSegment {
	PathPoint origin{};
	PathPoint controlA{};
	PathPoint controlB{};
	PathPoint end{};
};

/// @brief Flattening tolerance and recursion depth travel together.
struct Subdivision {
	double tolerance = 0;
	int depth = 0;
};

class Budget {
  public:
	explicit Budget(std::size_t baseBytes) : bytes_(baseBytes) { account(0); }

	void account(std::size_t count) {
		if (count > segmentLimit || segments_ > segmentLimit - count) {
			throw PreparationLimit{};
		}
		segments_ += count;
		if (count > 0 && bytesPerSegment > std::numeric_limits<std::size_t>::max() / count) {
			throw PreparationLimit{};
		}
		const std::size_t added = count * bytesPerSegment;
		if (added > std::numeric_limits<std::size_t>::max() - bytes_) {
			throw PreparationLimit{};
		}
		bytes_ += added;
	}

	void adoptSegments(std::size_t segments) {
		if (segments > segmentLimit) {
			throw PreparationLimit{};
		}
		segments_ = segments;
	}

	[[nodiscard]] auto bytes() const noexcept -> std::size_t { return bytes_; }

	[[nodiscard]] auto segments() const noexcept -> std::size_t { return segments_; }

  private:
	std::size_t segments_ = 0;
	std::size_t bytes_ = 0;
};

class MutablePath {
  public:
	explicit MutablePath(CGMutablePathRef path) : path_(path) {}

	MutablePath(const MutablePath &) = delete;
	MutablePath(MutablePath &&) = delete;
	auto operator=(const MutablePath &) -> MutablePath & = delete;
	auto operator=(MutablePath &&) -> MutablePath & = delete;

	~MutablePath() {
		if (path_ != nullptr) {
			CFRelease(path_);
		}
	}

	[[nodiscard]] auto get() const noexcept -> CGMutablePathRef { return path_; }

  private:
	CGMutablePathRef path_ = nullptr;
};

struct PreparedGraphic {
	std::shared_ptr<CoreOwner<CGPathRef>> fill{};
	std::shared_ptr<CoreOwner<CGPathRef>> stroke{};
};

struct FillBase {
	std::vector<std::shared_ptr<CoreOwner<CGPathRef>>> fills{};
	std::size_t bytes = 0;
	std::size_t segments = 0;
};

struct PreparedSymbol {
	std::vector<PreparedGraphic> paths{};
	std::size_t bytes = 0;
};

enum class StrokeRequest : std::uint8_t { omit, paint, invalid };

struct StrokePlan {
	StrokeRequest request = StrokeRequest::omit;
	double width = 0;
};

[[nodiscard]] auto adoptCore(CGPathRef __attribute__((cf_consumed)) reference)
	-> std::shared_ptr<CoreOwner<CGPathRef>> {
	if (reference == nullptr) {
		throw PreparationFailed{};
	}
	try {
		return std::make_shared<CoreOwner<CGPathRef>>(reference);
	} catch (...) {
		CFRelease(reference);
		throw;
	}
}

[[nodiscard]] auto publishPath(CGMutablePathRef path) -> std::shared_ptr<CoreOwner<CGPathRef>> {
	if (path == nullptr) {
		throw PreparationFailed{};
	}
	return adoptCore(CGPathCreateCopy(path));
}

[[nodiscard]] auto midpoint(PointPair pair) -> PathPoint {
	return pair.left.translated(pair.right).scaled(midpointFactor);
}

[[nodiscard]] auto arcPoint(const ArcCurve &curve, double angle) -> PathPoint {
	const double cosine = std::cos(angle);
	const double sine = std::sin(angle);
	const double rotationCosine = std::cos(curve.rotation);
	const double rotationSine = std::sin(curve.rotation);
	return {
		.x = curve.center.x + curve.radii.x * cosine * rotationCosine -
			curve.radii.y * sine * rotationSine,
		.y = curve.center.y + curve.radii.x * cosine * rotationSine +
			curve.radii.y * sine * rotationCosine
	};
}

[[nodiscard]] auto arcDerivative(const ArcCurve &curve, double angle) -> PathPoint {
	const double cosine = std::cos(angle);
	const double sine = std::sin(angle);
	const double rotationCosine = std::cos(curve.rotation);
	const double rotationSine = std::sin(curve.rotation);
	return {
		.x = -curve.radii.x * sine * rotationCosine - curve.radii.y * cosine * rotationSine,
		.y = -curve.radii.x * sine * rotationSine + curve.radii.y * cosine * rotationCosine
	};
}

[[nodiscard]] auto toleranceFor(double quality, double localMaximumScale) -> double {
	if (!std::isfinite(localMaximumScale)) {
		throw PreparationFailed{};
	}
	const double localScale = std::max(minimumLocalScale, quality * localMaximumScale);
	return approximationErrorDevicePixels / localScale;
}

/// @brief Counts a flattened outline without reading element coordinates.
void accountFlattened(CGPathRef path, Budget &budget) {
	if (path == nullptr) {
		throw PreparationFailed{};
	}

	struct State {
		Budget *budget = nullptr;
		bool excessive = false;
		bool failed = false;
	} state{.budget = &budget};

	State *const collected = &state;
	CGPathApplyWithBlock(path, ^(const CGPathElement *element) {
	  if (collected->excessive || collected->failed || element == nullptr) {
		  return;
	  }
	  switch (element->type) {
	  case kCGPathElementMoveToPoint:
		  return;
	  case kCGPathElementAddLineToPoint:
	  case kCGPathElementCloseSubpath:
		  try {
			  collected->budget->account(1);
		  } catch (const PreparationLimit &) {
			  collected->excessive = true;
		  }
		  return;
	  case kCGPathElementAddQuadCurveToPoint:
	  case kCGPathElementAddCurveToPoint:
		  collected->failed = true;
		  return;
	  }
	  collected->failed = true;
	});
	if (state.excessive) {
		throw PreparationLimit{};
	}
	if (state.failed) {
		throw PreparationFailed{};
	}
}

void expandArc(
	std::vector<SymbolVerbData> &commands,
	const ArcCurve &curve,
	ArcSpan span,
	const Flatness &flatness,
	int depth,
	Budget &budget
) {
	const double major = std::max(std::abs(curve.radii.x), std::abs(curve.radii.y));
	const double minor = std::min(std::abs(curve.radii.x), std::abs(curve.radii.y));
	const double sweepMagnitude = std::abs(span.sweep);
	const double positionError =
		major * std::pow(sweepMagnitude, hermitePositionPower) / hermitePositionDenominator;
	const double derivativeError =
		major * std::pow(sweepMagnitude, hermiteDerivativePower) / hermiteDerivativeDenominator;
	double normalError = 0;
	if (flatness.strokeWidth > 0) {
		if (!(minor > 0)) {
			throw PreparationLimit{};
		}
		normalError = flatness.strokeWidth * derivativeError / minor;
	}
	const bool withinFlatness =
		sweepMagnitude <= rightAngleRadians && positionError + normalError <= flatness.tolerance;
	if (withinFlatness) {
		budget.account(1);
		const PathPoint end = arcPoint(curve, span.start + span.sweep);
		const double controlScale = span.sweep / hermiteControlDivisor;
		const PathPoint control1 =
			arcPoint(curve, span.start)
				.translated(arcDerivative(curve, span.start).scaled(controlScale));
		const PathPoint control2 =
			end.translated(arcDerivative(curve, span.start + span.sweep).scaled(-controlScale));
		commands.push_back(
			{.kind = SymbolVerb::cubic,
				.x = end.x,
				.y = end.y,
				.c1x = control1.x,
				.c1y = control1.y,
				.c2x = control2.x,
				.c2y = control2.y}
		);
		return;
	}
	if (depth >= depthLimit) {
		throw PreparationLimit{};
	}
	const double halfSweep = span.sweep * midpointFactor;
	const int nextDepth = depth + 1;
	expandArc(
		commands,
		curve,
		{.start = span.start, .sweep = halfSweep},
		flatness,
		nextDepth,
		budget
	);
	expandArc(
		commands,
		curve,
		{.start = span.start + halfSweep, .sweep = halfSweep},
		flatness,
		nextDepth,
		budget
	);
}

[[nodiscard]] auto nativeCommands(
	const std::vector<SymbolVerbData> &commands,
	const Flatness &flatness,
	Budget &budget
) -> std::vector<SymbolVerbData> {
	std::vector<SymbolVerbData> native;
	for (const auto &command : commands) {
		budget.account(1);
		if (command.kind != SymbolVerb::arc) {
			native.push_back(command);
			continue;
		}
		const ArcCurve curve{
			.center = {.x = command.x, .y = command.y},
			.radii = {.x = command.c1x, .y = command.c1y},
			.rotation = command.rotation
		};
		expandArc(
			native,
			curve,
			{.start = command.c2x, .sweep = command.sweep},
			flatness,
			0,
			budget
		);
	}
	return native;
}

void flattenCubic(
	CGMutablePathRef path,
	const CubicSegment &cubic,
	const Subdivision &subdivision,
	Budget &budget
) {
	const double tolerance = subdivision.tolerance;
	const int depth = subdivision.depth;
	const PathPoint delta = cubic.end.translated(cubic.origin.scaled(-1));
	const double square = delta.x * delta.x + delta.y * delta.y;
	const auto distance = [&](PathPoint probe) -> double {
		const PathPoint relative = probe.translated(cubic.origin.scaled(-1));
		const double projected =
			square > 0 ? (relative.x * delta.x + relative.y * delta.y) / square : 0;
		const double fraction = std::clamp(projected, 0.0, unitOpacity);
		const PathPoint closest = cubic.origin.translated(delta.scaled(fraction));
		const PathPoint error = probe.translated(closest.scaled(-1));
		return std::hypot(error.x, error.y);
	};
	if (std::max(distance(cubic.controlA), distance(cubic.controlB)) <= tolerance) {
		budget.account(1);
		CGPathAddLineToPoint(path, nullptr, cubic.end.x, cubic.end.y);
		return;
	}
	if (depth >= depthLimit) {
		throw PreparationLimit{};
	}
	const PathPoint originControl = midpoint({.left = cubic.origin, .right = cubic.controlA});
	const PathPoint controls = midpoint({.left = cubic.controlA, .right = cubic.controlB});
	const PathPoint controlEnd = midpoint({.left = cubic.controlB, .right = cubic.end});
	const PathPoint left = midpoint({.left = originControl, .right = controls});
	const PathPoint right = midpoint({.left = controls, .right = controlEnd});
	const PathPoint middle = midpoint({.left = left, .right = right});
	const int nextDepth = depth + 1;
	const Subdivision next{.tolerance = tolerance, .depth = nextDepth};
	flattenCubic(
		path,
		{.origin = cubic.origin, .controlA = originControl, .controlB = left, .end = middle},
		next,
		budget
	);
	flattenCubic(
		path,
		{.origin = middle, .controlA = right, .controlB = controlEnd, .end = cubic.end},
		next,
		budget
	);
}

void flattenCommands(
	CGMutablePathRef path,
	const std::vector<SymbolVerbData> &commands,
	double tolerance,
	Budget &budget
) {
	PathPoint current{};
	PathPoint start{};
	for (const auto &command : commands) {
		switch (command.kind) {
		case SymbolVerb::move:
			current = {.x = command.x, .y = command.y};
			start = current;
			CGPathMoveToPoint(path, nullptr, current.x, current.y);
			break;
		case SymbolVerb::line:
			budget.account(1);
			current = {.x = command.x, .y = command.y};
			CGPathAddLineToPoint(path, nullptr, current.x, current.y);
			break;
		case SymbolVerb::quadratic: {
			const PathPoint end{.x = command.x, .y = command.y};
			const PathPoint control{.x = command.c1x, .y = command.c1y};
			const PathPoint controlA =
				current.translated(control.translated(current.scaled(-1)).scaled(quadraticControl));
			const PathPoint controlB =
				end.translated(control.translated(end.scaled(-1)).scaled(quadraticControl));
			flattenCubic(
				path,
				{.origin = current, .controlA = controlA, .controlB = controlB, .end = end},
				{.tolerance = tolerance, .depth = 0},
				budget
			);
			current = end;
			break;
		}
		case SymbolVerb::cubic: {
			const PathPoint end{.x = command.x, .y = command.y};
			flattenCubic(
				path,
				{.origin = current,
					.controlA = {.x = command.c1x, .y = command.c1y},
					.controlB = {.x = command.c2x, .y = command.c2y},
					.end = end},
				{.tolerance = tolerance, .depth = 0},
				budget
			);
			current = end;
			break;
		}
		case SymbolVerb::close:
			budget.account(1);
			CGPathCloseSubpath(path);
			current = start;
			break;
		case SymbolVerb::arc:
			throw PreparationFailed{};
		}
	}
}

void appendCurves(CGMutablePathRef path, const std::vector<SymbolVerbData> &commands) {
	for (const auto &command : commands) {
		switch (command.kind) {
		case SymbolVerb::move:
			CGPathMoveToPoint(path, nullptr, command.x, command.y);
			break;
		case SymbolVerb::line:
			CGPathAddLineToPoint(path, nullptr, command.x, command.y);
			break;
		case SymbolVerb::quadratic:
			CGPathAddQuadCurveToPoint(
				path,
				nullptr,
				command.c1x,
				command.c1y,
				command.x,
				command.y
			);
			break;
		case SymbolVerb::cubic:
			CGPathAddCurveToPoint(
				path,
				nullptr,
				command.c1x,
				command.c1y,
				command.c2x,
				command.c2y,
				command.x,
				command.y
			);
			break;
		case SymbolVerb::close:
			CGPathCloseSubpath(path);
			break;
		case SymbolVerb::arc:
			throw PreparationFailed{};
		}
	}
}

[[nodiscard]] auto nativeCap(SymbolLineCap cap) -> CGLineCap {
	switch (cap) {
	case SymbolLineCap::butt:
		return kCGLineCapButt;
	case SymbolLineCap::round:
		return kCGLineCapRound;
	case SymbolLineCap::square:
		return kCGLineCapSquare;
	}
	throw PreparationFailed{};
}

[[nodiscard]] auto nativeJoin(SymbolLineJoin join) -> CGLineJoin {
	switch (join) {
	case SymbolLineJoin::miter:
		return kCGLineJoinMiter;
	case SymbolLineJoin::round:
		return kCGLineJoinRound;
	case SymbolLineJoin::bevel:
		return kCGLineJoinBevel;
	}
	throw PreparationFailed{};
}

[[nodiscard]] auto planStroke(const SymbolLeaf &symbol, const SymbolNode::Path &path)
	-> StrokePlan {
	if (!path.stroke.has_value()) {
		return {};
	}
	const double width = symbol.strokeWidth.value_or(path.strokeWidth);
	if (!std::isfinite(width) || !std::isfinite(path.miterLimit) || !(path.miterLimit > 0)) {
		return {.request = StrokeRequest::invalid};
	}
	if (!(width > 0)) {
		return {};
	}
	return {.request = StrokeRequest::paint, .width = width};
}

[[nodiscard]] auto buildBase(const SymbolLeaf &symbol, double quality)
	-> std::shared_ptr<FillBase> {
	auto base = std::make_shared<FillBase>();
	Budget budget{symbol.estimatedBytes};

	struct Builder {
		double quality = 1;
		Budget *budget = nullptr;
		FillBase *base = nullptr;

		void visit(const std::vector<SymbolNode> &nodes) {
			for (const auto &node : nodes) {
				if (const auto *group = std::get_if<SymbolNode::Group>(&node.value)) {
					visit(group->children);
					continue;
				}
				const auto &path = std::get<SymbolNode::Path>(node.value);
				const Flatness flatness{.tolerance = toleranceFor(quality, path.localMaximumScale)};
				const auto native = nativeCommands(path.commands, flatness, *budget);
				const MutablePath curved{CGPathCreateMutable()};
				if (curved.get() == nullptr) {
					throw PreparationFailed{};
				}
				flattenCommands(curved.get(), native, flatness.tolerance, *budget);
				base->fills.push_back(publishPath(curved.get()));
			}
		}
	};

	Builder builder{.quality = quality, .budget = &budget, .base = base.get()};
	builder.visit(symbol.elements);
	base->bytes = budget.bytes();
	base->segments = budget.segments();
	return base;
}

[[nodiscard]] auto
buildPrepared(const SymbolLeaf &symbol, double quality, const std::shared_ptr<const FillBase> &base)
	-> std::shared_ptr<PreparedSymbol> {
	if (!base) {
		throw PreparationFailed{};
	}
	Budget budget{0};
	budget.adoptSegments(base->segments);
	auto prepared = std::make_shared<PreparedSymbol>();

	struct Builder {
		const SymbolLeaf *symbol = nullptr;
		double quality = 1;
		Budget *budget = nullptr;
		const FillBase *base = nullptr;
		PreparedSymbol *prepared = nullptr;
		std::size_t index = 0;

		void visit(const std::vector<SymbolNode> &nodes) {
			for (const auto &node : nodes) {
				if (const auto *group = std::get_if<SymbolNode::Group>(&node.value)) {
					visit(group->children);
					continue;
				}
				const auto &path = std::get<SymbolNode::Path>(node.value);
				if (index >= base->fills.size()) {
					throw PreparationFailed{};
				}
				const auto plan = planStroke(*symbol, path);
				if (plan.request == StrokeRequest::invalid) {
					throw PreparationFailed{};
				}
				std::shared_ptr<CoreOwner<CGPathRef>> stroke;
				if (plan.request == StrokeRequest::paint) {
					const Flatness flatness{
						.tolerance = toleranceFor(quality, path.localMaximumScale),
						.strokeWidth = plan.width
					};
					const auto native = nativeCommands(path.commands, flatness, *budget);
					const MutablePath curved{CGPathCreateMutable()};
					if (curved.get() == nullptr) {
						throw PreparationFailed{};
					}
					appendCurves(curved.get(), native);
					const auto outlined = adoptCore(CGPathCreateCopyByStrokingPath(
						curved.get(),
						nullptr,
						plan.width,
						nativeCap(path.lineCap),
						nativeJoin(path.lineJoin),
						path.miterLimit
					));
					const auto flat = adoptCore(
						CGPathCreateCopyByFlattening(outlined->get(), flatness.tolerance)
					);
					accountFlattened(flat->get(), *budget);
					stroke = flat;
				}
				prepared->paths.push_back(
					{.fill = base->fills.at(index), .stroke = std::move(stroke)}
				);
				++index;
			}
		}
	};

	Builder builder{
		.symbol = &symbol,
		.quality = quality,
		.budget = &budget,
		.base = base.get(),
		.prepared = prepared.get()
	};
	builder.visit(symbol.elements);
	if (builder.index != base->fills.size()) {
		throw PreparationFailed{};
	}
	prepared->bytes = base->bytes + budget.bytes();
	if (prepared->bytes < base->bytes) {
		throw PreparationLimit{};
	}
	return prepared;
}

struct CacheKey {
	std::uint64_t commandHash = 0;
	std::uint64_t strokeHash = 0;
	std::optional<double> strokeWidth{};
	double scale = 1;
};

struct CacheEntry {
	CacheKey key{};
	std::shared_ptr<const FillBase> base{};
	std::shared_ptr<const PreparedSymbol> prepared{};
	std::uint64_t age = 0;
};

[[nodiscard]] auto sameStroke(const std::optional<double> &left, const std::optional<double> &right)
	-> bool {
	if (left.has_value() != right.has_value()) {
		return false;
	}
	return !left.has_value() || std::is_eq(*left <=> *right);
}

[[nodiscard]] auto sameKey(const CacheKey &key, const CacheKey &other) -> bool {
	return key.commandHash == other.commandHash && key.strokeHash == other.strokeHash &&
		sameStroke(key.strokeWidth, other.strokeWidth) && std::is_eq(key.scale <=> other.scale);
}

void bumpAge(std::uint64_t &age) {
	if (age == std::numeric_limits<std::uint64_t>::max()) {
		throw PreparationLimit{};
	}
	++age;
}

[[nodiscard]] auto qualityBucket(double scale) -> std::expected<double, SymbolFailureCode> {
	if (!std::isfinite(scale) || scale < 0) {
		return std::unexpected(SymbolFailureCode::resourceLimitExceeded);
	}
	if (!(scale > 0)) {
		return 1.0;
	}
	const double quality = std::exp2(std::ceil(std::log2(scale)));
	if (!std::isfinite(quality)) {
		return std::unexpected(SymbolFailureCode::resourceLimitExceeded);
	}
	return quality;
}

class SymbolCache {
  public:
	[[nodiscard]] auto prepare(const SymbolLeaf &symbol, double deviceScale)
		-> std::expected<std::shared_ptr<const PreparedSymbol>, SymbolFailureCode> {
		const auto quality = qualityBucket(deviceScale);
		if (!quality) {
			return std::unexpected(quality.error());
		}
		if (symbol.strokeWidth && !std::isfinite(*symbol.strokeWidth)) {
			return std::unexpected(SymbolFailureCode::preparationFailed);
		}
		try {
			const std::scoped_lock lock{mutex_};
			bumpAge(age_);
			const CacheKey key{
				.commandHash = symbol.commandHash,
				.strokeHash = symbol.strokeHash,
				.strokeWidth = symbol.strokeWidth,
				.scale = *quality
			};
			if (auto found = find(key)) {
				return found;
			}
			auto base = findBase(symbol.commandHash, *quality);
			if (!base) {
				base = buildBase(symbol, *quality);
			}
			auto prepared = buildPrepared(symbol, *quality, base);
			store(key, std::move(base), prepared);
			return prepared;
		} catch (const PreparationLimit &) {
			return std::unexpected(SymbolFailureCode::resourceLimitExceeded);
		} catch (const PreparationFailed &) {
			return std::unexpected(SymbolFailureCode::preparationFailed);
		}
	}

  private:
	[[nodiscard]] auto find(const CacheKey &key) -> std::shared_ptr<const PreparedSymbol> {
		for (auto &entry : entries_) {
			if (!sameKey(entry.key, key)) {
				continue;
			}
			entry.age = age_;
			return entry.prepared;
		}
		return {};
	}

	[[nodiscard]] auto findBase(std::uint64_t commandHash, double scale)
		-> std::shared_ptr<const FillBase> {
		for (const auto &entry : entries_) {
			if (entry.key.commandHash == commandHash && std::is_eq(entry.key.scale <=> scale) &&
				entry.base) {
				return entry.base;
			}
		}
		return {};
	}

	void store(
		const CacheKey &key,
		std::shared_ptr<const FillBase> base,
		const std::shared_ptr<const PreparedSymbol> &prepared
	) {
		if (!prepared || prepared->bytes > cacheByteLimit) {
			return;
		}
		while (entries_.size() >= cacheEntryLimit || bytes_ > cacheByteLimit - prepared->bytes) {
			if (entries_.empty()) {
				break;
			}
			const auto oldest = std::ranges::min_element(
				entries_,
				[](const CacheEntry &left, const CacheEntry &right) -> bool {
					return left.age < right.age;
				}
			);
			const std::size_t occupied = oldest->prepared ? oldest->prepared->bytes : 0;
			if (bytes_ >= occupied) {
				bytes_ -= occupied;
			} else {
				bytes_ = 0;
			}
			entries_.erase(oldest);
		}
		bytes_ += prepared->bytes;
		entries_.push_back(
			{.key = key, .base = std::move(base), .prepared = prepared, .age = age_}
		);
	}

	std::mutex mutex_{};
	std::vector<CacheEntry> entries_{};
	std::size_t bytes_ = 0;
	std::uint64_t age_ = 0;
};

[[nodiscard]] auto cache() -> SymbolCache & {
	static SymbolCache instance;
	return instance;
}

[[nodiscard]] auto maximumScale(CGAffineTransform transform) -> double {
	const double dominant = std::max(
		{std::abs(transform.a), std::abs(transform.b), std::abs(transform.c), std::abs(transform.d)}
	);
	if (!(dominant > 0)) {
		return 0;
	}
	const double horizontalX = transform.a / dominant;
	const double verticalX = transform.b / dominant;
	const double horizontalY = transform.c / dominant;
	const double verticalY = transform.d / dominant;
	const double trace = horizontalX * horizontalX + verticalX * verticalX +
		horizontalY * horizontalY + verticalY * verticalY;
	const double determinant = horizontalX * verticalY - verticalX * horizontalY;
	const double discriminant = trace * trace - singularTermCount * determinant * determinant;
	return dominant * std::sqrt((trace + std::sqrt(std::max(0.0, discriminant))) * midpointFactor);
}

[[nodiscard]] auto finiteAffine(const SymbolAffine &transform) -> bool {
	return std::isfinite(transform.a) && std::isfinite(transform.b) && std::isfinite(transform.c) &&
		std::isfinite(transform.d) && std::isfinite(transform.tx) && std::isfinite(transform.ty);
}

[[nodiscard]] auto finiteTransform(CGAffineTransform transform) -> bool {
	return std::isfinite(transform.a) && std::isfinite(transform.b) && std::isfinite(transform.c) &&
		std::isfinite(transform.d) && std::isfinite(transform.tx) && std::isfinite(transform.ty);
}

void concatenate(CGContextRef context, const SymbolAffine &transform) {
	CGContextConcatCTM(
		context,
		CGAffineTransformMake(
			transform.a,
			transform.b,
			transform.c,
			transform.d,
			transform.tx,
			transform.ty
		)
	);
}

void fillColor(CGContextRef context, const SceneColor &color) {
	CGContextSetRGBFillColor(context, color.red, color.green, color.blue, color.alpha);
}

void fillPrepared(CGContextRef context, CGPathRef path, SymbolFillRule rule) {
	CGContextAddPath(context, path);
	switch (rule) {
	case SymbolFillRule::evenodd:
		CGContextEOFillPath(context);
		break;
	case SymbolFillRule::nonzero:
		CGContextFillPath(context);
		break;
	}
}

[[nodiscard]] auto auditSymbol(const SymbolLeaf &symbol, const PreparedSymbol &prepared)
	-> std::optional<SymbolFailureCode> {
	struct Auditor {
		const SymbolLeaf *symbol = nullptr;
		const PreparedSymbol *prepared = nullptr;
		std::size_t index = 0;
		std::optional<SymbolFailureCode> failure{};

		void visit(const std::vector<SymbolNode> &nodes) {
			if (failure) {
				return;
			}
			for (const auto &node : nodes) {
				if (failure) {
					return;
				}
				if (const auto *group = std::get_if<SymbolNode::Group>(&node.value)) {
					if (!finiteAffine(group->transform) || !std::isfinite(group->opacity)) {
						failure = SymbolFailureCode::preparationFailed;
						return;
					}
					visit(group->children);
					continue;
				}
				const auto &path = std::get<SymbolNode::Path>(node.value);
				if (index >= prepared->paths.size()) {
					failure = SymbolFailureCode::renderingFailed;
					return;
				}
				const auto &graphic = prepared->paths.at(index);
				++index;
				if (!graphic.fill || graphic.fill->get() == nullptr ||
					!finiteAffine(path.transform) || !std::isfinite(path.opacity)) {
					failure = SymbolFailureCode::renderingFailed;
					return;
				}
				const auto plan = planStroke(*symbol, path);
				if (plan.request == StrokeRequest::invalid) {
					failure = SymbolFailureCode::preparationFailed;
					return;
				}
				const bool outlined = graphic.stroke && graphic.stroke->get() != nullptr;
				if ((plan.request == StrokeRequest::paint) != outlined) {
					failure = SymbolFailureCode::preparationFailed;
					return;
				}
			}
		}
	};

	Auditor auditor{.symbol = &symbol, .prepared = &prepared};
	auditor.visit(symbol.elements);
	if (auditor.failure) {
		return auditor.failure;
	}
	if (auditor.index != prepared.paths.size()) {
		return SymbolFailureCode::renderingFailed;
	}
	return std::nullopt;
}

[[nodiscard]] auto
drawPrepared(CGContextRef context, const SymbolLeaf &symbol, const PreparedSymbol &prepared)
	-> bool {
	struct Drawer {
		CGContextRef context = nullptr;
		const PreparedSymbol *prepared = nullptr;
		std::size_t index = 0;
		bool failed = false;

		void visit(const std::vector<SymbolNode> &nodes) {
			if (failed) {
				return;
			}
			for (const auto &node : nodes) {
				if (failed) {
					return;
				}
				CGContextSaveGState(context);
				if (const auto *group = std::get_if<SymbolNode::Group>(&node.value)) {
					concatenate(context, group->transform);
					CGContextSetAlpha(context, group->opacity);
					CGContextBeginTransparencyLayer(context, nullptr);
					visit(group->children);
					CGContextEndTransparencyLayer(context);
				} else if (index >= prepared->paths.size()) {
					failed = true;
				} else {
					const auto &path = std::get<SymbolNode::Path>(node.value);
					const auto &graphic = prepared->paths.at(index);
					++index;
					concatenate(context, path.transform);
					CGContextSetAlpha(context, path.opacity);
					CGContextBeginTransparencyLayer(context, nullptr);
					if (path.fill && graphic.fill && graphic.fill->get() != nullptr) {
						fillColor(context, *path.fill);
						fillPrepared(context, graphic.fill->get(), path.fillRule);
					}
					if (path.stroke && graphic.stroke && graphic.stroke->get() != nullptr) {
						fillColor(context, *path.stroke);
						CGContextAddPath(context, graphic.stroke->get());
						CGContextFillPath(context);
					}
					CGContextEndTransparencyLayer(context);
				}
				CGContextRestoreGState(context);
			}
		}
	};

	Drawer drawer{.context = context, .prepared = &prepared};
	CGContextSaveGState(context);
	concatenate(context, symbol.transform);
	if (!finiteTransform(CGContextGetCTM(context))) {
		CGContextRestoreGState(context);
		return false;
	}
	const bool isolate = !std::is_eq(symbol.opacity <=> unitOpacity);
	if (isolate) {
		CGContextSetAlpha(context, symbol.opacity);
		CGContextBeginTransparencyLayer(context, nullptr);
	}
	drawer.visit(symbol.elements);
	if (isolate) {
		CGContextEndTransparencyLayer(context);
	}
	CGContextRestoreGState(context);
	return !drawer.failed;
}
} // namespace

auto drawSymbol(
	CGContextRef context,
	const SymbolLeaf &symbol,
	const std::function<void(SymbolFailure)> &report,
	std::uint64_t epoch
) -> bool {
	const auto fail = [&](SymbolFailureCode code) -> bool {
		if (report) {
			report(
				{.code = code,
					.instance = symbol.instance,
					.generation = symbol.generation,
					.epoch = epoch}
			);
		}
		return false;
	};
	if (symbol.preparationLimited) {
		return fail(SymbolFailureCode::resourceLimitExceeded);
	}
	if (!std::isfinite(symbol.opacity) || !std::isfinite(symbol.preparationScale) ||
		!finiteAffine(symbol.transform)) {
		return fail(SymbolFailureCode::preparationFailed);
	}
	if (context == nullptr) {
		return fail(SymbolFailureCode::renderingFailed);
	}
	const double deviceScale = maximumScale(CGContextGetCTM(context)) * symbol.preparationScale;
	const auto prepared = cache().prepare(symbol, deviceScale);
	if (!prepared) {
		return fail(prepared.error());
	}
	if (const auto failure = auditSymbol(symbol, **prepared)) {
		return fail(*failure);
	}
	if (!drawPrepared(context, symbol, **prepared)) {
		return fail(SymbolFailureCode::renderingFailed);
	}
	if (report) {
		report({.instance = symbol.instance, .generation = symbol.generation, .epoch = epoch});
	}
	return true;
}
} // namespace stana::platform::detail
