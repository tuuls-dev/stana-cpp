#ifndef STANA_MACOS_TEXT_LAYOUT_HPP
#define STANA_MACOS_TEXT_LAYOUT_HPP
#include "renderer.hpp"
#import <AppKit/AppKit.h>

namespace stana::platform::detail {
/// @brief One font resolver shared by line measurement, immutable resources and legacy paint.
[[nodiscard]] auto resolveTextFont(const FontRequest &font) -> NSFont *;
} // namespace stana::platform::detail
#endif
