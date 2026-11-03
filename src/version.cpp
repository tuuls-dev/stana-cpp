module;

#include <string_view>

module stana;

namespace stana {

auto version() noexcept -> std::string_view {
	return "0.1.0";
}

} // namespace stana
