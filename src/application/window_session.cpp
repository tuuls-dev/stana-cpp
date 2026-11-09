module;

#include "../platform/native_contracts.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

module stana;
import :application;

#include "window_session.hpp"

namespace stana::application::detail {
namespace {

using ::stana::platform::detail::NativeCloseDecision;
using ::stana::platform::detail::NativePlacementKind;
using ::stana::platform::detail::NativeWindowActionKind;
using ::stana::platform::detail::NativeWindowLevel;
using ::stana::platform::detail::NativeWindowObservation;
using ::stana::platform::detail::NativeWindowOutcome;
using ::stana::platform::detail::NativeWindowRole;
using ::stana::platform::detail::NativeWindowState;
using ::stana::platform::detail::WindowConfiguration;

[[nodiscard]] auto nativeRole(const WindowRole &role) -> NativeWindowRole {
	return std::visit(
		[](const auto &value) -> NativeWindowRole {
			using Alternative = std::decay_t<decltype(value)>;
			if constexpr (std::same_as<Alternative, DialogWindow>) {
				return NativeWindowRole::dialog;
			} else if constexpr (std::same_as<Alternative, UtilityWindow>) {
				return NativeWindowRole::utility;
			} else if constexpr (std::same_as<Alternative, PopupWindow>) {
				return NativeWindowRole::popup;
			} else {
				return NativeWindowRole::standard;
			}
		},
		role
	);
}

[[nodiscard]] auto nativePlacement(const WindowPlacement &placement)
	-> std::tuple<NativePlacementKind, double, double> {
	if (const auto *explicitPlacement = std::get_if<ExplicitPlacement>(&placement)) {
		return {
			NativePlacementKind::explicitOrigin,
			explicitPlacement->origin.x.value(),
			explicitPlacement->origin.y.value()
		};
	}
	if (std::holds_alternative<SystemPlacement>(placement)) {
		return {NativePlacementKind::system, 0.0, 0.0};
	}
	return {NativePlacementKind::centered, 0.0, 0.0};
}

[[nodiscard]] auto nativeState(WindowState state) -> NativeWindowState {
	switch (state) {
	case WindowState::minimized:
		return NativeWindowState::minimized;
	case WindowState::maximized:
		return NativeWindowState::maximized;
	case WindowState::fullscreen:
		return NativeWindowState::fullscreen;
	case WindowState::normal:
		return NativeWindowState::normal;
	}
	return NativeWindowState::normal;
}

[[nodiscard]] auto portableState(NativeWindowState state) -> WindowState {
	switch (state) {
	case NativeWindowState::minimized:
		return WindowState::minimized;
	case NativeWindowState::maximized:
		return WindowState::maximized;
	case NativeWindowState::fullscreen:
		return WindowState::fullscreen;
	case NativeWindowState::normal:
		return WindowState::normal;
	}
	return WindowState::normal;
}

[[nodiscard]] auto portableOutcome(NativeWindowOutcome outcome) -> WindowOutcome {
	switch (outcome) {
	case NativeWindowOutcome::applied:
		return WindowOutcome::applied;
	case NativeWindowOutcome::pending:
		return WindowOutcome::pending;
	case NativeWindowOutcome::adjusted:
		return WindowOutcome::adjusted;
	case NativeWindowOutcome::unsupported:
		return WindowOutcome::unsupported;
	case NativeWindowOutcome::invalid:
		return WindowOutcome::invalid;
	case NativeWindowOutcome::denied:
		return WindowOutcome::denied;
	case NativeWindowOutcome::failed:
		return WindowOutcome::failed;
	case NativeWindowOutcome::cancelled:
		return WindowOutcome::cancelled;
	case NativeWindowOutcome::unconfirmed:
		return WindowOutcome::unconfirmed;
	}
	return WindowOutcome::failed;
}

[[nodiscard]] auto optionalLogicalSize(const std::optional<Size> &size)
	-> std::optional<::stana::platform::detail::LogicalSize> {
	if (!size) {
		return std::nullopt;
	}
	return ::stana::platform::detail::LogicalSize{
		.width = size->width.value(),
		.height = size->height.value()
	};
}

void publishEvent(const std::shared_ptr<WindowSessionRuntime> &runtime, const WindowEvent &event) {
	if (runtime && runtime->events.onEvent) {
		runtime->events.onEvent(event);
	}
}

[[nodiscard]] auto snapshotFromObservation(
	WindowId id,
	const NativeWindowObservation &observation,
	WindowLifecycle lifecycle
) -> WindowSnapshot {
	return {
		.id = id,
		.lifecycle = lifecycle,
		.contentBounds =
			{.origin = {.x = Points{observation.contentX}, .y = Points{observation.contentY}},
				.size =
					{.width = Points{observation.contentWidth},
						.height = Points{observation.contentHeight}}},
		.contentSize =
			{.width = Points{observation.contentWidth},
				.height = Points{observation.contentHeight}},
		.screen = observation.screenId == 0
			? std::optional<ScreenId>{}
			: std::optional{ScreenId{.value = observation.screenId}},
		.state = portableState(observation.state),
		.isVisible = observation.isVisible,
		.isKey = observation.isKey,
		.isActive = observation.isActive,
		.backingScale = ScaleFactor{observation.backingScale}
	};
}

} // namespace

void pushConfiguration(const std::shared_ptr<WindowSessionRuntime> &runtime) {
	if (!runtime || runtime->closed || runtime->options == nullptr || !runtime->hostControl ||
		!runtime->hostControl->applyConfiguration) {
		return;
	}
	const auto configuration = nativeConfigurationFromOptions(*runtime->options);
	runtime->lastRequested = configuration;
	runtime->hostControl->applyConfiguration(configuration);
}

namespace {

void subscribePortablePropertyUpdates(const std::shared_ptr<WindowSessionRuntime> &runtime) {
	if (runtime->options == nullptr) {
		return;
	}
	const auto weak = std::weak_ptr{runtime};
	const auto refresh = [weak](const auto &) -> void {
		if (const auto owner = weak.lock(); owner && !owner->closed) {
			pushConfiguration(owner);
			drainSessionRequests(owner);
		}
	};

	auto &options = *runtime->options;
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.geometry.contentSize, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.geometry.placement, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.geometry.minimumContentSize, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.geometry.maximumContentSize, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.geometry.aspectRatio, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.geometry.resizeIncrement, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.appearance.title, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.appearance.decorations, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.appearance.opacity, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.appearance.hasShadow, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.behavior.isResizable, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.behavior.controls, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.behavior.state, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.behavior.isVisible, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.behavior.acceptsFocus, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.behavior.activation, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.behavior.pointerPolicy, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.desktop.level, refresh)
	);
}

void subscribePropertyUpdates(const std::shared_ptr<WindowSessionRuntime> &runtime) {
	subscribePortablePropertyUpdates(runtime);
	subscribePlatformPropertyUpdates(runtime);
}

} // namespace

auto nativeConfigurationFromOptions(const WindowOptions &options) -> WindowConfiguration {
	const auto snapshot = sampleConfiguration(WindowId{}, options);
	const auto [placement, originX, originY] = nativePlacement(snapshot.placement);
	const auto controls = snapshot.controls;
	WindowConfiguration configuration{
		.contentWidth = snapshot.contentSize.width.value(),
		.contentHeight = snapshot.contentSize.height.value(),
		.title = snapshot.title,
		.style =
			{.closable = controls.closes,
				.minimizable = controls.minimizes,
				.maximizable = controls.maximizes,
				.resizable = snapshot.isResizable,
				.titled = std::holds_alternative<NativeDecorations>(snapshot.decorations),
				.fullscreens = controls.fullscreens},
		.opacity = snapshot.opacity,
		.hasShadow = snapshot.hasShadow,
		.isVisible = snapshot.isVisible,
		.acceptsFocus = snapshot.acceptsFocus,
		.ignoresMouseEvents = std::holds_alternative<PointerPassThrough>(snapshot.pointerPolicy),
		.presentWithoutActivation =
			std::holds_alternative<PresentWithoutActivation>(snapshot.activation),
		.minimumContentSize = optionalLogicalSize(snapshot.minimumContentSize),
		.maximumContentSize = optionalLogicalSize(snapshot.maximumContentSize),
		.aspectRatio = snapshot.aspectRatio,
		.resizeIncrement = optionalLogicalSize(snapshot.resizeIncrement),
		.placement = placement,
		.originX = originX,
		.originY = originY,
		.level = snapshot.level == WindowLevel::floating ? NativeWindowLevel::floating
														 : NativeWindowLevel::normal,
		.state = nativeState(snapshot.state),
		.role = nativeRole(snapshot.role)
	};
	applyPlatformWindowOptions(configuration, options.platform);
	return configuration;
}

auto validateSessionIdentities(const std::vector<WindowSession> &sessions) -> bool {
	std::vector<std::uint64_t> seen;
	seen.reserve(sessions.size());
	for (const auto &session : sessions) {
		if (session.id.value == 0) {
			continue;
		}
		for (const auto value : seen) {
			if (value == session.id.value) {
				return false;
			}
		}
		seen.push_back(session.id.value);
	}
	return true;
}

auto attachSessionRequestStreams(std::vector<WindowSession> &sessions) -> bool {
	for (auto &session : sessions) {
		if (const auto attached = attachRequestStream(session.options.requests); !attached) {
			return false;
		}
	}
	return true;
}

void drainSessionRequests(const std::shared_ptr<WindowSessionRuntime> &runtime) {
	if (!runtime || runtime->closed || runtime->options == nullptr) {
		return;
	}
	const auto channel = requestStreamChannel(runtime->options->requests);
	if (!channel) {
		return;
	}
	for (auto &pending : channel->drain()) {
		NativeWindowActionKind kind = NativeWindowActionKind::close;
		NativeWindowState state = NativeWindowState::normal;
		std::uint64_t token = 0;
		bool supported = true;
		std::visit(
			[&](const auto &action) -> void {
				using Alternative = std::decay_t<decltype(action)>;
				if constexpr (std::same_as<Alternative, FocusWindow>) {
					kind = NativeWindowActionKind::focus;
				} else if constexpr (std::same_as<Alternative, SetWindowState>) {
					kind = NativeWindowActionKind::setState;
					state = nativeState(action.state);
				} else if constexpr (std::same_as<Alternative, CloseWindow>) {
					kind = NativeWindowActionKind::close;
				} else if constexpr (std::same_as<Alternative, BeginInteractiveMove>) {
					kind = NativeWindowActionKind::beginInteractiveMove;
					token = action.interaction.value;
				} else if constexpr (std::same_as<Alternative, BeginInteractiveResize>) {
					kind = NativeWindowActionKind::beginInteractiveResize;
					token = action.interaction.value;
				} else if constexpr (std::same_as<Alternative, ShowWindowMenu>) {
					kind = NativeWindowActionKind::showWindowMenu;
					token = action.interaction.value;
				} else if constexpr (std::same_as<Alternative, CaptureWindow>) {
					kind = NativeWindowActionKind::capture;
				} else if constexpr (std::same_as<Alternative, PrintWindow>) {
					kind = NativeWindowActionKind::print;
				} else if constexpr (std::same_as<Alternative, RespondDeferredClose>) {
					kind = NativeWindowActionKind::respondDeferredClose;
					token = action.request.value;
				} else {
					supported = false;
				}
			},
			pending.action
		);
		NativeWindowOutcome outcome = NativeWindowOutcome::unsupported;
		if (supported && runtime->hostControl && runtime->hostControl->dispatchAction) {
			outcome = runtime->hostControl->dispatchAction(kind, state, token);
		} else if (supported) {
			outcome = NativeWindowOutcome::unsupported;
		}
		publishEvent(
			runtime,
			WindowRequestCompleted{
				.result =
					{.id = pending.id,
						.outcome = portableOutcome(outcome),
						.action = std::move(pending.action),
						.payload = {}}
			}
		);
	}
}

void installSessionController(
	::stana::platform::detail::Window &window,
	const std::shared_ptr<WindowSessionRuntime> &runtime
) {
	if (runtime->options == nullptr) {
		return;
	}
	const auto configuration = nativeConfigurationFromOptions(*runtime->options);
	runtime->lastRequested = configuration;
	window.title = configuration.title;
	window.width = configuration.contentWidth;
	window.height = configuration.contentHeight;
	window.configuration = configuration;
	window.hostControl = runtime->hostControl;

	const auto weak = std::weak_ptr{runtime};
	const auto previousClose = std::move(window.close);
	window.close = [weak, previousClose] -> void {
		if (const auto owner = weak.lock()) {
			teardownSessionRuntime(owner);
		}
		if (previousClose) {
			previousClose();
		}
	};
	window.configurationApplied = [weak] -> void {
		if (const auto owner = weak.lock(); owner && owner->hostControl &&
			owner->hostControl->observe) {
			publishEvent(
				owner,
				WindowReady{
					.snapshot = snapshotFromObservation(
						owner->id,
						owner->hostControl->observe(),
						WindowLifecycle::open
					),
					.capabilities = {}
				}
			);
			drainSessionRequests(owner);
		}
	};
	window.closeShouldProceed = [weak](std::uint64_t closeRequestId) -> NativeCloseDecision {
		const auto owner = weak.lock();
		if (!owner || !owner->events.onCloseRequested) {
			return NativeCloseDecision::allow;
		}
		switch (owner->events.onCloseRequested(
			CloseRequest{
				.id = CloseRequestId{.value = closeRequestId},
				.window = owner->id
			}
		)) {
		case CloseDecision::allow:
			return NativeCloseDecision::allow;
		case CloseDecision::deny:
			return NativeCloseDecision::deny;
		case CloseDecision::defer:
			owner->deferredCloseRequest = closeRequestId;
			return NativeCloseDecision::defer;
		}
		return NativeCloseDecision::deny;
	};
	window.stateChanged = [weak](const NativeWindowObservation &observation) -> void {
		if (const auto owner = weak.lock()) {
			publishEvent(
				owner,
				WindowStateChanged{
					.snapshot =
						snapshotFromObservation(owner->id, observation, WindowLifecycle::open)
				}
			);
		}
	};

	const auto previousFrame = std::move(window.frame);
	window.frame = [weak, previousFrame](double timestamp, double deadline) -> void {
		if (const auto owner = weak.lock()) {
			drainSessionRequests(owner);
		}
		if (previousFrame) {
			previousFrame(timestamp, deadline);
		}
	};

	subscribePropertyUpdates(runtime);
}

void teardownSessionRuntime(const std::shared_ptr<WindowSessionRuntime> &runtime) {
	if (!runtime || runtime->closed) {
		return;
	}
	runtime->closed = true;
	for (auto &subscription : runtime->subscriptions) {
		subscription.cancel();
	}
	runtime->subscriptions.clear();
	if (runtime->options != nullptr) {
		closeRequestStream(runtime->options->requests);
	}
	publishEvent(runtime, WindowClosed{.id = runtime->id});
}

} // namespace stana::application::detail
