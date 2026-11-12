#include <cmath>
#include <compare>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

import stana;

namespace {
inline constexpr double invalidOpacity = 1.5;
inline constexpr double halfOpacity = 0.5;
inline constexpr double placementX = 10;
inline constexpr double placementY = 20;
inline constexpr int defaultWidth = 800;
inline constexpr int defaultHeight = 600;
inline constexpr int largerBound = 200;
inline constexpr int smallerBound = 100;
inline constexpr int sampleWidth = 320;
inline constexpr int sampleHeight = 240;
inline constexpr int payloadA = 65;
inline constexpr int payloadB = 66;
inline constexpr int embeddedNullLength = 9;

void require(bool condition, const std::string &message = "application contract failed") {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static_assert(std::is_aggregate_v<stana::application::WindowOptions>);

void defaults() {
	const stana::application::WindowOptions options{};
	const auto snapshot =
		stana::application::detail::sampleConfiguration(stana::application::WindowId{}, options);
	require(snapshot.contentSize.width == stana::Points{defaultWidth}, "default width");
	require(snapshot.contentSize.height == stana::Points{defaultHeight}, "default height");
	require(std::holds_alternative<stana::application::CenteredPlacement>(snapshot.placement));
	require(std::is_eq(snapshot.opacity <=> 1.0), "default opacity");
	require(snapshot.hasShadow, "default shadow");
	require(snapshot.isResizable, "default resizable");
	require(snapshot.isVisible && snapshot.isEnabled && snapshot.acceptsFocus);
	require(snapshot.state == stana::application::WindowState::normal);
	require(stana::application::PlatformWindowOptions{}.empty(), "platform options empty");
}

void validation() {
	const stana::application::WindowOptions options{};
	auto snapshot =
		stana::application::detail::sampleConfiguration(stana::application::WindowId{}, options);
	require(stana::application::detail::validateConfiguration(snapshot).has_value());

	snapshot.opacity = invalidOpacity;
	require(
		!stana::application::detail::validateConfiguration(snapshot) &&
			stana::application::detail::validateConfiguration(snapshot).error() ==
				stana::Error::invalidValue,
		"bad opacity"
	);

	snapshot =
		stana::application::detail::sampleConfiguration(stana::application::WindowId{}, options);
	snapshot.minimumContentSize =
		stana::Size{.width = stana::Points{largerBound}, .height = stana::Points{largerBound}};
	snapshot.maximumContentSize =
		stana::Size{.width = stana::Points{smallerBound}, .height = stana::Points{smallerBound}};
	require(
		!stana::application::detail::validateConfiguration(snapshot) &&
			stana::application::detail::validateConfiguration(snapshot).error() ==
				stana::Error::invalidValue,
		"min greater than max"
	);

	snapshot =
		stana::application::detail::sampleConfiguration(stana::application::WindowId{}, options);
	snapshot.role = stana::application::PopupWindow{};
	require(
		!stana::application::detail::validateConfiguration(snapshot) &&
			stana::application::detail::validateConfiguration(snapshot).error() ==
				stana::Error::invalidStructure,
		"popup without owner"
	);

	snapshot =
		stana::application::detail::sampleConfiguration(stana::application::WindowId{}, options);
	snapshot.placement = stana::application::ExplicitPlacement{
		.origin = {.x = stana::Points{placementX}, .y = stana::Points{placementY}},
		.screen = stana::application::SpecificScreen{}
	};
	require(
		!stana::application::detail::validateConfiguration(snapshot) &&
			stana::application::detail::validateConfiguration(snapshot).error() ==
				stana::Error::invalidValue,
		"invalid screen id"
	);
}

void sampleReadsReactiveInputs() {
	const stana::reactive::Signal title{std::string{"signal-title"}};
	const stana::reactive::Signal size{
		stana::Size{.width = stana::Points{sampleWidth}, .height = stana::Points{sampleHeight}}
	};
	const auto opacity = stana::reactive::Computed<double>{[] -> double {
		return halfOpacity;
	}};
	const stana::application::WindowOptions options{
		.contentSize = size,
		.title = title,
		.opacity = opacity
	};
	const auto snapshot =
		stana::application::detail::sampleConfiguration(stana::application::WindowId{}, options);
	require(snapshot.title == "signal-title");
	require(snapshot.contentSize.width == stana::Points{sampleWidth});
	require(snapshot.contentSize.height == stana::Points{sampleHeight});
	require(std::is_eq(snapshot.opacity <=> halfOpacity));
}

void requestStreamContracts() {
	const stana::application::WindowRequestSource source;
	const auto stream = source.stream();
	require(
		!source.emit(stana::application::CloseWindow{}).has_value() &&
			source.emit(stana::application::CloseWindow{}).error() ==
				stana::application::WindowRequestError::notAttached,
		"emit before attach fails"
	);
	require(stana::application::detail::attachRequestStream(stream).has_value(), "attach once");
	require(
		!stana::application::detail::attachRequestStream(stream).has_value() &&
			stana::application::detail::attachRequestStream(stream).error() ==
				stana::application::WindowRequestError::duplicateStream,
		"attach twice fails"
	);
	const auto first = source.emit(stana::application::FocusWindow{});
	const auto second = source.emit(stana::application::CloseWindow{});
	require(first && second && first->value != second->value, "distinct request ids");
	const auto channel = stana::application::detail::requestStreamChannel(stream);
	require(channel != nullptr);
	const auto pending = channel->drain();
	require(pending.size() == 2, "queued actions retained");
	require(pending.at(0).id == *first && pending.at(1).id == *second);
	stana::application::detail::closeRequestStream(stream);
	require(
		!source.emit(stana::application::CloseWindow{}).has_value() &&
			source.emit(stana::application::CloseWindow{}).error() ==
				stana::application::WindowRequestError::windowClosed,
		"emit after close fails"
	);
	require(
		!source
			.emit(
				stana::application::BeginInteractiveMove{
					.interaction = stana::application::InteractionToken{}
				}
			)
			.has_value(),
		"invalid token fails"
	);
	const stana::application::WindowRequestSource deferred;
	const auto deferredStream = deferred.stream();
	require(stana::application::detail::attachRequestStream(deferredStream).has_value());
	require(
		!deferred
				.emit(
					stana::application::RespondDeferredClose{
						.request = stana::application::CloseRequestId{.value = 1},
						.decision = stana::application::CloseDecision::defer
					}
				)
				.has_value() &&
			deferred.emit(
						stana::application::RespondDeferredClose{
							.request = stana::application::CloseRequestId{.value = 1},
							.decision = stana::application::CloseDecision::defer
						}
			)
					.error() == stana::application::WindowRequestError::invalidAction,
		"RespondDeferredClose with defer invalid"
	);
}

void extendedValidation() {
	using namespace stana::application;
	WindowOptions options{};
	const auto id = WindowId::create();
	const auto valid = [&] -> bool {
		return detail::validateConfiguration(detail::sampleConfiguration(id, options)).has_value();
	};
	options.contentSize = stana::Size{};
	require(!valid(), "zero-sized content rejected");
	options.contentSize =
		stana::Size{.width = stana::Points{sampleWidth}, .height = stana::Points{sampleHeight}};
	options.resizeIncrement = std::optional{stana::Size{.width = stana::Points{1}}};
	require(!valid(), "zero resize increment rejected");
	options.resizeIncrement = std::nullopt;
	options.placement = CenteredPlacement{.screen = SpecificScreen{}};
	require(!valid(), "centered placement validates screen identity");
	options.placement = CenteredPlacement{};
	options.role = UtilityWindow{.owner = id};
	require(!valid(), "self ownership rejected");
	options.role = StandardWindow{};
	options.toolbar = std::optional{WindowToolbar{
		.identifier = "toolbar",
		.items = {{.identifier = "same"}, {.identifier = "same"}}
	}};
	require(!valid(), "duplicate toolbar item identity rejected");
	options.toolbar = std::nullopt;
	options.dragAndDrop.acceptedFormats = {{.identifier = ""}};
	require(!valid(), "empty drop format rejected");
	options.dragAndDrop.acceptedFormats.clear();
	options.title = std::string{"bad\0title", embeddedNullLength};
	require(!valid(), "embedded null in native title rejected");
	options.title = "valid";
	require(valid(), "valid after clearing optional constraints and facilities");
}

void requestWakeups() {
	using namespace stana::application;
	const WindowRequestSource source;
	const auto stream = source.stream();
	require(detail::attachRequestStream(stream).has_value());
	const auto channel = detail::requestStreamChannel(stream);
	std::vector<WindowRequestId> delivered;
	// Reentrant drain proves the producer never invokes the wakeup under its mutex.
	channel->setWakeup([&] -> void {
		for (const auto &request : channel->drain()) {
			delivered.push_back(request.id);
		}
	});
	const auto first = source.emit(FocusWindow{});
	const auto second = source.emit(CloseWindow{});
	require(
		first && second && delivered == std::vector{*first, *second},
		"every emission wakes in order"
	);
	channel->setWakeup({});
	require(source.emit(FocusWindow{}).has_value());
	channel->setWakeup([&] -> void {
		require(channel->drain().size() == 1, "installing wake drains existing requests");
	});
	detail::closeRequestStream(stream);
}

void ownedFacilities() {
	using namespace stana::application;
	int activations = 0;
	const WindowToolbarItem item{.identifier = "save", .label = "Save", .onActivate = [&] -> void {
									 ++activations;
								 }};
	const WindowToolbarItem descriptor{.identifier = "save", .label = "Save"};
	require(item.descriptorEquals(descriptor), "callbacks are not native descriptor identity");
	require(item != descriptor, "callback identity propagates through reactive value equality");
	item.onActivate();
	require(activations == 1);
	DragInfo received;
	{
		const DragInfo source{
			.items = {{.format = {.identifier = "text/plain"}, .bytes = {payloadA, payloadB}}}
		};
		received = source;
	}
	require(
		received.items.at(0).bytes == std::vector<std::uint8_t>{payloadA, payloadB},
		"drop payload outlives native delivery"
	);
}

void windowIdUniqueness() {
	const auto left = stana::application::WindowId::create();
	const auto right = stana::application::WindowId::create();
	require(left.value != 0 && right.value != 0 && left != right, "WindowId::create uniqueness");
}

} // namespace

auto main() -> int {
	try {
		defaults();
		validation();
		sampleReadsReactiveInputs();
		requestStreamContracts();
		windowIdUniqueness();
		extendedValidation();
		requestWakeups();
		ownedFacilities();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
