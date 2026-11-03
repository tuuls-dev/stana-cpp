#include <string_view>

import stana;

auto main() -> int {
	using namespace std::string_view_literals;
	static_assert(noexcept(stana::version()));
	return stana::version() == "0.1.0"sv ? 0 : 1;
}
