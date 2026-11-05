#include <cstddef>
#include <vector>
import stana;

auto main() -> int {
	const auto rejected = stana::GPUShader::metalLibrary(std::vector<std::byte>{});
	static_cast<void>(rejected);
}
