#ifndef STANA_SHADOW_FIXTURE_HPP
#define STANA_SHADOW_FIXTURE_HPP
#include "../../../src/platform/native_contracts.hpp"
#include <memory>
#include <vector>

namespace stana::testing::fixtures::platform::macos {
[[nodiscard]] auto shadowScenes(bool explicitDrawing = false)
	-> std::vector<std::shared_ptr<const stana::platform::detail::Scene>>;
}
#endif
