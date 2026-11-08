#include "native_contracts.hpp"
#include <stdexcept>

namespace stana::platform::detail {
namespace {
[[noreturn]] void unavailable(const char *message) {
	throw std::runtime_error(message);
}
} // namespace

auto diagnostics() -> std::vector<WindowStatistics> {
	return {};
}

auto run(const std::vector<Window> &) -> std::expected<int, std::string> {
	return std::unexpected("Native windows are currently implemented only on macOS 26+");
}

void stop() {
}

void postAfter(std::chrono::milliseconds, const std::function<void()> &) {
	// Headless Linux builds have no UI executor; scheduled work is dropped.
}

auto createMailboxWakeup(const std::function<void()> &)
	-> std::expected<MailboxWakeup, std::string> {
	return std::unexpected("No native UI executor on this platform");
}

auto measureText(const std::string &, FontRequest, double) -> std::pair<double, double> {
	unavailable("Native text measurement is unavailable on this platform");
}
} // namespace stana::platform::detail
