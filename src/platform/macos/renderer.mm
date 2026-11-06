#include "renderer.hpp"
#include "symbol_paths.hpp"
#import <AppKit/AppKit.h>
#include <algorithm>
#include <cmath>
#include <compare>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace stana::platform::detail {
namespace {
constexpr std::size_t bitsPerComponent = 8;
constexpr double fallbackLineHeight = 1.4;
constexpr unsigned char firstNonAscii = 128;

[[nodiscard]] auto rectangle(LogicalRect value) -> CGRect {
	return CGRectMake(value.origin.x, value.origin.y, value.size.width, value.size.height);
}

[[nodiscard]] auto logical(CGRect value) -> LogicalRect {
	return {
		.origin = {.x = value.origin.x, .y = value.origin.y},
		.size = {.width = value.size.width, .height = value.size.height}
	};
}

[[nodiscard]] auto intersection(LogicalRect left, LogicalRect right) -> LogicalRect {
	const double x = std::max(left.origin.x, right.origin.x);
	const double y = std::max(left.origin.y, right.origin.y);
	return {
		.origin = {.x = x, .y = y},
		.size = {
			.width = std::max(
				0.0,
				std::min(left.origin.x + left.size.width, right.origin.x + right.size.width) - x
			),
			.height = std::max(
				0.0,
				std::min(left.origin.y + left.size.height, right.origin.y + right.size.height) - y
			)
		}
	};
}

[[nodiscard]] auto clipped(Ink ink, const std::optional<LogicalRect> &clip) -> Ink {
	if (clip && ink.bounds) {
		ink.bounds = intersection(*ink.bounds, *clip);
	}
	return ink;
}

[[nodiscard]] auto unite(Ink left, const Ink &right) -> Ink {
	left.unknown = left.unknown || right.unknown;
	if (!right.bounds) {
		return left;
	}
	if (!left.bounds) {
		left.bounds = right.bounds;
		return left;
	}
	const auto a = *left.bounds;
	const auto b = *right.bounds;
	const double x = std::min(a.origin.x, b.origin.x);
	const double y = std::min(a.origin.y, b.origin.y);
	left.bounds = LogicalRect{
		.origin = {.x = x, .y = y},
		.size = {
			.width = std::max(a.origin.x + a.size.width, b.origin.x + b.size.width) - x,
			.height = std::max(a.origin.y + a.size.height, b.origin.y + b.size.height) - y
		}
	};
	return left;
}

[[nodiscard]] auto expanded(LogicalRect rect, double amount) -> LogicalRect {
	return {
		.origin = {.x = rect.origin.x - amount, .y = rect.origin.y - amount},
		.size = {.width = rect.size.width + 2 * amount, .height = rect.size.height + 2 * amount}
	};
}

[[nodiscard]] auto nativeString(const std::string &text) -> NSString * {
	NSString *const value = [[NSString alloc] initWithBytes:text.data()
													 length:text.size()
												   encoding:NSUTF8StringEncoding];
	if (value == nil) {
		throw std::invalid_argument("Stana text must be valid UTF-8");
	}
	return value;
}

[[nodiscard]] auto attributedText(const Command &command) -> NSAttributedString * {
	NSFont *const font = [NSFont systemFontOfSize:command.fontSize weight:command.fontWeight];
	NSColor *const color = [NSColor colorWithSRGBRed:command.red
											   green:command.green
												blue:command.blue
											   alpha:command.alpha];
	return [[NSAttributedString alloc]
		initWithString:nativeString(command.text)
			attributes:@{NSFontAttributeName : font, NSForegroundColorAttributeName : color}];
}

[[nodiscard]] auto prepareOperation(const Command &command, double scale) -> PreparedOperation {
	const bool emptyStroke = (command.operation == PaintOperation::stroke ||
								 command.operation == PaintOperation::line) &&
		command.lineWidth <= 0;
	const bool emptyRectangle = (command.operation == PaintOperation::fill ||
									command.operation == PaintOperation::stroke) &&
		(command.width <= 0 || command.height <= 0);
	const bool emptyText = command.operation == PaintOperation::text &&
		(command.text.empty() || command.fontSize <= 0);
	if (emptyStroke || emptyRectangle || emptyText) {
		return PreparedOperation{.command = command};
	}

	PreparedOperation operation{.command = command};
	LogicalRect bounds{
		.origin = {.x = command.x, .y = command.y},
		.size = {.width = command.width, .height = command.height}
	};
	double margin = 1 / scale;
	switch (command.operation) {
	case PaintOperation::text: {
		operation.text = std::make_shared<PreparedText>(command);
		const CGRect image = CTLineGetImageBounds(operation.text->line.get(), nullptr);
		// Bitmap/color/fallback glyph metrics are not a portable guaranteed bound.
		// Conservatively use full damage rather than guessing a glyph padding.
		const bool unusual = std::ranges::any_of(command.text, [](char byte) -> bool {
			return static_cast<unsigned char>(byte) >= firstNonAscii;
		});
		if (unusual || CGRectIsNull(image) || CGRectIsInfinite(image)) {
			operation.ink.unknown = !command.text.empty();
		} else {
			bounds = logical(image);
			bounds.origin.x += command.x;
			bounds.origin.y =
				command.y + operation.text->ascent - image.origin.y - image.size.height;
			operation.ink.bounds = expanded(bounds, margin);
		}
		return operation;
	}
	case PaintOperation::line:
		if (bounds.size.width < 0) {
			bounds.origin.x += bounds.size.width;
			bounds.size.width = -bounds.size.width;
		}
		if (bounds.size.height < 0) {
			bounds.origin.y += bounds.size.height;
			bounds.size.height = -bounds.size.height;
		}
		margin += std::max(command.lineWidth / 2, 1 / (2 * scale));
		break;
	case PaintOperation::stroke:
		margin += std::max(command.lineWidth / 2, 1 / (2 * scale));
		operation.path = std::make_shared<PreparedPath>(command);
		break;
	case PaintOperation::fill:
		operation.path = std::make_shared<PreparedPath>(command);
		break;
	case PaintOperation::pushClip:
		operation.path = std::make_shared<PreparedPath>(command);
		return operation;
	case PaintOperation::popClip:
		return operation;
	}
	operation.ink.bounds = expanded(bounds, margin);
	return operation;
}

void prepareOperations(
	const std::vector<Command> &commands,
	double scale,
	std::vector<PreparedOperation> &operations,
	std::vector<std::optional<LogicalRect>> &clips,
	Ink &ink
) {
	for (const auto &command : commands) {
		auto operation = prepareOperation(command, scale);
		if (command.operation == PaintOperation::pushClip) {
			const LogicalRect rect{
				.origin = {.x = command.x, .y = command.y},
				.size = {.width = command.width, .height = command.height}
			};
			clips.emplace_back(clips.back() ? intersection(*clips.back(), rect) : rect);
		} else if (command.operation == PaintOperation::popClip) {
			if (clips.size() <= 1) {
				throw std::invalid_argument("Native clip underflow");
			}
			clips.pop_back();
		} else {
			ink = unite(ink, clipped(operation.ink, clips.back()));
		}
		operations.push_back(std::move(operation));
	}
}

[[nodiscard]] auto prepareBranch(
	const std::shared_ptr<const SceneBranch> &source,
	double scale,
	const std::shared_ptr<const PreparedBranch> &previous,
	PreparationTrace *trace
) -> std::shared_ptr<const PreparedBranch> {
	if (!source) {
		return {};
	}
	if (previous && previous->source->id == source->id &&
		previous->source->generation == source->generation) {
		return previous;
	}
	if (trace != nullptr) {
		trace->branches.push_back(source->id);
	}
	auto result = std::make_shared<PreparedBranch>();
	result->source = source;
	if (previous && previous->paint->identity == source->paint->identity) {
		result->paint = previous->paint;
	} else {
		if (trace != nullptr) {
			trace->paints.push_back(source->paint->identity);
		}
		auto paint = std::make_shared<PreparedPaint>();
		paint->identity = source->paint->identity;
		paint->childClipBounds = source->paint->childClipBounds;
		if (paint->childClipBounds) {
			for (const auto &command : source->paint->prefix) {
				if (command.operation == PaintOperation::pushClip) {
					paint->childClipRadius = command.radius;
					break;
				}
			}
		}
		std::vector<std::optional<LogicalRect>> clips{std::nullopt};
		prepareOperations(source->paint->prefix, scale, paint->prefix, clips, paint->prefixInk);
		prepareOperations(source->paint->suffix, scale, paint->suffix, clips, paint->suffixInk);
		if (clips.size() != 1) {
			throw std::invalid_argument("Native unbalanced clips");
		}
		if (source->paint->gpu) {
			paint->prefixInk = unite(paint->prefixInk, Ink{.bounds = source->paint->gpu->bounds});
		}
		if (source->paint->symbol) {
			paint->prefixInk = unite(paint->prefixInk, Ink{.bounds = source->paint->symbol->ink});
		}
		result->paint = std::move(paint);
	}
	std::map<NodeIdentity, std::shared_ptr<const PreparedBranch>> pairs;
	if (previous) {
		for (const auto &child : previous->children) {
			pairs.emplace(child->source->id, child);
		}
	}
	for (const auto &child : source->children) {
		const auto found = pairs.find(child->id);
		result->children.push_back(
			prepareBranch(child, scale, found == pairs.end() ? nullptr : found->second, trace)
		);
	}
	if (source->visible) {
		result->ink = unite(result->paint->prefixInk, result->paint->suffixInk);
		for (const auto &child : result->children) {
			result->ink = unite(result->ink, clipped(child->ink, result->paint->childClipBounds));
		}
	}
	return result;
}

void addInk(
	const Ink &ink,
	const std::optional<LogicalRect> &clip,
	const TileGrid &grid,
	double scale,
	TileMask &mask,
	RenderingWork &work
) {
	if (ink.unknown) {
		mask |= allTiles(grid);
		++work.fullDamageFallbacks;
		return;
	}
	const auto bounded = clipped(ink, clip);
	if (bounded.bounds) {
		selectTiles(grid, scale, *bounded.bounds, mask);
	}
}

/// @brief GPU leaves share CPU clip/border commands; content identity is the converted drawing.
[[nodiscard]] auto
gpuEquivalent(const std::optional<GPUCanvasLeaf> &left, const std::optional<GPUCanvasLeaf> &right)
	-> bool {
	if (!left.has_value() && !right.has_value()) {
		return true;
	}
	if (!left.has_value() || !right.has_value()) {
		return false;
	}
	return left->drawing == right->drawing && std::is_eq(left->opacity <=> right->opacity) &&
		std::is_eq(left->bounds <=> right->bounds);
}

[[nodiscard]] auto
sameOptionalDouble(const std::optional<double> &left, const std::optional<double> &right) -> bool {
	if (left.has_value() != right.has_value()) {
		return false;
	}
	return !left.has_value() || std::is_eq(*left <=> *right);
}

[[nodiscard]] auto
sameColor(const std::optional<SceneColor> &left, const std::optional<SceneColor> &right) -> bool {
	if (left.has_value() != right.has_value()) {
		return false;
	}
	return !left.has_value() || std::is_eq(*left <=> *right);
}

[[nodiscard]] auto
symbolNodesEquivalent(const std::vector<SymbolNode> &left, const std::vector<SymbolNode> &right)
	-> bool;

[[nodiscard]] auto symbolNodeEquivalent(const SymbolNode &left, const SymbolNode &right) -> bool {
	const auto *leftGroup = std::get_if<SymbolNode::Group>(&left.value);
	const auto *rightGroup = std::get_if<SymbolNode::Group>(&right.value);
	if ((leftGroup == nullptr) != (rightGroup == nullptr)) {
		return false;
	}
	if (leftGroup != nullptr && rightGroup != nullptr) {
		return std::is_eq(leftGroup->transform <=> rightGroup->transform) &&
			std::is_eq(leftGroup->opacity <=> rightGroup->opacity) &&
			symbolNodesEquivalent(leftGroup->children, rightGroup->children);
	}
	const auto &leftPath = std::get<SymbolNode::Path>(left.value);
	const auto &rightPath = std::get<SymbolNode::Path>(right.value);
	return std::is_eq(leftPath.transform <=> rightPath.transform) &&
		std::is_eq(leftPath.opacity <=> rightPath.opacity) &&
		std::is_eq(leftPath.fillOpacity <=> rightPath.fillOpacity) &&
		std::is_eq(leftPath.strokeOpacity <=> rightPath.strokeOpacity) &&
		leftPath.fillRule == rightPath.fillRule && sameColor(leftPath.fill, rightPath.fill) &&
		sameColor(leftPath.stroke, rightPath.stroke);
}

[[nodiscard]] auto
symbolNodesEquivalent(const std::vector<SymbolNode> &left, const std::vector<SymbolNode> &right)
	-> bool {
	if (left.size() != right.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.size(); ++index) {
		if (!symbolNodeEquivalent(left.at(index), right.at(index))) {
			return false;
		}
	}
	return true;
}

/// @brief Animation moves the leaf transform or preparation scale; either one must damage tiles.
[[nodiscard]] auto
symbolEquivalent(const std::optional<SymbolLeaf> &left, const std::optional<SymbolLeaf> &right)
	-> bool {
	if (!left.has_value() && !right.has_value()) {
		return true;
	}
	if (!left.has_value() || !right.has_value()) {
		return false;
	}
	return left->generation == right->generation && left->commandHash == right->commandHash &&
		left->strokeHash == right->strokeHash &&
		std::is_eq(left->preparationScale <=> right->preparationScale) &&
		sameOptionalDouble(left->strokeWidth, right->strokeWidth) &&
		std::is_eq(left->transform <=> right->transform) &&
		std::is_eq(left->opacity <=> right->opacity) &&
		left->preparationLimited == right->preparationLimited &&
		symbolNodesEquivalent(left->elements, right->elements);
}

/// @brief Complete local value comparison handles coalesced A->B->A with fresh paint identities.
/// Symbol geometry, stroke, placement, opacity, and paint colors participate.
[[nodiscard]] auto paintEquivalent(
	const PreparedPaint &left,
	const PreparedPaint &right,
	const std::optional<GPUCanvasLeaf> &leftGpu,
	const std::optional<GPUCanvasLeaf> &rightGpu,
	const std::optional<SymbolLeaf> &leftSymbol,
	const std::optional<SymbolLeaf> &rightSymbol
) -> bool {
	if (left.identity == right.identity) {
		return true;
	}
	const auto sameOperation = [](const PreparedOperation &a, const PreparedOperation &b) -> bool {
		return a.command == b.command;
	};
	return gpuEquivalent(leftGpu, rightGpu) && symbolEquivalent(leftSymbol, rightSymbol) &&
		left.prefix.size() == right.prefix.size() && left.suffix.size() == right.suffix.size() &&
		std::equal(left.prefix.begin(), left.prefix.end(), right.prefix.begin(), sameOperation) &&
		std::equal(left.suffix.begin(), left.suffix.end(), right.suffix.begin(), sameOperation);
}

void diff(
	const std::shared_ptr<const PreparedBranch> &old,
	const std::shared_ptr<const PreparedBranch> &next,
	const std::optional<LogicalRect> &clip,
	const TileGrid &grid,
	double scale,
	TileMask &mask,
	RenderingWork &work
) {
	if (old == next ||
		(old && next && old->source->id == next->source->id &&
			old->source->generation == next->source->generation)) {
		return;
	}
	if (!old || !next || old->source->visible != next->source->visible ||
		old->source->id != next->source->id) {
		if (old) {
			addInk(old->ink, clip, grid, scale, mask, work);
		}
		if (next) {
			addInk(next->ink, clip, grid, scale, mask, work);
		}
		return;
	}
	if (!next->source->visible) {
		return;
	}
	if (!paintEquivalent(
			*old->paint,
			*next->paint,
			old->source->paint->gpu,
			next->source->paint->gpu,
			old->source->paint->symbol,
			next->source->paint->symbol
		)) {
		addInk(unite(old->paint->prefixInk, old->paint->suffixInk), clip, grid, scale, mask, work);
		addInk(
			unite(next->paint->prefixInk, next->paint->suffixInk),
			clip,
			grid,
			scale,
			mask,
			work
		);
		const bool clipChanged =
			std::is_neq(old->paint->childClipRadius <=> next->paint->childClipRadius) ||
			old->paint->childClipBounds.has_value() != next->paint->childClipBounds.has_value() ||
			(old->paint->childClipBounds &&
				std::is_neq(*old->paint->childClipBounds <=> *next->paint->childClipBounds));
		if (clipChanged) {
			addInk(old->ink, clip, grid, scale, mask, work);
			addInk(next->ink, clip, grid, scale, mask, work);
			return;
		}
	}
	const bool sameOrder = old->children.size() == next->children.size() &&
		std::equal(
			old->children.begin(),
			old->children.end(),
			next->children.begin(),
			[](const auto &left, const auto &right) -> bool {
				return left->source->id == right->source->id;
			}
		);
	if (!sameOrder) {
		addInk(old->ink, clip, grid, scale, mask, work);
		addInk(next->ink, clip, grid, scale, mask, work);
		return;
	}
	auto childClip = clip;
	if (next->paint->childClipBounds) {
		childClip = clip ? intersection(*clip, *next->paint->childClipBounds)
						 : next->paint->childClipBounds;
	}
	for (std::size_t index = 0; index < next->children.size(); ++index) {
		diff(old->children.at(index), next->children.at(index), childClip, grid, scale, mask, work);
	}
}

void replayOperation(CGContextRef context, const PreparedOperation &operation) {
	if ((operation.command.operation == PaintOperation::stroke ||
			operation.command.operation == PaintOperation::line) &&
		operation.command.lineWidth <= 0) {
		return;
	}
	if ((operation.command.operation == PaintOperation::fill ||
			operation.command.operation == PaintOperation::stroke) &&
		(operation.command.width <= 0 || operation.command.height <= 0)) {
		return;
	}
	if (operation.command.operation == PaintOperation::text &&
		(operation.command.text.empty() || operation.command.fontSize <= 0)) {
		return;
	}

	const auto &command = operation.command;
	switch (command.operation) {
	case PaintOperation::fill:
		CGContextSetRGBFillColor(context, command.red, command.green, command.blue, command.alpha);
		CGContextAddPath(context, operation.path->path.get());
		CGContextFillPath(context);
		break;
	case PaintOperation::stroke:
		CGContextSetRGBStrokeColor(
			context,
			command.red,
			command.green,
			command.blue,
			command.alpha
		);
		CGContextSetLineWidth(context, command.lineWidth);
		CGContextAddPath(context, operation.path->path.get());
		CGContextStrokePath(context);
		break;
	case PaintOperation::line:
		CGContextSetRGBStrokeColor(
			context,
			command.red,
			command.green,
			command.blue,
			command.alpha
		);
		CGContextSetLineWidth(context, command.lineWidth);
		CGContextMoveToPoint(context, command.x, command.y);
		CGContextAddLineToPoint(context, command.x + command.width, command.y + command.height);
		CGContextStrokePath(context);
		break;
	case PaintOperation::pushClip:
		CGContextSaveGState(context);
		CGContextAddPath(context, operation.path->path.get());
		CGContextClip(context);
		break;
	case PaintOperation::popClip:
		CGContextRestoreGState(context);
		break;
	case PaintOperation::text:
		CGContextSaveGState(context);
		CGContextTranslateCTM(context, command.x, command.y + operation.text->ascent);
		CGContextScaleCTM(context, 1, -1);
		CGContextSetTextPosition(context, 0, 0);
		CTLineDraw(operation.text->line.get(), context);
		CGContextRestoreGState(context);
		break;
	}
}

void replayBranch(
	CGContextRef context,
	const std::shared_ptr<const PreparedBranch> &branch,
	LogicalRect tile,
	bool cull,
	const std::function<void(SymbolFailure)> &symbolError,
	std::uint64_t symbolEpoch
) {
	if (!branch || !branch->source->visible) {
		return;
	}
	if (cull && !branch->ink.unknown &&
		(!branch->ink.bounds ||
			CGRectIsEmpty(rectangle(intersection(*branch->ink.bounds, tile))))) {
		return;
	}
	for (const auto &operation : branch->paint->prefix) {
		replayOperation(context, operation);
	}
	if (branch->source->paint->symbol) {
		static_cast<void>(
			drawSymbol(context, *branch->source->paint->symbol, symbolError, symbolEpoch)
		);
	}
	for (const auto &child : branch->children) {
		replayBranch(context, child, tile, cull, symbolError, symbolEpoch);
	}
	for (const auto &operation : branch->paint->suffix) {
		replayOperation(context, operation);
	}
}
} // namespace

PreparedText::PreparedText(const Command &command)
	: line(
		  CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)attributedText(command))
	  ) {
	if (line.get() == nullptr) {
		throw std::runtime_error("Unable to prepare Core Text line");
	}
	CGFloat baseline = 0;
	CTLineGetTypographicBounds(line.get(), &baseline, nullptr, nullptr);
	ascent = baseline;
}

PreparedPath::PreparedPath(const Command &command)
	: path(CGPathCreateWithRoundedRect(
		  CGRectMake(command.x, command.y, command.width, command.height),
		  command.radius,
		  command.radius,
		  nullptr
	  )) {
	if (path.get() == nullptr) {
		throw std::runtime_error("Unable to prepare CoreGraphics path");
	}
}

auto prepareScene(
	std::shared_ptr<const Scene> source,
	double scale,
	const std::shared_ptr<const PreparedScene> &previous,
	PreparationTrace *trace
) -> std::shared_ptr<const PreparedScene> {
	if (!source || !std::isfinite(scale) || scale <= 0) {
		throw std::invalid_argument("Invalid prepared scene surface");
	}
	if (previous && previous->source == source && std::is_eq(previous->scale <=> scale)) {
		return previous;
	}
	auto scene = std::make_shared<PreparedScene>();
	scene->source = std::move(source);
	scene->scale = scale;
	const bool reusable = previous && std::is_eq(previous->scale <=> scale) &&
		previous->source->textEnvironment == scene->source->textEnvironment;
	scene->root =
		prepareBranch(scene->source->root, scale, reusable ? previous->root : nullptr, trace);
	return scene;
}

auto sceneDamage(
	const std::shared_ptr<const PreparedScene> &before,
	const std::shared_ptr<const PreparedScene> &after,
	const TileGrid &grid,
	RenderingWork &work
) -> TileMask {
	if (!after) {
		throw std::invalid_argument("Missing damage target");
	}
	if (!before || before->source->textEnvironment != after->source->textEnvironment ||
		std::is_neq(before->scale <=> after->scale) ||
		std::is_neq(before->source->size <=> after->source->size) ||
		std::is_neq(before->source->background <=> after->source->background)) {
		return allTiles(grid);
	}
	TileMask mask;
	diff(before->root, after->root, std::nullopt, grid, after->scale, mask, work);
	return mask;
}

void rasterScene(
	CGContextRef context,
	const PreparedScene &scene,
	PixelRect pixels,
	bool cullBranches,
	std::uint64_t symbolEpoch,
	const std::function<void(SymbolFailure)> &symbolReport
) {
	if (context == nullptr) {
		throw std::invalid_argument("Missing raster context");
	}
	CGContextSaveGState(context);
	const auto color = scene.source->background;
	CGContextSetRGBFillColor(context, color.red, color.green, color.blue, 1);
	CGContextFillRect(
		context,
		CGRectMake(0, 0, static_cast<double>(pixels.width), static_cast<double>(pixels.height))
	);
	CGContextTranslateCTM(context, 0, static_cast<double>(pixels.height));
	CGContextScaleCTM(context, scene.scale, -scene.scale);
	CGContextTranslateCTM(
		context,
		-static_cast<double>(pixels.x) / scene.scale,
		-static_cast<double>(pixels.y) / scene.scale
	);
	const LogicalRect tile{
		.origin =
			{.x = static_cast<double>(pixels.x) / scene.scale,
				.y = static_cast<double>(pixels.y) / scene.scale},
		.size = {
			.width = static_cast<double>(pixels.width) / scene.scale,
			.height = static_cast<double>(pixels.height) / scene.scale
		}
	};
	const auto &report = symbolReport ? symbolReport : scene.source->symbolError;
	replayBranch(context, scene.root, tile, cullBranches, report, symbolEpoch);
	CGContextRestoreGState(context);
	CGContextFlush(context);
}

auto rasterSymbol(
	CGContextRef context,
	double scale,
	PixelRect pixels,
	const std::vector<PreparedOperation> &clips,
	const SymbolLeaf &symbol,
	std::uint64_t epoch,
	const std::function<void(SymbolFailure)> &report
) -> bool {
	if (context == nullptr) {
		return drawSymbol(context, symbol, report, epoch);
	}
	CGContextSaveGState(context);
	CGContextClearRect(
		context,
		CGRectMake(0, 0, static_cast<double>(pixels.width), static_cast<double>(pixels.height))
	);
	CGContextTranslateCTM(context, 0, static_cast<double>(pixels.height));
	CGContextScaleCTM(context, scale, -scale);
	CGContextTranslateCTM(
		context,
		-static_cast<double>(pixels.x) / scale,
		-static_cast<double>(pixels.y) / scale
	);
	for (const auto &clip : clips) {
		replayOperation(context, clip);
	}
	const bool painted = drawSymbol(context, symbol, report, epoch);
	for (std::size_t index = 0; index < clips.size(); ++index) {
		static_cast<void>(index);
		CGContextRestoreGState(context);
	}
	CGContextRestoreGState(context);
	CGContextFlush(context);
	return painted;
}

void rasterRun(
	CGContextRef context,
	double scale,
	PixelRect pixels,
	const std::vector<PreparedOperation> &operations
) {
	CGContextSaveGState(context);
	CGContextClearRect(
		context,
		CGRectMake(0, 0, static_cast<double>(pixels.width), static_cast<double>(pixels.height))
	);
	CGContextTranslateCTM(context, 0, static_cast<double>(pixels.height));
	CGContextScaleCTM(context, scale, -scale);
	CGContextTranslateCTM(
		context,
		-static_cast<double>(pixels.x) / scale,
		-static_cast<double>(pixels.y) / scale
	);
	for (const auto &operation : operations) {
		replayOperation(context, operation);
	}
	CGContextRestoreGState(context);
	CGContextFlush(context);
}

RasterTile::RasterTile(
	TileLayout value,
	id<MTLDevice> device,
	std::shared_ptr<RasterAccounting> owner
)
	: layout(value),
	  accounting(std::move(owner)) {
	if (layout.allocationBytes > device.maxBufferLength ||
		layout.allocationBytes > maximumRasterBytes - accounting->bytes.load()) {
		throw std::runtime_error("Shared raster allocation budget exceeded");
	}
	const auto reserved = stana::gpu::detail::AllocationLease::reserve(
		accounting->deviceBudget,
		accounting->windowBudget,
		layout.allocationBytes
	);
	if (!reserved) {
		throw std::runtime_error(reserved.error());
	}
	combinedLease = *reserved;
	buffer = [device newBufferWithLength:layout.allocationBytes
								 options:(device.hasUnifiedMemory ? MTLResourceStorageModeShared
																  : MTLResourceStorageModeManaged) |
		MTLResourceCPUCacheModeDefaultCache];
	if (buffer == nil || buffer.contents == nullptr) {
		throw std::runtime_error("Unable to allocate shared Metal tile buffer");
	}
	++accounting->allocations;
	const CoreOwner colorspace{CGColorSpaceCreateWithName(kCGColorSpaceSRGB)};
	const auto info = static_cast<CGBitmapInfo>(
		static_cast<std::uint32_t>(kCGImageAlphaPremultipliedFirst) |
		static_cast<std::uint32_t>(kCGBitmapByteOrder32Little)
	);
	bitmapContext = CGBitmapContextCreate(
		buffer.contents,
		layout.pixels.width,
		layout.pixels.height,
		bitsPerComponent,
		layout.bytesPerRow,
		colorspace.get(),
		info
	);
	if (bitmapContext == nullptr) {
		throw std::runtime_error("Unable to create shared tile bitmap context");
	}
	const auto current =
		accounting->bytes.fetch_add(layout.allocationBytes) + layout.allocationBytes;
	auto peak = accounting->peak.load();
	while (current > peak && !accounting->peak.compare_exchange_weak(peak, current)) {
	}
}

RasterTile::~RasterTile() {
	if (bitmapContext != nullptr) {
		CGContextRelease(bitmapContext);
		accounting->bytes.fetch_sub(layout.allocationBytes);
	}
}

void encodeTile(id<MTLBlitCommandEncoder> encoder, const RasterTile &tile, id<MTLTexture> texture) {
	if (tile.cpuDirty && tile.buffer.storageMode == MTLStorageModeManaged) {
		[tile.buffer didModifyRange:NSMakeRange(0, tile.layout.allocationBytes)];
		tile.accounting->managedWriteBytes.fetch_add(tile.layout.allocationBytes);
	}

	tile.cpuDirty = false;
	if (encoder == nil || texture == nil || texture.width != tile.layout.pixels.width ||
		texture.height != tile.layout.pixels.height ||
		texture.pixelFormat != MTLPixelFormatBGRA8Unorm || texture.device != tile.buffer.device) {
		throw std::runtime_error("Tile drawable surface mismatch");
	}
	[encoder copyFromBuffer:tile.buffer
			   sourceOffset:0
		  sourceBytesPerRow:tile.layout.bytesPerRow
		sourceBytesPerImage:0
				 sourceSize:MTLSizeMake(tile.layout.pixels.width, tile.layout.pixels.height, 1)
				  toTexture:texture
		   destinationSlice:0
		   destinationLevel:0
		  destinationOrigin:MTLOriginMake(0, 0, 0)];
}

auto measureText(const std::string &text, FontRequest font, double maximumWidth)
	-> std::pair<double, double> {
	@autoreleasepool {
		const PreparedText prepared{
			Command{.text = text, .fontSize = font.size, .fontWeight = font.weight}
		};
		CGFloat ascent = 0;
		CGFloat descent = 0;
		CGFloat leading = 0;
		const double width =
			CTLineGetTypographicBounds(prepared.line.get(), &ascent, &descent, &leading);
		return {
			std::min(maximumWidth, std::ceil(width)),
			std::max(std::ceil(ascent + descent + leading), font.size * fallbackLineHeight)
		};
	}
}
} // namespace stana::platform::detail
