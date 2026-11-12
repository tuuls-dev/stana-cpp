module;

#include "../native_contracts.hpp"

#include <map>

#include <cstdint>
#include <memory>
#include <utility>

module stana;
import :application;

#include "../../application/window_session.hpp"

namespace stana::application::detail {
namespace {

using ::stana::platform::detail::WindowConfiguration;

void observeMacos(
	const std::shared_ptr<WindowSessionRuntime> &runtime,
	auto &&property,
	auto &&refresh
) {
	runtime->subscriptions.push_back(
		::stana::reactive::observe(std::forward<decltype(property)>(property), refresh)
	);
}

} // namespace

void applyPlatformWindowOptions(
	WindowConfiguration &configuration,
	const PlatformWindowOptions &platform
) {
	const auto *macos = platform.macos();
	if (macos == nullptr) {
		configuration.macos = {};
		return;
	}
	configuration.macos.present = true;
	configuration.macos.fullSizeContentView = macos->titleBar.contentIntegration.get() ==
		platform::macos::TitleBarContentIntegration::fullSizeContentView;
	configuration.macos.transparentTitleBar = macos->titleBar.transparentTitleBar.get();
	configuration.macos.titleVisible = macos->titleBar.titleVisible.get();
	configuration.macos.hideTitleBarControls =
		macos->titleBar.controls.get() == platform::macos::TitleBarControlVisibility::hidden;
	configuration.macos.tabbingMode = static_cast<std::uint8_t>(macos->tabbing.mode.get());
	configuration.macos.tabbingIdentifier = macos->tabbing.tabbingIdentifier.get();
	configuration.macos.spacesBehavior = static_cast<std::uint8_t>(macos->spaces.behavior.get());
	configuration.macos.fullscreenBehavior =
		static_cast<std::uint8_t>(macos->spaces.fullscreen.get());
	configuration.macos.becomesKeyOnlyIfNeeded = macos->panel.becomesKeyOnlyIfNeeded.get();
	configuration.macos.worksWhenModal = macos->panel.worksWhenModal.get();
	configuration.macos.floatingPanel = macos->panel.floatingPanel.get();
	if (const auto level = macos->nativeLevel.level.get()) {
		configuration.macos.nativeLevel =
			static_cast<::stana::platform::detail::MacosWindowLevel>(*level);
	}
}

void subscribePlatformPropertyUpdates(const std::shared_ptr<WindowSessionRuntime> &runtime) {
	if (runtime == nullptr || runtime->options == nullptr) {
		return;
	}
	const auto *macos = runtime->options->platform.macos();
	if (macos == nullptr) {
		return;
	}
	const auto weak = std::weak_ptr{runtime};
	const auto refresh = [weak](const auto &) -> void {
		if (const auto owner = weak.lock(); owner && !owner->closed) {
			scheduleConfiguration(owner);
		}
	};
	observeMacos(runtime, macos->titleBar.contentIntegration, refresh);
	observeMacos(runtime, macos->titleBar.transparentTitleBar, refresh);
	observeMacos(runtime, macos->titleBar.titleVisible, refresh);
	observeMacos(runtime, macos->titleBar.controls, refresh);
	observeMacos(runtime, macos->tabbing.mode, refresh);
	observeMacos(runtime, macos->tabbing.tabbingIdentifier, refresh);
	observeMacos(runtime, macos->spaces.behavior, refresh);
	observeMacos(runtime, macos->spaces.fullscreen, refresh);
	observeMacos(runtime, macos->panel.becomesKeyOnlyIfNeeded, refresh);
	observeMacos(runtime, macos->panel.worksWhenModal, refresh);
	observeMacos(runtime, macos->panel.floatingPanel, refresh);
	observeMacos(runtime, macos->nativeLevel.level, refresh);
}

} // namespace stana::application::detail
