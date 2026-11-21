#include "text_layout.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stana::platform::detail {
namespace {
[[nodiscard]] auto nativeString(const std::string &text) -> NSString * {
	return [[NSString alloc] initWithBytes:text.data()
									length:text.size()
								  encoding:NSUTF8StringEncoding];
}
} // namespace

auto resolveTextFont(const FontRequest &request) -> NSFont * {
	NSFont *font = [NSFont systemFontOfSize:request.size weight:request.weight];
	if (request.family) {
		NSString *const family = nativeString(*request.family);
		if (family == nil) {
			throw std::invalid_argument("Invalid font family");
		}
		const auto descriptor = [NSFontDescriptor fontDescriptorWithFontAttributes:@{
			NSFontFamilyAttribute : family,
			NSFontTraitsAttribute : @{NSFontWeightTrait : @(request.weight)}
		}];
		NSFont *const custom = [NSFont fontWithDescriptor:descriptor size:request.size];
		if (custom != nil) {
			font = custom;
		}
	}
	if (request.italic) {
		NSFont *const italic = [[NSFontManager sharedFontManager] convertFont:font
																  toHaveTrait:NSItalicFontMask];
		if (italic != nil) {
			font = italic;
		}
	}
	return font;
}

auto shapeTextLine(const std::string &text, const FontRequest &font, bool rightToLeft)
	-> std::expected<NativeTextLine, std::string> {
	@autoreleasepool {
		if (!std::isfinite(font.size) || font.size < 0 || !std::isfinite(font.weight)) {
			return std::unexpected("Invalid font request");
		}
		if (font.size <= 0) {
			return NativeTextLine{};
		}
		NSString *const value = nativeString(text);
		if (value == nil) {
			return std::unexpected("Invalid UTF-8 text");
		}
		try {
			const auto resource = std::make_shared<PreparedText>(Command{
				.text = text,
				.fontSize = font.size,
				.fontFamily = font.family,
				.fontItalic = font.italic,
				.textRightToLeft = rightToLeft,
				.fontWeight = font.weight
			});
			CGFloat ascent = 0, descent = 0, leading = 0;
			const double width =
				CTLineGetTypographicBounds(resource->line.get(), &ascent, &descent, &leading);
			NSFont *const resolved = resolveTextFont(font);
			NativeTextLine result{
				.width = width,
				.ascent = ascent,
				.descent = descent,
				.leading = leading,
				.xHeight = resolved.xHeight,
				.underlineThickness = std::max(1.0, resolved.underlineThickness),
				.resource = resource
			};
			for (NSUInteger offset = 0; offset <= value.length; ++offset) {
				if (offset < value.length &&
					(CFStringIsSurrogateLowCharacter([value characterAtIndex:offset]) != 0u)) {
					continue;
				}
				CGFloat secondary = 0;
				const auto primary = CTLineGetOffsetForStringIndex(
					resource->line.get(),
					static_cast<CFIndex>(offset),
					&secondary
				);
				result.carets.push_back(
					{.utf16Offset = offset, .primary = primary, .secondary = secondary}
				);
			}
			CFArrayRef const runs = CTLineGetGlyphRuns(resource->line.get());
			for (CFIndex index = 0; index < CFArrayGetCount(runs); ++index) {
				const auto run = static_cast<CTRunRef>(CFArrayGetValueAtIndex(runs, index));
				const auto range = CTRunGetStringRange(run);
				if (range.location < 0 || range.length < 0) {
					return std::unexpected("Invalid native glyph range");
				}
				const auto start = static_cast<std::size_t>(range.location);
				const auto length = static_cast<std::size_t>(range.length);
				if (length > value.length || start > value.length - length) {
					return std::unexpected("Overflowing native glyph range");
				}
				const auto first =
					CTLineGetOffsetForStringIndex(resource->line.get(), range.location, nullptr);
				const auto last = CTLineGetOffsetForStringIndex(
					resource->line.get(),
					range.location + range.length,
					nullptr
				);
				double left = std::min(first, last);
				double right = std::max(first, last);
				const auto count = CTRunGetGlyphCount(run);
				if (count > 0) {
					left = std::numeric_limits<double>::max();
					right = std::numeric_limits<double>::lowest();
					for (CFIndex glyph = 0; glyph < count; ++glyph) {
						CGPoint position{};
						CGSize advance{};
						CTRunGetPositions(run, CFRangeMake(glyph, 1), &position);
						CTRunGetAdvances(run, CFRangeMake(glyph, 1), &advance);
						left = std::min(left, std::min(position.x, position.x + advance.width));
						right = std::max(right, std::max(position.x, position.x + advance.width));
					}
				}
				result.runs.push_back(
					{.start = start,
						.end = start + length,
						.x = left,
						.width = std::max(0.0, right - left),
						.rightToLeft = (CTRunGetStatus(run) & kCTRunStatusRightToLeft) != 0}
				);
			}
			return result;
		} catch (const std::exception &error) {
			return std::unexpected(error.what());
		}
	}
}

} // namespace stana::platform::detail
