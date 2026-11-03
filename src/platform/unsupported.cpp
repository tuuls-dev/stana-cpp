#include "native_contracts.hpp"
#include <stdexcept>

namespace stana::native {
auto diagnostics() -> std::vector<WindowStatistics> {
	return {};
}

auto run(std::vector<Window>) -> std::expected<int, std::string> {
	return std::unexpected("Native windows are currently implemented only on macOS 26+");
}

void stop() {
}

void postAfter(std::chrono::milliseconds, std::function<void()>) {
	throw std::runtime_error("No native UI executor on this platform");
}

auto measureText(const std::string &, FontRequest, double) -> std::pair<double, double> {
	throw std::runtime_error("Native text measurement is unavailable on this platform");
}
} // namespace stana::native
