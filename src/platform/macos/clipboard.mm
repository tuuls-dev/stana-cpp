#include "../text_contracts.hpp"
#import <AppKit/AppKit.h>

namespace stana::platform::detail {
auto readTextClipboard() -> std::expected<std::optional<std::string>, std::string> {
	@autoreleasepool {
		NSString *const value =
			[[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
		if (value == nil) {
			return std::optional<std::string>{};
		}
		NSData *const bytes = [value dataUsingEncoding:NSUTF8StringEncoding];
		if (bytes == nil) {
			return std::unexpected("Clipboard UTF-8 conversion failed");
		}
		return std::optional<std::string>{
			std::string{static_cast<const char *>(bytes.bytes), bytes.length}
		};
	}
}

auto writeTextClipboard(const std::string &text) -> std::expected<void, std::string> {
	@autoreleasepool {
		NSString *const value = [[NSString alloc] initWithBytes:text.data()
														 length:text.size()
													   encoding:NSUTF8StringEncoding];
		if (value == nil) {
			return std::unexpected("Invalid clipboard UTF-8");
		}
		NSPasteboard *const clipboard = [NSPasteboard generalPasteboard];
		[clipboard clearContents];
		if (![clipboard setString:value forType:NSPasteboardTypeString]) {
			return std::unexpected("Clipboard write failed");
		}
		return {};
	}
}
} // namespace stana::platform::detail
