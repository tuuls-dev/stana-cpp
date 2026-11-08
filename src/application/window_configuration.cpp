module;

#include "../platform/native_contracts.hpp"

#include <cmath>
#include <concepts>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

module stana;
import :application;

namespace stana::application {
namespace {

[[nodiscard]] auto finiteNonnegative(Points value) -> bool {
	return std::isfinite(value.value()) && value.value() >= 0.0;
}

[[nodiscard]] auto finiteNonnegative(const Size &size) -> bool {
	return finiteNonnegative(size.width) && finiteNonnegative(size.height);
}

[[nodiscard]] auto validOwnership(const WindowRole &role) -> bool {
	return std::visit(
		[](const auto &value) -> bool {
			using Alternative = std::decay_t<decltype(value)>;
			if constexpr (std::same_as<Alternative, PopupWindow>) {
				return value.owner.value != 0;
			} else if constexpr (std::same_as<Alternative, DialogWindow>) {
				return !value.owner.has_value() || value.owner->value != 0;
			} else if constexpr (std::same_as<Alternative, UtilityWindow>) {
				return !value.owner.has_value() || value.owner->value != 0;
			} else {
				return true;
			}
		},
		role
	);
}

} // namespace

auto WindowId::create() -> WindowId {
	return WindowId{.value = ::stana::detail::nextIdentity()};
}

WindowRequestSource::WindowRequestSource()
	: channel_(std::make_shared<detail::WindowRequestChannel>()) {}

WindowRequestSource::~WindowRequestSource() = default;

auto WindowRequestSource::stream() const -> WindowRequestStream {
	return WindowRequestStream{channel_};
}

auto WindowRequestSource::emit(WindowAction action) const
	-> std::expected<WindowRequestId, WindowRequestError> {
	if (!channel_) {
		return std::unexpected(WindowRequestError::notAttached);
	}
	return channel_->emit(std::move(action));
}

namespace detail {
struct WindowRequestChannel::State {
	std::mutex mutex{};
	bool attached = false;
	bool closed = false;
	std::vector<PendingWindowRequest> pending{};
};

WindowRequestChannel::WindowRequestChannel() : state(std::make_unique<State>()) {}

WindowRequestChannel::~WindowRequestChannel() = default;

auto WindowRequestChannel::attach() -> std::expected<void, WindowRequestError> {
	std::lock_guard lock(state->mutex);
	if (state->closed) {
		return std::unexpected(WindowRequestError::windowClosed);
	}
	if (state->attached) {
		return std::unexpected(WindowRequestError::duplicateStream);
	}
	state->attached = true;
	return {};
}

void WindowRequestChannel::close() {
	std::lock_guard lock(state->mutex);
	state->closed = true;
	state->pending.clear();
}

auto WindowRequestChannel::drain() -> std::vector<PendingWindowRequest> {
	std::lock_guard lock(state->mutex);
	std::vector<PendingWindowRequest> drained{};
	drained.swap(state->pending);
	return drained;
}

auto WindowRequestChannel::emit(WindowAction action)
	-> std::expected<WindowRequestId, WindowRequestError> {
	if (const auto validation = validateWindowAction(action); !validation) {
		return std::unexpected(validation.error());
	}
	std::lock_guard lock(state->mutex);
	if (state->closed) {
		return std::unexpected(WindowRequestError::windowClosed);
	}
	if (!state->attached) {
		return std::unexpected(WindowRequestError::notAttached);
	}
	const WindowRequestId id{.value = ::stana::detail::nextIdentity()};
	state->pending.push_back(PendingWindowRequest{.id = id, .action = std::move(action)});
	return id;
}

auto requestStreamChannel(const WindowRequestStream &stream)
	-> std::shared_ptr<WindowRequestChannel> {
	return stream.channel_;
}

auto attachRequestStream(const WindowRequestStream &stream)
	-> std::expected<void, WindowRequestError> {
	const auto channel = requestStreamChannel(stream);
	if (!channel) {
		return {};
	}
	return channel->attach();
}

void closeRequestStream(const WindowRequestStream &stream) {
	if (const auto channel = requestStreamChannel(stream)) {
		channel->close();
	}
}

auto validateWindowAction(const WindowAction &action)
	-> std::expected<void, WindowRequestError> {
	const auto requireToken = [](const InteractionToken &token)
		-> std::expected<void, WindowRequestError> {
		if (token.value == 0) {
			return std::unexpected(WindowRequestError::invalidToken);
		}
		return {};
	};

	return std::visit(
		[&](const auto &value) -> std::expected<void, WindowRequestError> {
			using Alternative = std::decay_t<decltype(value)>;
			if constexpr (std::same_as<Alternative, BeginInteractiveMove> ||
				std::same_as<Alternative, BeginInteractiveResize> ||
				std::same_as<Alternative, ShowWindowMenu>) {
				return requireToken(value.interaction);
			} else if constexpr (std::same_as<Alternative, FocusWindow>) {
				if (value.interaction.has_value()) {
					return requireToken(*value.interaction);
				}
				return {};
			} else if constexpr (std::same_as<Alternative, RespondDeferredClose>) {
				if (value.request.value == 0 || value.decision == CloseDecision::defer) {
					return std::unexpected(WindowRequestError::invalidAction);
				}
				return {};
			} else {
				return {};
			}
		},
		action
	);
}

auto validateConfiguration(const WindowConfigurationSnapshot &snapshot)
	-> std::expected<void, ConfigurationValidationError> {
	if (!finiteNonnegative(snapshot.contentSize)) {
		return std::unexpected(ConfigurationValidationError::invalidGeometry);
	}
	if (snapshot.minimumContentSize && !finiteNonnegative(*snapshot.minimumContentSize)) {
		return std::unexpected(ConfigurationValidationError::invalidGeometry);
	}
	if (snapshot.maximumContentSize && !finiteNonnegative(*snapshot.maximumContentSize)) {
		return std::unexpected(ConfigurationValidationError::invalidGeometry);
	}
	if (snapshot.resizeIncrement && !finiteNonnegative(*snapshot.resizeIncrement)) {
		return std::unexpected(ConfigurationValidationError::invalidGeometry);
	}
	if (snapshot.minimumContentSize && snapshot.maximumContentSize) {
		if (snapshot.minimumContentSize->width.value() >
				snapshot.maximumContentSize->width.value() ||
			snapshot.minimumContentSize->height.value() >
				snapshot.maximumContentSize->height.value()) {
			return std::unexpected(ConfigurationValidationError::invalidConstraints);
		}
	}
	if (snapshot.aspectRatio) {
		if (!std::isfinite(*snapshot.aspectRatio) || *snapshot.aspectRatio <= 0.0) {
			return std::unexpected(ConfigurationValidationError::invalidConstraints);
		}
	}
	if (!std::isfinite(snapshot.opacity) || snapshot.opacity < 0.0 || snapshot.opacity > 1.0) {
		return std::unexpected(ConfigurationValidationError::invalidOpacity);
	}
	if (!validOwnership(snapshot.role)) {
		return std::unexpected(ConfigurationValidationError::invalidOwnership);
	}
	const auto *explicitPlacement = std::get_if<ExplicitPlacement>(&snapshot.placement);
	if (explicitPlacement != nullptr) {
		if (!std::isfinite(explicitPlacement->origin.x.value()) ||
			!std::isfinite(explicitPlacement->origin.y.value())) {
			return std::unexpected(ConfigurationValidationError::invalidGeometry);
		}
		if (const auto *specific =
				std::get_if<SpecificScreen>(&explicitPlacement->screen)) {
			if (specific->id.value == 0) {
				return std::unexpected(ConfigurationValidationError::invalidScreen);
			}
		}
	}
	return {};
}

auto sampleConfiguration(WindowId id, const WindowOptions &options)
	-> WindowConfigurationSnapshot {
	return {
		.id = id,
		.role = options.creation.role,
		.contentSize = options.geometry.contentSize.get(),
		.placement = options.geometry.placement.get(),
		.minimumContentSize = options.geometry.minimumContentSize.get(),
		.maximumContentSize = options.geometry.maximumContentSize.get(),
		.aspectRatio = options.geometry.aspectRatio.get(),
		.resizeIncrement = options.geometry.resizeIncrement.get(),
		.title = options.appearance.title.get(),
		.decorations = options.appearance.decorations.get(),
		.background = options.appearance.background.get(),
		.opacity = options.appearance.opacity.get(),
		.theme = options.appearance.theme.get(),
		.hasShadow = options.appearance.hasShadow.get(),
		.isResizable = options.behavior.isResizable.get(),
		.controls = options.behavior.controls.get(),
		.state = options.behavior.state.get(),
		.isVisible = options.behavior.isVisible.get(),
		.isEnabled = options.behavior.isEnabled.get(),
		.acceptsFocus = options.behavior.acceptsFocus.get(),
		.activation = options.behavior.activation.get(),
		.pointerPolicy = options.behavior.pointerPolicy.get(),
		.level = options.desktop.level.get(),
		.taskSwitcher = options.desktop.taskSwitcher.get()
	};
}

} // namespace detail

auto Application::screens() -> std::vector<Screen> {
	std::vector<Screen> result;
	for (const auto &screen : ::stana::platform::detail::enumerateScreens()) {
		result.push_back(
			{.id = ScreenId{.value = screen.id},
				.name = screen.name,
				.bounds =
					{.origin = {.x = Points{screen.bounds.origin.x}, .y = Points{screen.bounds.origin.y}},
						.size =
							{.width = Points{screen.bounds.size.width},
								.height = Points{screen.bounds.size.height}}},
				.workArea =
					{.origin =
							{.x = Points{screen.workArea.origin.x},
								.y = Points{screen.workArea.origin.y}},
						.size =
							{.width = Points{screen.workArea.size.width},
								.height = Points{screen.workArea.size.height}}},
				.backingScale = ScaleFactor{screen.backingScale},
				.isPrimary = screen.isPrimary}
		);
	}
	return result;
}

} // namespace stana::application
