#ifndef STANA_WINDOW_FIXTURE_HPP
#define STANA_WINDOW_FIXTURE_HPP
#include <cstdint>
#include <string>

namespace stana::fixtures {
struct PhysicalViewport {
	double width = 0;
	double height = 0;
};

void resizeWindow(const std::string &title, PhysicalViewport);
void showWindow(const std::string &title, bool visible);
void replaceAttachment(const std::string &title);
void closeWindow(const std::string &title);
[[nodiscard]] auto resultPath() -> std::string;
[[nodiscard]] auto processIdentity() -> std::int32_t;
} // namespace stana::fixtures
#endif
