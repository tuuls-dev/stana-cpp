#ifndef STANA_CANVAS_FIXTURE_HPP
#define STANA_CANVAS_FIXTURE_HPP
#include "../../../src/platform/gpu_contracts.hpp"
#include <memory>
#include <vector>

namespace stana::native::fixtures {
[[nodiscard]] auto counterPrograms() -> std::vector<std::shared_ptr<const gpu::Program>>;
}
#endif
