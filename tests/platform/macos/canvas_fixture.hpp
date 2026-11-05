#ifndef STANA_CANVAS_FIXTURE_HPP
#define STANA_CANVAS_FIXTURE_HPP
#include "../../../src/platform/native_contracts.hpp"
#include <memory>
#include <vector>

namespace stana::testing::fixtures::platform::macos {
[[nodiscard]] auto mixedScene(bool extended = false)
	-> std::shared_ptr<const stana::platform::detail::Scene>;
[[nodiscard]] auto builtinScenes()
	-> std::vector<std::shared_ptr<const stana::platform::detail::Scene>>;
} // namespace stana::testing::fixtures::platform::macos
#endif
