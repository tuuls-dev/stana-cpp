module;

#include <algorithm>
#include <compare>
#include <cstdint>
#include <optional>

export module stana:layout;
import :primitives;
import :reactive;

export namespace stana::layout {
/// @brief Shared box-model overrides accepted by visual components.
///
/// Pass this aggregate as .layout = {.padding = ..., .frame = ...}. Values may be
/// constant, Signal or Computed inputs. Omitted fields preserve component defaults;
/// Insets{} explicitly requests zero padding. Mounting subscribes in the node's
/// scope, so later updates remeasure/reposition retained nodes without remounting.
struct LayoutProperties {
	/// @brief Optional reactive content inset. Omission keeps control defaults; Insets{} clears
	/// them.
	std::optional<::stana::reactive::Property<Insets>> padding{};
	/// @brief Optional reactive axis sizes and constraints consumed by the layout projection.
	std::optional<::stana::reactive::Property<Frame>> frame{};
};

namespace detail {
/// @brief Complete cache key, including both proposal dimensions and provider generation.
struct MeasureKey {
	Size available{};
	std::uint64_t localInputs = 0;
	std::uint64_t descendantInputs = 0;
	std::uint64_t textEnvironment = 0;
	auto operator<=>(const MeasureKey &) const = default;
};

struct ArrangeKey {
	Rect assigned{};
	std::uint64_t placementInputs = 0;
	std::uint64_t childMeasurements = 0;
	Direction direction = Direction::leftToRight;
	auto operator<=>(const ArrangeKey &) const = default;
};

/// @brief The direction-resolved content box shared by text and both canvas projections.
[[nodiscard]] inline auto contentRect(Rect outer, Insets padding, Direction direction) -> Rect {
	return {
		.origin =
			{.x = outer.origin.x +
					(direction == Direction::leftToRight ? padding.leading : padding.trailing),
				.y = outer.origin.y + padding.top},
		.size = {
			.width = std::max(0.0, outer.size.width - padding.leading - padding.trailing),
			.height = std::max(0.0, outer.size.height - padding.top - padding.bottom)
		}
	};
}

} // namespace detail
} // namespace stana::layout
