#ifndef STANA_SHADOW_FIXTURE_HPP
#define STANA_SHADOW_FIXTURE_HPP
#include "../../../src/platform/native_contracts.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace stana::testing::fixtures::platform::macos {
enum class ShadowSceneKind : std::uint8_t { retained, explicitDrawing, mixed };
[[nodiscard]] auto shadowScenes(ShadowSceneKind kind = ShadowSceneKind::retained)
	-> std::vector<std::shared_ptr<const stana::platform::detail::Scene>>;
} // namespace stana::testing::fixtures::platform::macos
#endif
