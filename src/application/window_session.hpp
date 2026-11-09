#ifndef STANA_APPLICATION_WINDOW_SESSION_HPP
#define STANA_APPLICATION_WINDOW_SESSION_HPP

#include "../platform/native_contracts.hpp"

#include <expected>
#include <memory>
#include <vector>

/// @brief Private portable session orchestration for window configuration and requests.
/// Included only from application implementation units after `import :application`.
namespace stana::application::detail {

struct WindowSession;

/// @brief Retained runtime for one prepared window across reactive updates and teardown.
struct WindowSessionRuntime {
	WindowId id{};
	WindowOptions *options = nullptr; ///< Points at the owning `WindowSession::options`.
	WindowEvents events{};
	std::shared_ptr<::stana::platform::detail::WindowHostControl> hostControl =
		std::make_shared<::stana::platform::detail::WindowHostControl>();
	std::vector<::stana::reactive::Subscription> subscriptions{};
	::stana::platform::detail::WindowConfiguration lastRequested{};
	bool closed = false;
	std::uint64_t deferredCloseRequest = 0;
};

/// @brief Rejects duplicate identities across one `Application::run` set.
[[nodiscard]] auto validateSessionIdentities(const std::vector<WindowSession> &sessions)
	-> std::expected<void, Error>;

/// @brief Attaches request streams; fails with `WindowRequestError` on duplicate attachment.
[[nodiscard]] auto attachSessionRequestStreams(std::vector<WindowSession> &sessions)
	-> std::expected<void, WindowRequestError>;

/// @brief Samples options into the native configuration payload.
[[nodiscard]] auto nativeConfigurationFromOptions(const WindowOptions &options)
	-> ::stana::platform::detail::WindowConfiguration;

/// @brief Maps typed platform extensions into the module-free native configuration.
///
/// macOS field packing lives in `src/platform/macos/window_options.cpp`.
void applyPlatformWindowOptions(
	::stana::platform::detail::WindowConfiguration &configuration,
	const PlatformWindowOptions &platform
);

/// @brief Observes platform-extension properties and pushes configuration updates.
///
/// macOS subscriptions live in `src/platform/macos/window_options.cpp`.
void subscribePlatformPropertyUpdates(const std::shared_ptr<WindowSessionRuntime> &runtime);

/// @brief Pushes the latest sampled configuration through the native host control.
void pushConfiguration(const std::shared_ptr<WindowSessionRuntime> &runtime);

/// @brief Installs configuration, close-gate, observation, and reactive update hooks.
void installSessionController(
	::stana::platform::detail::Window &window,
	const std::shared_ptr<WindowSessionRuntime> &runtime
);

/// @brief Drains the request queue and reports `WindowRequestCompleted` outcomes.
void drainSessionRequests(const std::shared_ptr<WindowSessionRuntime> &runtime);

/// @brief Cancels subscriptions, closes the request stream, and emits WindowClosed.
void teardownSessionRuntime(const std::shared_ptr<WindowSessionRuntime> &runtime);

} // namespace stana::application::detail

#endif
