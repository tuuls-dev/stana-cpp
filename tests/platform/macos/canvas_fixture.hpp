#ifndef STANA_CANVAS_FIXTURE_HPP
#define STANA_CANVAS_FIXTURE_HPP
#include "../../../src/platform/native_contracts.hpp"
#include <memory>
#include <vector>

namespace stana::native::fixtures {
[[nodiscard]] auto mixedScene(bool extended = false) -> std::shared_ptr<const Scene>;
[[nodiscard]] auto counterPrograms() -> std::vector<std::shared_ptr<const gpu::Program>>;
} // namespace stana::native::fixtures
#endif
