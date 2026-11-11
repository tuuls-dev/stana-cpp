module;

#include "../platform/native_contracts.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
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

namespace {
[[nodiscard]] auto capabilities(const WindowOptions &options) -> WindowCapabilities {
	WindowCapabilities result;
	for (unsigned index = 0; index <= static_cast<unsigned>(WindowFeature::nativeLevel); ++index) {
		const auto feature = static_cast<WindowFeature>(index);
		auto support = FeatureSupport::supported;
#if !defined(__APPLE__)
		static_cast<void>(options);
		support = FeatureSupport::unsupported;
#else
		if (feature == WindowFeature::taskSwitcher) {
			support = FeatureSupport::unsupported;
		}
		if (feature == WindowFeature::panelBehavior &&
			!std::holds_alternative<UtilityWindow>(options.role)) {
			support = FeatureSupport::unsupported;
		}
		if (feature == WindowFeature::state || feature == WindowFeature::placement ||
			feature == WindowFeature::focus) {
			support = FeatureSupport::partial;
		}
#endif
		result.features.push_back(
			{.feature = feature,
				.support = support,
				.mutability = (feature == WindowFeature::role ||
								  feature == WindowFeature::renderingColorMode ||
								  feature == WindowFeature::restoration ||
								  feature == WindowFeature::dragAndDrop)
					? FeatureMutability::creationOnly
					: FeatureMutability::runtime}
		);
	}
	return result;
}
} // namespace

void scheduleConfiguration(const std::shared_ptr<WindowSessionRuntime> &runtime) {
	if (!runtime || runtime->closed || runtime->refreshQueued) {
		return;
	}
	runtime->refreshQueued = true;
	::stana::reactive::detail::ReactiveRuntime::current().effects.emplace_back(
		[weak = std::weak_ptr{runtime}] -> void {
			if (const auto owner = weak.lock()) {
				owner->refreshQueued = false;
				pushConfiguration(owner);
			}
		}
	);
}

void pushConfiguration(const std::shared_ptr<WindowSessionRuntime> &runtime) {
	if (!runtime || runtime->closed || !runtime->ready || runtime->options == nullptr ||
		!runtime->hostControl || !runtime->hostControl->applyConfiguration) {
		return;
	}
	const auto snapshot = sampleConfiguration(runtime->id, *runtime->options);
	auto configuration = nativeConfigurationFromOptions(*runtime->options);
	configuration.id = runtime->id.value;
	WindowConfigurationResult result{.revision = {.value = ++runtime->revision}};
	const auto add = [&](WindowFeature feature, const auto &value) -> void {
		result.changed.push_back(feature);
		result.results.push_back(
			{.feature = feature, .outcome = WindowOutcome::applied, .effective = value}
		);
	};
	if (!runtime->accepted ||
		!std::equal_to{}(runtime->accepted->contentSize, snapshot.contentSize)) {
		add(WindowFeature::contentSize, snapshot.contentSize);
	}
	if (!runtime->accepted || !std::equal_to{}(runtime->accepted->placement, snapshot.placement)) {
		add(WindowFeature::placement, snapshot.placement);
	}
	if (!runtime->accepted ||
		!std::equal_to{}(runtime->accepted->minimumContentSize, snapshot.minimumContentSize)) {
		add(WindowFeature::minimumContentSize, snapshot.minimumContentSize);
	}
	if (!runtime->accepted ||
		!std::equal_to{}(runtime->accepted->maximumContentSize, snapshot.maximumContentSize)) {
		add(WindowFeature::maximumContentSize, snapshot.maximumContentSize);
	}
	if (!runtime->accepted ||
		!std::equal_to{}(runtime->accepted->aspectRatio, snapshot.aspectRatio)) {
		add(WindowFeature::aspectRatio, snapshot.aspectRatio);
	}
	if (!runtime->accepted ||
		!std::equal_to{}(runtime->accepted->resizeIncrement, snapshot.resizeIncrement)) {
		add(WindowFeature::resizeIncrement, snapshot.resizeIncrement);
	}
	if (!runtime->accepted || !std::equal_to{}(runtime->accepted->title, snapshot.title)) {
		add(WindowFeature::title, snapshot.title);
	}
	if (!runtime->accepted ||
		!std::equal_to{}(runtime->accepted->decorations, snapshot.decorations)) {
		add(WindowFeature::decorations, snapshot.decorations);
	}
	if (!runtime->accepted ||
		!std::equal_to{}(runtime->accepted->background, snapshot.background)) {
		add(WindowFeature::background, snapshot.background);
	}
	if (!runtime->accepted || !std::equal_to{}(runtime->accepted->opacity, snapshot.opacity)) {
		add(WindowFeature::opacity, snapshot.opacity);
	}
	if (!runtime->accepted || !std::equal_to{}(runtime->accepted->theme, snapshot.theme)) {
		add(WindowFeature::theme, snapshot.theme);
	}
	if (!runtime->accepted || !std::equal_to{}(runtime->accepted->hasShadow, snapshot.hasShadow)) {
		add(WindowFeature::hasShadow, snapshot.hasShadow);
	}
	if (!runtime->accepted ||
		!std::equal_to{}(runtime->accepted->isResizable, snapshot.isResizable)) {
		add(WindowFeature::isResizable, snapshot.isResizable);
	}
	if (!runtime->accepted || !std::equal_to{}(runtime->accepted->controls, snapshot.controls)) {
		add(WindowFeature::controls, snapshot.controls);
	}
	if (!runtime->accepted || !std::equal_to{}(runtime->accepted->state, snapshot.state)) {
		add(WindowFeature::state, snapshot.state);
	}
	if (!runtime->accepted || !std::equal_to{}(runtime->accepted->isVisible, snapshot.isVisible)) {
		add(WindowFeature::isVisible, snapshot.isVisible);
	}
	if (!runtime->accepted || !std::equal_to{}(runtime->accepted->isEnabled, snapshot.isEnabled)) {
		add(WindowFeature::isEnabled, snapshot.isEnabled);
	}
	if (!runtime->accepted ||
		!std::equal_to{}(runtime->accepted->acceptsFocus, snapshot.acceptsFocus)) {
		add(WindowFeature::acceptsFocus, snapshot.acceptsFocus);
	}
	if (!runtime->accepted ||
		!std::equal_to{}(runtime->accepted->activation, snapshot.activation)) {
		add(WindowFeature::activation, snapshot.activation);
	}
	if (!runtime->accepted ||
		!std::equal_to{}(runtime->accepted->pointerPolicy, snapshot.pointerPolicy)) {
		add(WindowFeature::pointerPolicy, snapshot.pointerPolicy);
	}
	if (!runtime->accepted || !std::equal_to{}(runtime->accepted->level, snapshot.level)) {
		add(WindowFeature::level, snapshot.level);
	}
	if (!runtime->accepted ||
		!std::equal_to{}(runtime->accepted->taskSwitcher, snapshot.taskSwitcher)) {
		add(WindowFeature::taskSwitcher, snapshot.taskSwitcher);
	}
	if (!runtime->accepted || !std::equal_to{}(runtime->accepted->toolbar, snapshot.toolbar)) {
		add(WindowFeature::toolbar, snapshot.toolbar);
	}
	if (!runtime->accepted || !std::equal_to{}(runtime->accepted->document, snapshot.document)) {
		add(WindowFeature::document, snapshot.document);
	}
	const auto &oldMacos = runtime->lastRequested.macos;
	const auto &newMacos = configuration.macos;
	if (!runtime->accepted || oldMacos.fullSizeContentView != newMacos.fullSizeContentView ||
		oldMacos.transparentTitleBar != newMacos.transparentTitleBar ||
		oldMacos.titleVisible != newMacos.titleVisible ||
		oldMacos.hideTitleBarControls != newMacos.hideTitleBarControls) {
		add(WindowFeature::titleBarIntegration, std::monostate{});
	}
	if (!runtime->accepted || oldMacos.tabbingMode != newMacos.tabbingMode ||
		oldMacos.tabbingIdentifier != newMacos.tabbingIdentifier) {
		add(WindowFeature::tabbing, std::monostate{});
	}
	if (!runtime->accepted || oldMacos.spacesBehavior != newMacos.spacesBehavior ||
		oldMacos.fullscreenBehavior != newMacos.fullscreenBehavior) {
		add(WindowFeature::spaces, std::monostate{});
	}
	if (!runtime->accepted || oldMacos.becomesKeyOnlyIfNeeded != newMacos.becomesKeyOnlyIfNeeded ||
		oldMacos.worksWhenModal != newMacos.worksWhenModal ||
		oldMacos.floatingPanel != newMacos.floatingPanel) {
		add(WindowFeature::panelBehavior, std::monostate{});
	}
	if (!runtime->accepted || oldMacos.nativeLevel != newMacos.nativeLevel) {
		add(WindowFeature::nativeLevel, std::monostate{});
	}

	if (result.changed.empty()) {
		--runtime->revision;
		return;
	}
	if (!validateConfiguration(snapshot)) {
		for (auto &row : result.results) {
			row.outcome = WindowOutcome::invalid;
			row.effective = {};
		}
	} else {
		if (runtime->pendingState &&
			std::ranges::find(result.changed, WindowFeature::state) != result.changed.end()) {
			const auto previous = *runtime->pendingState;
			runtime->pendingState.reset();
			publishEvent(
				runtime,
				WindowConfigurationCompleted{
					.result = {
						.revision = previous.first,
						.changed = {WindowFeature::state},
						.results = {
							{.feature = WindowFeature::state, .outcome = WindowOutcome::cancelled}
						}
					}
				}
			);
		}
		const auto outcome = runtime->hostControl->applyConfiguration(configuration);
		const bool accepted = outcome == NativeWindowOutcome::applied ||
			outcome == NativeWindowOutcome::pending || outcome == NativeWindowOutcome::adjusted;
		for (auto &row : result.results) {
			row.outcome = portableOutcome(outcome);
			if (accepted) {
				row.outcome = WindowOutcome::applied;
				if (row.feature == WindowFeature::taskSwitcher &&
					snapshot.taskSwitcher != TaskSwitcherParticipation::automatic) {
					row.outcome = WindowOutcome::unsupported;
					row.effective = TaskSwitcherParticipation::automatic;
				}
				if (row.feature == WindowFeature::panelBehavior &&
					!std::holds_alternative<UtilityWindow>(snapshot.role)) {
					row.outcome = WindowOutcome::unsupported;
				}
				if (row.feature == WindowFeature::state && runtime->hostControl->observe) {
					const auto actual = portableState(runtime->hostControl->observe().state);
					if (actual != snapshot.state) {
						row.outcome = WindowOutcome::pending;
						runtime->pendingState = std::pair{result.revision, snapshot.state};
						constexpr std::chrono::seconds confirmationDeadline{5};
						Application::postAfter(
							confirmationDeadline,
							[weak = std::weak_ptr{runtime}, revision = result.revision] -> void {
								if (const auto owner = weak.lock(); owner && !owner->closed &&
									owner->pendingState && owner->pendingState->first == revision) {
									owner->pendingState.reset();
									publishEvent(
										owner,
										WindowConfigurationCompleted{
											.result = {
												.revision = revision,
												.changed = {WindowFeature::state},
												.results = {
													{.feature = WindowFeature::state,
														.outcome = WindowOutcome::unconfirmed}
												}
											}
										}
									);
								}
							}
						);
					}
					row.effective = actual;
				}
				if (row.feature == WindowFeature::contentSize && runtime->hostControl->observe) {
					const auto observation = runtime->hostControl->observe();
					const Size actual{
						.width = Points{observation.contentWidth},
						.height = Points{observation.contentHeight}
					};
					row.effective = actual;
					if (actual != snapshot.contentSize) {
						row.outcome = WindowOutcome::adjusted;
					}
				}
			} else {
				row.effective = {};
			}
		}
		if (accepted) {
			if (runtime->acceptedChanged) {
				runtime->acceptedChanged(snapshot);
			}
			runtime->accepted = snapshot;
			runtime->lastRequested = configuration;
		}
	}
	publishEvent(runtime, WindowConfigurationCompleted{.result = std::move(result)});
}

namespace {

void subscribePortablePropertyUpdates(const std::shared_ptr<WindowSessionRuntime> &runtime) {
	if (runtime->options == nullptr) {
		return;
	}
	const auto weak = std::weak_ptr{runtime};
	const auto refresh = [weak](const auto &) -> void {
		if (const auto owner = weak.lock(); owner && !owner->closed) {
			scheduleConfiguration(owner);
		}
	};

	auto &options = *runtime->options;
	runtime->subscriptions.push_back(::stana::reactive::observe(options.contentSize, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.placement, refresh));
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.minimumContentSize, refresh)
	);
	runtime->subscriptions.push_back(
		::stana::reactive::observe(options.maximumContentSize, refresh)
	);
	runtime->subscriptions.push_back(::stana::reactive::observe(options.aspectRatio, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.resizeIncrement, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.title, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.decorations, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.opacity, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.hasShadow, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.isResizable, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.controls, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.state, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.isVisible, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.acceptsFocus, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.activation, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.pointerPolicy, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.level, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.background, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.theme, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.isEnabled, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.taskSwitcher, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.toolbar, refresh));
	runtime->subscriptions.push_back(::stana::reactive::observe(options.document, refresh));
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
	WindowConfiguration configuration;
	configuration.contentWidth = snapshot.contentSize.width.value();
	configuration.contentHeight = snapshot.contentSize.height.value();
	configuration.title = snapshot.title;
	configuration.style = {
		.closable = controls.closes,
		.minimizable = controls.minimizes,
		.maximizable = controls.maximizes,
		.resizable = snapshot.isResizable,
		.titled = std::holds_alternative<NativeDecorations>(snapshot.decorations),
		.fullscreens = controls.fullscreens
	};
	configuration.opacity = snapshot.opacity;
	configuration.hasShadow = snapshot.hasShadow;
	configuration.isVisible = snapshot.isVisible;
	configuration.acceptsFocus = snapshot.acceptsFocus;
	configuration.ignoresMouseEvents =
		std::holds_alternative<PointerPassThrough>(snapshot.pointerPolicy);
	configuration.presentWithoutActivation =
		std::holds_alternative<PresentWithoutActivation>(snapshot.activation);
	configuration.minimumContentSize = optionalLogicalSize(snapshot.minimumContentSize);
	configuration.maximumContentSize = optionalLogicalSize(snapshot.maximumContentSize);
	configuration.aspectRatio = snapshot.aspectRatio;
	configuration.resizeIncrement = optionalLogicalSize(snapshot.resizeIncrement);
	configuration.placement = placement;
	configuration.originX = originX;
	configuration.originY = originY;
	configuration.level = snapshot.level == WindowLevel::floating ? NativeWindowLevel::floating
																  : NativeWindowLevel::normal;
	configuration.state = nativeState(snapshot.state);
	configuration.role = nativeRole(snapshot.role);

	std::visit(
		[&](const auto &role) -> void {
			using Role = std::decay_t<decltype(role)>;
			if constexpr (std::same_as<Role, PopupWindow>) {
				configuration.owner = role.owner.value;
				configuration.anchor = {
					.origin =
						{.x = role.anchor.origin.x.value(), .y = role.anchor.origin.y.value()},
					.size = {
						.width = role.anchor.size.width.value(),
						.height = role.anchor.size.height.value()
					}
				};
				configuration.anchorEdge = static_cast<std::uint8_t>(role.edge);
			} else if constexpr (!std::same_as<Role, StandardWindow>) {
				configuration.owner = role.owner ? role.owner->value : 0;
				if constexpr (std::same_as<Role, DialogWindow>) {
					configuration.modal = role.modal;
				}
			}
		},
		snapshot.role
	);
	std::visit(
		[&](const auto &placementValue) -> void {
			std::visit(
				[&](const auto &screen) -> void {
					using Selection = std::decay_t<decltype(screen)>;
					if constexpr (std::same_as<Selection, PrimaryScreen>) {
						configuration.screenSelection = 1;
					}
					if constexpr (std::same_as<Selection, SpecificScreen>) {
						configuration.screenSelection = 2;
						configuration.screenId = screen.id.value;
					}
				},
				placementValue.screen
			);
		},
		snapshot.placement
	);
	configuration.isEnabled = snapshot.isEnabled;
	configuration.taskSwitcher = static_cast<std::uint8_t>(snapshot.taskSwitcher);
	if (snapshot.theme) {
		configuration.darkTheme = *snapshot.theme == Theme::dark;
	}
	if (const auto *solid = std::get_if<SolidBackground>(&snapshot.background)) {
		configuration.backgroundKind = 1;
		configuration.background = {
			.red = solid->color.red.value(),
			.green = solid->color.green.value(),
			.blue = solid->color.blue.value(),
			.alpha = solid->color.alpha.value()
		};
	} else if (std::holds_alternative<TransparentBackground>(snapshot.background)) {
		configuration.backgroundKind = 2;
	}
	if (snapshot.toolbar) {
		::stana::platform::detail::NativeToolbar toolbar{
			.identifier = snapshot.toolbar->identifier,
			.visible = snapshot.toolbar->visible
		};
		for (const auto &item : snapshot.toolbar->items) {
			toolbar.items.push_back(
				{.identifier = item.identifier, .label = item.label, .enabled = item.enabled}
			);
		}
		const auto property = options.toolbar;
		toolbar.activate = [property](const std::string &identifier) -> void {
			if (const auto current = property.get()) {
				for (const auto &item : current->items) {
					if (item.identifier == identifier && item.enabled && item.onActivate) {
						item.onActivate();
						return;
					}
				}
			}
		};
		configuration.toolbar = std::move(toolbar);
	}
	if (snapshot.document) {
		configuration.document = {
			.representedPath = snapshot.document->representedPath,
			.edited = snapshot.document->isEdited
		};
	}
	for (const auto &format : options.dragAndDrop.acceptedFormats) {
		configuration.dropTarget.formats.push_back(format.identifier);
	}
	configuration.dropTarget.operation =
		static_cast<std::uint8_t>(options.dragAndDrop.permittedOperations);
	const auto portableDrag =
		[](const ::stana::platform::detail::NativeDragInfo &info) -> DragInfo {
		DragInfo result{
			.location = {.x = Points{info.location.x}, .y = Points{info.location.y}},
			.proposedOperation = static_cast<DragOperation>(info.operation)
		};
		for (const auto &format : info.formats) {
			result.formats.push_back({.identifier = format});
		}
		for (const auto &item : info.items) {
			result.items.push_back({.format = {.identifier = item.format}, .bytes = item.bytes});
		}
		return result;
	};
	const auto drop = std::make_shared<const WindowDragAndDrop>(options.dragAndDrop);
	configuration.dropTarget.negotiate =
		[drop, portableDrag](const auto &info, bool entered) -> std::uint8_t {
		const auto &callback = entered ? drop->onEnter : drop->onMove;
		return static_cast<std::uint8_t>(
			callback ? callback(portableDrag(info)) : drop->permittedOperations
		);
	};
	configuration.dropTarget.leave = drop->onLeave;
	configuration.dropTarget.drop = [drop, portableDrag](const auto &info) -> bool {
		return drop->onDrop && drop->onDrop(portableDrag(info));
	};
	configuration.restoration = {
		.key = options.restoration.identity.key,
		.enabled = options.restoration.policy != WindowRestorationPolicy::disabled &&
			!options.restoration.identity.key.empty(),
		.save = options.restoration.onSave,
		.restore = options.restoration.onRestore
	};
	applyPlatformWindowOptions(configuration, options.platform);
	return configuration;
}

auto validateSessionIdentities(const std::vector<WindowSession> &sessions)
	-> std::expected<void, Error> {
	std::map<std::uint64_t, std::uint64_t> owners;
	std::vector<std::string> restoration;
	for (const auto &session : sessions) {
		if (session.id.value == 0 || owners.contains(session.id.value)) {
			return std::unexpected(Error::invalidStructure);
		}
		if (!validateConfiguration(sampleConfiguration(session.id, session.options))) {
			return std::unexpected(Error::invalidValue);
		}
		const auto configuration = nativeConfigurationFromOptions(session.options);
		owners.emplace(session.id.value, configuration.owner);
		if (configuration.restoration.enabled) {
			if (std::ranges::find(restoration, configuration.restoration.key) !=
				restoration.end()) {
				return std::unexpected(Error::invalidStructure);
			}
			restoration.push_back(configuration.restoration.key);
		}
	}
	for (const auto &[id, owner] : owners) {
		std::vector<std::uint64_t> chain{id};
		auto next = owner;
		while (next != 0) {
			if (!owners.contains(next) || std::ranges::find(chain, next) != chain.end()) {
				return std::unexpected(Error::invalidStructure);
			}
			chain.push_back(next);
			next = owners.at(next);
		}
	}
	return {};
}

auto attachSessionRequestStreams(std::vector<WindowSession> &sessions)
	-> std::expected<void, WindowRequestError> {
	std::vector<WindowRequestStream> attachedStreams;
	for (auto &session : sessions) {
		if (const auto attached = attachRequestStream(session.options.requests); !attached) {
			for (const auto &stream : attachedStreams) {
				closeRequestStream(stream);
			}
			return std::unexpected(attached.error());
		}
		attachedStreams.push_back(session.options.requests);
	}
	return {};
}

void drainSessionRequests(const std::shared_ptr<WindowSessionRuntime> &runtime) {
	if (!runtime || runtime->closed || !runtime->ready || runtime->draining ||
		runtime->options == nullptr) {
		return;
	}
	const auto channel = requestStreamChannel(runtime->options->requests);
	if (!channel) {
		return;
	}
	runtime->draining = true;
	auto pending = channel->drain();
	for (auto &request : pending) {
		runtime->outstanding.emplace(request.id.value, request);
	}
	std::vector<std::uint64_t> ids;
	ids.reserve(pending.size());
	for (const auto &request : pending) {
		ids.push_back(request.id.value);
	}
	for (const auto id : ids) {
		if (runtime->closed) {
			break;
		}
		const auto found = runtime->outstanding.find(id);
		if (found == runtime->outstanding.end()) {
			continue;
		}
		::stana::platform::detail::NativeWindowAction native;
		std::visit(
			[&](const auto &action) -> void {
				using Action = std::decay_t<decltype(action)>;
				if constexpr (std::same_as<Action, FocusWindow>) {
					native.kind = NativeWindowActionKind::focus;
					native.token = action.interaction ? action.interaction->value : 0;
				} else if constexpr (std::same_as<Action, SetWindowState>) {
					native.kind = NativeWindowActionKind::setState;
					native.state = nativeState(action.state);
				} else if constexpr (std::same_as<Action, CloseWindow>) {
					native.kind = NativeWindowActionKind::close;
				} else if constexpr (std::same_as<Action, BeginInteractiveMove>) {
					native.kind = NativeWindowActionKind::beginInteractiveMove;
					native.token = action.interaction.value;
				} else if constexpr (std::same_as<Action, BeginInteractiveResize>) {
					native.kind = NativeWindowActionKind::beginInteractiveResize;
					native.token = action.interaction.value;
					native.edge = static_cast<std::uint8_t>(action.edge);
				} else if constexpr (std::same_as<Action, ShowWindowMenu>) {
					native.kind = NativeWindowActionKind::showWindowMenu;
					native.token = action.interaction.value;
					native.location = {
						.x = action.location.x.value(),
						.y = action.location.y.value()
					};
				} else if constexpr (std::same_as<Action, CaptureWindow>) {
					native.kind = NativeWindowActionKind::capture;
				} else if constexpr (std::same_as<Action, PrintWindow>) {
					native.kind = NativeWindowActionKind::print;
				} else if constexpr (std::same_as<Action, RespondDeferredClose>) {
					native.kind = NativeWindowActionKind::respondDeferredClose;
					native.token = action.request.value;
					native.closeDecision = action.decision == CloseDecision::allow
						? NativeCloseDecision::allow
						: NativeCloseDecision::deny;
				}
			},
			found->second.action
		);
		const auto completed = [weak = std::weak_ptr{runtime}, id](
								   ::stana::platform::detail::NativeActionResult result
							   ) -> void {
			const auto owner = weak.lock();
			if (!owner || owner->closed) {
				return;
			}
			const auto entry = owner->outstanding.find(id);
			if (entry == owner->outstanding.end()) {
				return;
			}
			WindowRequestResult portable{
				.id = {.value = id},
				.outcome = portableOutcome(result.outcome),
				.action = entry->second.action
			};
			if (!result.pixels.empty()) {
				portable.payload = CapturePixels{
					.width = result.width,
					.height = result.height,
					.bytes = std::move(result.pixels)
				};
			}
			if (result.outcome != NativeWindowOutcome::pending) {
				owner->outstanding.erase(entry);
			}
			publishEvent(owner, WindowRequestCompleted{.result = std::move(portable)});
		};
		if (runtime->hostControl && runtime->hostControl->dispatchAction) {
			const auto dispatch = runtime->hostControl->dispatchAction;
			dispatch(native, completed);
		} else {
			completed({.outcome = NativeWindowOutcome::unsupported});
		}
	}
	runtime->draining = false;
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
	window.configuration.id = runtime->id.value;
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
		if (const auto owner = weak.lock();
			owner && owner->hostControl && owner->hostControl->observe) {
			owner->ready = true;
			publishEvent(
				owner,
				WindowReady{
					.snapshot = snapshotFromObservation(
						owner->id,
						owner->hostControl->observe(),
						WindowLifecycle::open
					),
					.capabilities = capabilities(*owner->options)
				}
			);
			pushConfiguration(owner);
			drainSessionRequests(owner);
		}
	};
	window.closeShouldProceed = [weak](std::uint64_t closeRequestId) -> NativeCloseDecision {
		const auto owner = weak.lock();
		if (!owner || !owner->events.onCloseRequested) {
			return NativeCloseDecision::allow;
		}
		switch (owner->events.onCloseRequested(
			CloseRequest{.id = CloseRequestId{.value = closeRequestId}, .window = owner->id}
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
			if (owner->pendingState &&
				owner->pendingState->second == portableState(observation.state)) {
				const auto revision = owner->pendingState->first;
				owner->pendingState.reset();
				publishEvent(
					owner,
					WindowConfigurationCompleted{
						.result = {
							.revision = revision,
							.changed = {WindowFeature::state},
							.results = {
								{.feature = WindowFeature::state,
									.outcome = WindowOutcome::applied,
									.effective = portableState(observation.state)}
							}
						}
					}
				);
			}
			publishEvent(
				owner,
				WindowStateChanged{
					.snapshot =
						snapshotFromObservation(owner->id, observation, WindowLifecycle::open)
				}
			);
		}
	};

	window.interaction = [weak](
							 std::uint8_t phase,
							 ::stana::platform::detail::LogicalPoint point,
							 std::uint64_t token
						 ) -> void {
		if (const auto owner = weak.lock(); owner && !owner->closed) {
			publishEvent(
				owner,
				WindowInteraction{
					.window = owner->id,
					.phase = static_cast<WindowInteractionPhase>(phase),
					.location = {.x = Points{point.x}, .y = Points{point.y}},
					.token =
						token == 0 ? std::nullopt : std::optional{InteractionToken{.value = token}}
				}
			);
		}
	};
	const auto wakeup = ::stana::platform::detail::createMailboxWakeup([weak] -> void {
		if (const auto owner = weak.lock()) {
			drainSessionRequests(owner);
		}
	});
	if (!wakeup) {
		throw std::runtime_error(wakeup.error());
	}
	runtime->requestWakeup = *wakeup;
	if (const auto channel = requestStreamChannel(runtime->options->requests)) {
		channel->setWakeup(wakeup->notify);
	}

	::stana::reactive::Transaction::perform([&] -> void {
		subscribePropertyUpdates(runtime);
	});
}

void teardownSessionRuntime(const std::shared_ptr<WindowSessionRuntime> &runtime) {
	if (!runtime || runtime->closed) {
		return;
	}
	runtime->closed = true;
	if (runtime->pendingState) {
		const auto revision = runtime->pendingState->first;
		runtime->pendingState.reset();
		publishEvent(
			runtime,
			WindowConfigurationCompleted{
				.result = {
					.revision = revision,
					.changed = {WindowFeature::state},
					.results = {
						{.feature = WindowFeature::state, .outcome = WindowOutcome::cancelled}
					}
				}
			}
		);
	}
	if (runtime->requestWakeup.close) {
		runtime->requestWakeup.close();
	}
	auto cancelled = std::move(runtime->outstanding);
	if (runtime->options != nullptr) {
		if (const auto channel = requestStreamChannel(runtime->options->requests)) {
			for (auto &request : channel->drain()) {
				cancelled.emplace(request.id.value, request);
			}
		}
	}
	for (auto &[id, request] : cancelled) {
		static_cast<void>(id);
		publishEvent(
			runtime,
			WindowRequestCompleted{
				.result = {
					.id = request.id,
					.outcome = WindowOutcome::cancelled,
					.action = request.action
				}
			}
		);
	}
	for (auto &subscription : runtime->subscriptions) {
		subscription.cancel();
	}
	runtime->subscriptions.clear();
	if (runtime->hostControl) {
		runtime->hostControl->applyConfiguration = {};
		runtime->hostControl->dispatchAction = {};
		runtime->hostControl->observe = {};
	}
	if (runtime->options != nullptr) {
		closeRequestStream(runtime->options->requests);
	}
	publishEvent(runtime, WindowClosed{.id = runtime->id});
}

} // namespace stana::application::detail
