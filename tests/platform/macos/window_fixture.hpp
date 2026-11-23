#ifndef STANA_WINDOW_FIXTURE_HPP
#define STANA_WINDOW_FIXTURE_HPP
#include <chrono>
#include <cstdint>
#include <string>

namespace stana::testing::fixtures::platform::macos {
struct PhysicalViewport {
	double width = 0;
	double height = 0;
};

[[nodiscard]] auto manualFacilities() -> bool;
[[nodiscard]] auto surfaceIsOpaque(const std::string &title) -> bool;
[[nodiscard]] auto windowOpacity(const std::string &title) -> double;

void resizeWindow(const std::string &title, PhysicalViewport);
void setPresentDelay(const std::string &title, std::chrono::milliseconds delay);
[[nodiscard]] auto surfaceContinuity(const std::string &title) -> bool;
[[nodiscard]] auto surfaceSettled(const std::string &title) -> bool;
[[nodiscard]] auto fallbackCoversContent(const std::string &title) -> bool;
[[nodiscard]] auto replacementBackgroundReady(const std::string &title) -> bool;
[[nodiscard]] auto retainedHostLayers(const std::string &title) -> std::size_t;
[[nodiscard]] auto compositorFullResets(const std::string &title) -> std::uint64_t;
[[nodiscard]] auto compositorResizeInvalidations(const std::string &title) -> std::uint64_t;
void staleBackingScale(const std::string &title);
void showWindow(const std::string &title, bool visible);
void replaceAttachment(const std::string &title);
[[nodiscard]] auto activateToolbar(const std::string &title) -> bool;
void pressWindow(const std::string &title);
void dismissWindowMenuAfter(std::chrono::milliseconds delay);
void cancelPrintPanelAfter(std::chrono::milliseconds delay);
void closeWindow(const std::string &title);
[[nodiscard]] auto resultPath() -> std::string;
[[nodiscard]] auto processIdentity() -> std::int32_t;
} // namespace stana::testing::fixtures::platform::macos
#endif
