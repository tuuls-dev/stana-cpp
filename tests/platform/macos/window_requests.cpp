#include "window_fixture.hpp"
#include <chrono>
#include <compare>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

import stana;

namespace {
inline constexpr auto captureWidth = 64;
inline constexpr auto captureHeight = 48;
inline constexpr auto maximumByte = 255;
inline constexpr auto staleRequest = 999;
inline constexpr auto watchdogSeconds = 10;
inline constexpr auto settlementMillis = 20;
inline constexpr auto invalidOpacity = 1.5;
inline constexpr auto acceptedOpacity = 0.75;

using namespace stana::application;
using namespace stana::literals;

struct Probe {
	double scale = 1;
	bool ready = false;
	bool closed = false;
	bool invalid = false;
	bool capture = false;
	bool denied = false;
	bool stale = false;
	bool cancelled = false;
	unsigned configurations = 0;
	unsigned closeAttempts = 0;
	std::string failure;
};

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

auto runContracts() -> int {
	const auto probe = std::make_shared<Probe>();
	const auto requests = std::make_shared<WindowRequestSource>();
	const stana::reactive::Signal opacity{1.0};
	const stana::reactive::Signal title{std::string{"Stana hidden request contracts"}};
	const auto emit = [requests](WindowAction action) -> void {
		require(requests->emit(action).has_value(), "request emission");
	};
	const Window window{
		.content = stana::ui::Text{.text = ""},
		.options =
			{.contentSize =
					stana::Size{
						.width = stana::Points{captureWidth},
						.height = stana::Points{captureHeight}
					},
				.title = title,
				.background = SolidBackground{.color = stana::Color::rgb(1, 0, 0)},
				.opacity = opacity,
				.isVisible = false,
				.requests = requests->stream()},
		.events = {
			.onEvent = [probe, requests, opacity, title, emit](const WindowEvent &event) -> void {
				if (const auto *ready = std::get_if<WindowReady>(&event)) {
					probe->ready = true;
					probe->scale = ready->snapshot.backingScale.value();
					require(!ready->snapshot.isVisible, "hidden initial state observed");
					require(
						ready->capabilities.features.size() ==
							static_cast<std::size_t>(WindowFeature::nativeLevel) + 1,
						"complete capabilities matrix"
					);
					Application::postAfter(std::chrono::milliseconds{0}, [opacity] -> void {
						opacity.set(invalidOpacity);
					});
					Application::postAfter(
						std::chrono::milliseconds{settlementMillis},
						[opacity, title, emit] -> void {
							require(
								std::is_eq(
									stana::testing::fixtures::platform::macos::windowOpacity(
										"Stana hidden request contracts"
									) <=> 1.0
								),
								"invalid snapshot preserves native opacity"
							);
							stana::reactive::Transaction::perform([&] -> void {
								opacity.set(acceptedOpacity);
								title.set("Stana settled hidden window");
							});
							emit(CaptureWindow{});
						}
					);
				} else if (const auto *configuration =
							   std::get_if<WindowConfigurationCompleted>(&event)) {
					++probe->configurations;
					for (const auto &row : configuration->result.results) {
						if (row.outcome == WindowOutcome::invalid) {
							probe->invalid = true;
						}
					}
				} else if (const auto *completed = std::get_if<WindowRequestCompleted>(&event)) {
					const auto &result = completed->result;
					if (std::holds_alternative<CaptureWindow>(result.action) &&
						result.outcome == WindowOutcome::applied) {
						const auto *pixels = std::get_if<CapturePixels>(&result.payload);
						require(
							pixels != nullptr &&
								pixels->width ==
									static_cast<std::uint32_t>(captureWidth * probe->scale) &&
								pixels->height ==
									static_cast<std::uint32_t>(captureHeight * probe->scale),
							"capture dimensions exclude chrome"
						);
						require(
							pixels->bytes.size() ==
								static_cast<std::size_t>(pixels->width) * pixels->height * 4,
							"capture tightly packed"
						);
						require(
							pixels->bytes.at(0) == maximumByte && pixels->bytes.at(1) == 0 &&
								pixels->bytes.at(2) == 0 && pixels->bytes.at(3) == maximumByte,
							"capture content RGBA channels"
						);
						probe->capture = true;
						emit(CloseWindow{});
					} else if (std::holds_alternative<CloseWindow>(result.action) &&
						result.outcome == WindowOutcome::denied) {
						probe->denied = true;
						emit(
							RespondDeferredClose{
								.request = {.value = staleRequest},
								.decision = CloseDecision::allow
							}
						);
						emit(CloseWindow{});
					} else if (std::holds_alternative<RespondDeferredClose>(result.action) &&
						result.outcome == WindowOutcome::invalid) {
						probe->stale = true;
					} else if (std::holds_alternative<CloseWindow>(result.action) &&
						result.outcome == WindowOutcome::applied) {
						emit(CaptureWindow{});
					} else if (result.outcome == WindowOutcome::cancelled) {
						probe->cancelled = true;
					} else if (result.outcome == WindowOutcome::failed) {
						probe->failure = "native request failed";
						Application::stop();
					}
				} else if (std::holds_alternative<WindowClosed>(event)) {
					probe->closed = true;
					require(probe->cancelled, "cancellation precedes WindowClosed");
					require(
						!requests->emit(CloseWindow{}).has_value(),
						"closed stream rejects emissions"
					);
				}
			},
			.onCloseRequested = [probe, emit](const CloseRequest &request) -> CloseDecision {
				if (++probe->closeAttempts == 1) {
					emit(
						RespondDeferredClose{.request = request.id, .decision = CloseDecision::deny}
					);
					return CloseDecision::defer;
				}
				return CloseDecision::allow;
			}
		}
	};
	Application::postAfter(std::chrono::seconds{watchdogSeconds}, [probe] -> void {
		if (!probe->closed) {
			probe->failure = "hidden request wakeup timed out";
			Application::stop();
		}
	});
	const int result = Application::run(window);
	require(result == 0 && probe->failure.empty(), "native execution: " + probe->failure);
	require(
		probe->ready && probe->invalid && probe->capture && probe->denied && probe->stale &&
			probe->closed,
		"request lifecycle completed"
	);
	require(
		probe->configurations == 3,
		"initial, invalid, and coalesced valid configuration transactions"
	);
	return 0;
}
} // namespace

auto main() -> int {
	int result = 1;
	try {
		result = runContracts();
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
	}
	const auto path = stana::testing::fixtures::platform::macos::resultPath();
	if (!path.empty()) {
		std::ofstream{path} << result;
	}
	return result;
}
