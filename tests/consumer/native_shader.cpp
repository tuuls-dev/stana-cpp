#include <string>
import stana;

auto main() -> int {
	const auto rejected = stana::GPUShader::metalSource(std::string{"native source"});
	static_cast<void>(rejected);
}
