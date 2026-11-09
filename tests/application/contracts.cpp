#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

import stana;

namespace {

void require(bool condition, const std::string &message = "application contract failed") {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void defaults() {
	const stana::application::WindowOptions options{};
	const auto snapshot =
		stana::application::detail::sampleConfiguration(stana::application::WindowId{}, options);
	require(snapshot.contentSize.width == stana::Points{800}, "default width");
	require(snapshot.contentSize.height == stana::Points{600}, "default height");
	require(std::holds_alternative<stana::application::CenteredPlacement>(snapshot.placement));
	require(snapshot.opacity == 1.0, "default opacity");
	require(snapshot.hasShadow, "default shadow");
	require(snapshot.isResizable, "default resizable");
	require(snapshot.isVisible && snapshot.isEnabled && snapshot.acceptsFocus);
	require(snapshot.state == stana::application::WindowState::normal);
	require(stana::application::PlatformWindowOptions{}.empty(), "platform options empty");
}

void validation() {
	stana::application::WindowOptions options{};
	auto snapshot =
		stana::application::detail::sampleConfiguration(stana::application::WindowId{}, options);
	require(stana::application::detail::validateConfiguration(snapshot).has_value());

	snapshot.opacity = 1.5;
	require(
		!stana::application::detail::validateConfiguration(snapshot) &&
			stana::application::detail::validateConfiguration(snapshot).error() ==
				stana::Error::invalidValue,
		"bad opacity"
	);

	snapshot =
		stana::application::detail::sampleConfiguration(stana::application::WindowId{}, options);
	snapshot.minimumContentSize =
		stana::Size{.width = stana::Points{200}, .height = stana::Points{200}};
	snapshot.maximumContentSize =
		stana::Size{.width = stana::Points{100}, .height = stana::Points{100}};
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
		.origin = {.x = stana::Points{10}, .y = stana::Points{20}},
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
		stana::Size{.width = stana::Points{320}, .height = stana::Points{240}}
	};
	stana::application::WindowOptions options{};
	options.appearance.title = title;
	options.geometry.contentSize = size;
	options.appearance.opacity = 0.5;
	const auto snapshot =
		stana::application::detail::sampleConfiguration(stana::application::WindowId{}, options);
	require(snapshot.title == "signal-title");
	require(snapshot.contentSize.width == stana::Points{320});
	require(snapshot.contentSize.height == stana::Points{240});
	require(snapshot.opacity == 0.5);
}

void requestStreamContracts() {
	stana::application::WindowRequestSource source;
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
	stana::application::WindowRequestSource deferred;
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
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
