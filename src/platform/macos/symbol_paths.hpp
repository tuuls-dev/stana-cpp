#ifndef STANA_MACOS_SYMBOL_PATHS_HPP
#define STANA_MACOS_SYMBOL_PATHS_HPP

#include "../native_contracts.hpp"

#include <functional>

#import <CoreGraphics/CoreGraphics.h>

namespace stana::platform::detail {
/// @brief Fills one symbol in the current Core Graphics context.
/// Geometry is cached without tint, translation, rotation, or opacity.
/// @param epoch Renderer epoch stamped on the result so a replaced surface can retry.
/// @return False when preparation or drawing failed. The report receives that failure.
/// Success reports an empty code and returns true.
[[nodiscard]] auto drawSymbol(
	CGContextRef context,
	const SymbolLeaf &symbol,
	const std::function<void(SymbolFailure)> &report,
	std::uint64_t epoch
) -> bool;
} // namespace stana::platform::detail
#endif
