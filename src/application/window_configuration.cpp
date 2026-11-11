module;

#include "../platform/native_contracts.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <unicode/ustring.h>
#include <utility>
#include <variant>
#include <vector>

module stana;
import :application;

namespace stana::application {
namespace {
[[nodiscard]] auto validText(const std::string &text) -> bool {
	if (text.find('\0') != std::string::npos ||
		text.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
		return false;
	}
	UErrorCode status = U_ZERO_ERROR;
	std::int32_t length = 0;
	u_strFromUTF8(
		nullptr,
		0,
		&length,
		text.data(),
		static_cast<std::int32_t>(text.size()),
		&status
	);
	return U_SUCCESS(status) != 0 || status == U_BUFFER_OVERFLOW_ERROR;
}

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
			} else if constexpr (std::same_as<Alternative, DialogWindow> ||
				std::same_as<Alternative, UtilityWindow>) {
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
	: channel_(std::make_shared<detail::WindowRequestChannel>()) {
}

WindowRequestSource::~WindowRequestSource() = default;

auto WindowRequestSource::stream() const -> WindowRequestStream {
	return WindowRequestStream{channel_};
}

auto WindowRequestSource::emit(WindowAction action) const
	-> std::expected<WindowRequestId, WindowRequestError> {
	if (!channel_) {
		return std::unexpected(WindowRequestError::notAttached);
	}
	return channel_->emit(action);
}

namespace detail {
struct WindowRequestChannel::State {
	std::mutex mutex{};
	bool attached = false;
	bool closed = false;
	std::vector<PendingWindowRequest> pending{};
	std::function<void()> wakeup{};
};

WindowRequestChannel::WindowRequestChannel() : state(std::make_unique<State>()) {
}

WindowRequestChannel::~WindowRequestChannel() = default;

auto WindowRequestChannel::attach() -> std::expected<void, WindowRequestError> {
	const std::scoped_lock lock(state->mutex);
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
	const std::scoped_lock lock(state->mutex);
	state->closed = true;
	state->pending.clear();
	state->wakeup = {};
}

auto WindowRequestChannel::drain() -> std::vector<PendingWindowRequest> {
	const std::scoped_lock lock(state->mutex);
	std::vector<PendingWindowRequest> drained{};
	drained.swap(state->pending);
	return drained;
}

void WindowRequestChannel::setWakeup(std::function<void()> wakeup) {
	std::function<void()> notify;
	{
		const std::scoped_lock lock(state->mutex);
		state->wakeup = std::move(wakeup);
		if (!state->pending.empty()) {
			notify = state->wakeup;
		}
	}
	if (notify) {
		notify();
	}
}

auto WindowRequestChannel::emit(WindowAction action)
	-> std::expected<WindowRequestId, WindowRequestError> {
	if (const auto validation = validateWindowAction(action); !validation) {
		return std::unexpected(validation.error());
	}
	std::unique_lock lock(state->mutex);
	if (state->closed) {
		return std::unexpected(WindowRequestError::windowClosed);
	}
	if (!state->attached) {
		return std::unexpected(WindowRequestError::notAttached);
	}
	const WindowRequestId id{.value = ::stana::detail::nextIdentity()};
	state->pending.push_back(PendingWindowRequest{.id = id, .action = action});
	const auto notify = state->wakeup;
	lock.unlock();
	if (notify) {
		notify();
	}
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

auto validateWindowAction(const WindowAction &action) -> std::expected<void, WindowRequestError> {
	const auto requireToken =
		[](const InteractionToken &token) -> std::expected<void, WindowRequestError> {
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
				if constexpr (std::same_as<Alternative, ShowWindowMenu>) {
					if (!std::isfinite(value.location.x.value()) ||
						!std::isfinite(value.location.y.value())) {
						return std::unexpected(WindowRequestError::invalidAction);
					}
				}
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
	-> std::expected<void, Error> {
	if (!finiteNonnegative(snapshot.contentSize) || snapshot.contentSize.width.value() <= 0.0 ||
		snapshot.contentSize.height.value() <= 0.0) {
		return std::unexpected(Error::invalidValue);
	}
	if (snapshot.minimumContentSize && !finiteNonnegative(*snapshot.minimumContentSize)) {
		return std::unexpected(Error::invalidValue);
	}
	if (snapshot.maximumContentSize && !finiteNonnegative(*snapshot.maximumContentSize)) {
		return std::unexpected(Error::invalidValue);
	}
	if (snapshot.resizeIncrement &&
		(!finiteNonnegative(*snapshot.resizeIncrement) ||
			snapshot.resizeIncrement->width.value() <= 0.0 ||
			snapshot.resizeIncrement->height.value() <= 0.0)) {
		return std::unexpected(Error::invalidValue);
	}
	if (snapshot.minimumContentSize && snapshot.maximumContentSize) {
		if (snapshot.minimumContentSize->width.value() >
				snapshot.maximumContentSize->width.value() ||
			snapshot.minimumContentSize->height.value() >
				snapshot.maximumContentSize->height.value()) {
			return std::unexpected(Error::invalidValue);
		}
	}
	if (snapshot.aspectRatio) {
		if (!std::isfinite(*snapshot.aspectRatio) || *snapshot.aspectRatio <= 0.0) {
			return std::unexpected(Error::invalidValue);
		}
	}
	if (!std::isfinite(snapshot.opacity) || snapshot.opacity < 0.0 || snapshot.opacity > 1.0) {
		return std::unexpected(Error::invalidValue);
	}
	if (snapshot.state == WindowState::fullscreen && !snapshot.controls.fullscreens) {
		return std::unexpected(Error::invalidValue);
	}
	if (!validOwnership(snapshot.role)) {
		return std::unexpected(Error::invalidStructure);
	}
	const bool validScreen = std::visit(
		[](const auto &placement) -> bool {
			const auto *specific = std::get_if<SpecificScreen>(&placement.screen);
			return specific == nullptr || specific->id.value != 0;
		},
		snapshot.placement
	);
	if (!validScreen) {
		return std::unexpected(Error::invalidValue);
	}
	if (const auto *placement = std::get_if<ExplicitPlacement>(&snapshot.placement)) {
		if (!std::isfinite(placement->origin.x.value()) ||
			!std::isfinite(placement->origin.y.value())) {
			return std::unexpected(Error::invalidValue);
		}
	}
	const auto owner = std::visit(
		[](const auto &role) -> std::uint64_t {
			using Role = std::decay_t<decltype(role)>;
			if constexpr (std::same_as<Role, PopupWindow>) {
				return role.owner.value;
			} else if constexpr (!std::same_as<Role, StandardWindow>) {
				return role.owner ? role.owner->value : 0;
			}
			return 0;
		},
		snapshot.role
	);
	if (owner != 0 && owner == snapshot.id.value) {
		return std::unexpected(Error::invalidStructure);
	}
	if (const auto *popup = std::get_if<PopupWindow>(&snapshot.role)) {
		if (!finiteNonnegative(popup->anchor.size) ||
			!std::isfinite(popup->anchor.origin.x.value()) ||
			!std::isfinite(popup->anchor.origin.y.value())) {
			return std::unexpected(Error::invalidValue);
		}
	}
	const auto validIdentifier = [](const std::string &value) -> bool {
		return !value.empty() && validText(value);
	};
	if (!validText(snapshot.title)) {
		return std::unexpected(Error::invalidValue);
	}
	if (snapshot.toolbar) {
		if (!validIdentifier(snapshot.toolbar->identifier)) {
			return std::unexpected(Error::invalidValue);
		}
		std::set<std::string> identifiers;
		for (const auto &item : snapshot.toolbar->items) {
			if (!validIdentifier(item.identifier) || !identifiers.insert(item.identifier).second) {
				return std::unexpected(Error::invalidValue);
			}
		}
	}
	std::set<std::string> formats;
	for (const auto &format : snapshot.acceptedFormats) {
		if (!validIdentifier(format.identifier) || !formats.insert(format.identifier).second) {
			return std::unexpected(Error::invalidValue);
		}
	}
	if (snapshot.document && snapshot.document->representedPath &&
		!validIdentifier(*snapshot.document->representedPath)) {
		return std::unexpected(Error::invalidValue);
	}
	if (!validText(snapshot.restorationKey)) {
		return std::unexpected(Error::invalidValue);
	}

	return {};
}

auto sampleConfiguration(WindowId id, const WindowOptions &options) -> WindowConfigurationSnapshot {
	return {
		.id = id,
		.role = options.role,
		.contentSize = options.contentSize.get(),
		.placement = options.placement.get(),
		.minimumContentSize = options.minimumContentSize.get(),
		.maximumContentSize = options.maximumContentSize.get(),
		.aspectRatio = options.aspectRatio.get(),
		.resizeIncrement = options.resizeIncrement.get(),
		.title = options.title.get(),
		.decorations = options.decorations.get(),
		.background = options.background.get(),
		.opacity = options.opacity.get(),
		.theme = options.theme.get(),
		.hasShadow = options.hasShadow.get(),
		.isResizable = options.isResizable.get(),
		.controls = options.controls.get(),
		.state = options.state.get(),
		.isVisible = options.isVisible.get(),
		.isEnabled = options.isEnabled.get(),
		.acceptsFocus = options.acceptsFocus.get(),
		.activation = options.activation.get(),
		.pointerPolicy = options.pointerPolicy.get(),
		.level = options.level.get(),
		.taskSwitcher = options.taskSwitcher.get(),
		.toolbar = options.toolbar.get(),
		.document = options.document.get(),
		.acceptedFormats = options.dragAndDrop.acceptedFormats,
		.restorationKey = options.restoration.identity.key,
		.restorationPolicy = options.restoration.policy
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
					{.origin =
							{.x = Points{screen.bounds.origin.x},
								.y = Points{screen.bounds.origin.y}},
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
