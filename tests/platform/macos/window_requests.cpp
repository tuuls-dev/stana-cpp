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
#include <tuple>
#include <variant>
#include <vector>

import stana;

namespace {
inline constexpr auto captureWidth = 64;
inline constexpr std::chrono::milliseconds defaultPresentationDelay{300};
inline constexpr std::chrono::milliseconds manualPresentationDelay{2000};

[[nodiscard]] auto presentationDelay() -> std::chrono::milliseconds {
	return !stana::testing::fixtures::platform::macos::manualFacilities() ? defaultPresentationDelay
																		  : manualPresentationDelay;
}

inline constexpr auto captureHeight = 48;
inline constexpr auto maximumByte = 255;
inline constexpr auto staleRequest = 999;
inline constexpr auto watchdogSeconds = 10;
inline constexpr auto settlementMillis = 20;
inline constexpr auto invalidOpacity = 1.5;
inline constexpr auto acceptedOpacity = 0.75;
inline constexpr double half = 0.5;
inline constexpr double twice = 2.0;

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
	unsigned activations = 0;
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
	const stana::reactive::Signal<std::optional<WindowToolbar>> toolbar{std::optional{WindowToolbar{
		.identifier = "live-toolbar",
		.items = {{.identifier = "activate", .label = "Activate", .onActivate = [probe] -> void {
					   ++probe->activations;
				   }}}
	}}};
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
				.toolbar = toolbar,
				.requests = requests->stream()},
		.events = {
			.onEvent =
				[probe, requests, opacity, title, emit, toolbar](const WindowEvent &event) -> void {
				if (const auto *ready = std::get_if<WindowReady>(&event)) {
					probe->ready = true;
					require(
						stana::testing::fixtures::platform::macos::activateToolbar(
							"Stana hidden request contracts"
						),
						"initial toolbar activation"
					);
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
						[opacity, title, emit, toolbar, probe] -> void {
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
								auto replacement = toolbar.get();
								replacement->items.front().onActivate = [probe] -> void {
									constexpr unsigned increment = 10;
									probe->activations += increment;
								};
								toolbar.set(std::move(replacement));
							});
							require(
								stana::testing::fixtures::platform::macos::activateToolbar(
									"Stana settled hidden window"
								),
								"callback replacement preserves native toolbar item"
							);
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
	constexpr unsigned expectedActivations = 11;
	require(
		probe->activations == expectedActivations,
		"live callback replacement propagates without rebuilding native items"
	);
	require(
		probe->configurations == 3,
		"initial, invalid, and coalesced valid configuration transactions"
	);
	return 0;
}

auto mixedCaptureContracts() -> int {
	const auto requests = std::make_shared<WindowRequestSource>();
	const auto captured = std::make_shared<bool>(false);
	stana::rendering::Drawing cpu;
	cpu.fillRect(
		{.size = {.width = stana::Points{captureWidth}, .height = stana::Points{captureHeight}}},
		stana::Color::rgb(1, 0, 0)
	);
	stana::rendering::GPUDrawing gpu;
	gpu.fillRect(
		{.size =
				{.width = stana::Points{static_cast<double>(captureWidth) / twice},
					.height = stana::Points{captureHeight}}},
		stana::Color::rgb(0, 1, 0)
	);
	const stana::Frame fill{.width = stana::Length::fill(), .height = stana::Length::fill()};
	const Window window{
		.content =
			stana::ui::ZStack{
				.children = stana::ui::children(
					stana::ui::CPUCanvas{.drawing = cpu, .layout = {.frame = fill}},
					stana::ui::GPUCanvas{.drawing = gpu, .layout = {.frame = fill}}
				)
			},
		.options =
			{.renderingColorMode = stana::rendering::RenderingColorMode::sdr,
				.contentSize =
					stana::Size{
						.width = stana::Points{captureWidth},
						.height = stana::Points{captureHeight}
					},
				.title = "Stana mixed capture contracts",
				.isVisible = false,
				.requests = requests->stream()},
		.events = {.onEvent = [requests, captured](const WindowEvent &event) -> void {
			if (std::holds_alternative<WindowReady>(event)) {
				require(requests->emit(CaptureWindow{}).has_value(), "mixed capture accepted");
			}
			if (const auto *completion = std::get_if<WindowRequestCompleted>(&event);
				completion && completion->result.outcome == WindowOutcome::applied) {
				const auto *pixels = std::get_if<CapturePixels>(&completion->result.payload);
				require(pixels != nullptr, "mixed capture payload");
				const auto sample = [&](std::uint32_t x, std::uint32_t channel) -> std::uint8_t {
					return pixels->bytes.at(
						(static_cast<std::size_t>(pixels->height / 2) * pixels->width + x) * 4 +
						channel
					);
				};
				require(
					sample(pixels->width / 4, 0) == 0 &&
						sample(pixels->width / 4, 1) == maximumByte,
					"GPU pixels captured offscreen"
				);
				require(
					sample(pixels->width * 3 / 4, 0) == maximumByte &&
						sample(pixels->width * 3 / 4, 1) == 0,
					"CPU pixels captured behind GPU content"
				);
				*captured = true;
				Application::stop();
			} else if (completion && completion->result.outcome != WindowOutcome::pending) {
				Application::stop();
			}
		}}
	};
	Application::postAfter(std::chrono::seconds{watchdogSeconds}, [captured] -> void {
		if (!*captured) {
			Application::stop();
		}
	});
	const int result = Application::run(window);
	require(result == 0 && *captured, "mixed CPU/GPU content captured while hidden");
	return 0;
}

auto transparentCaptureContracts() -> int {
	const auto requests = std::make_shared<WindowRequestSource>();
	const auto captured = std::make_shared<bool>(false);
	const auto stage = std::make_shared<unsigned>(0);
	const stana::reactive::Signal<WindowBackground> background{TransparentBackground{}};
	constexpr std::uint8_t halfAlpha = 128;
	const Window window{
		.content = stana::ui::Text{.text = ""},
		.options =
			{.contentSize =
					stana::Size{
						.width = stana::Points{captureWidth},
						.height = stana::Points{captureHeight}
					},
				.title = "Stana transparent capture contracts",
				.background = background,
				.isVisible = false,
				.requests = requests->stream()},
		.events = {
			.onEvent = [requests, captured, stage, background](const WindowEvent &event) -> void {
				if (std::holds_alternative<WindowReady>(event)) {
					require(
						!stana::testing::fixtures::platform::macos::surfaceIsOpaque(
							"Stana transparent capture contracts"
						),
						"native transparent surface remains compositable"
					);
					require(
						requests->emit(CaptureWindow{}).has_value(),
						"transparent capture accepted"
					);
				}
				if (const auto *completion = std::get_if<WindowRequestCompleted>(&event);
					completion && completion->result.outcome == WindowOutcome::applied) {
					const auto *pixels = std::get_if<CapturePixels>(&completion->result.payload);
					require(pixels != nullptr, "capture payload present");
					if (*stage == 0) {
						require(pixels->bytes.at(3) == 0, "transparent capture preserves alpha");
						++*stage;
						background.set(SolidBackground{.color = stana::Color::rgb(1, 0, 0, half)});
						require(
							requests->emit(CaptureWindow{}).has_value(),
							"translucent capture accepted"
						);
					} else if (*stage == 1) {
						require(
							pixels->bytes.at(0) == maximumByte && pixels->bytes.at(1) == 0 &&
								pixels->bytes.at(3) == halfAlpha,
							"capture normalizes straight alpha without double filling"
						);
						++*stage;
						background.set(SystemBackground{});
						require(
							requests->emit(CaptureWindow{}).has_value(),
							"system background reset captured"
						);
					} else {
						require(
							pixels->bytes.at(3) == maximumByte &&
								stana::testing::fixtures::platform::macos::surfaceIsOpaque(
									"Stana transparent capture contracts"
								),
							"system background restores opaque native and render surfaces"
						);
						*captured = true;
						Application::stop();
					}
				} else if (completion && completion->result.outcome != WindowOutcome::pending) {
					Application::stop();
				}
			}
		}
	};
	Application::postAfter(std::chrono::seconds{watchdogSeconds}, [captured] -> void {
		if (!*captured) {
			Application::stop();
		}
	});
	const int result = Application::run(window);
	require(result == 0 && *captured, "transparent offscreen capture completes");
	return 0;
}

auto menuAndPrintContracts() -> int {
	const auto requests = std::make_shared<WindowRequestSource>();
	const auto completed = std::make_shared<bool>(false);
	const auto menuShown = std::make_shared<bool>(false);

	const Window window{
		.content = stana::ui::Text{.text = "Native menu and print cancellation"},
		.options =
			{.contentSize =
					stana::Size{
						.width = stana::Points{captureWidth * 4},
						.height = stana::Points{captureHeight * 4}
					},
				.title = "Stana menu and print contracts",
				.requests = requests->stream()},
		.events = {.onEvent = [requests, completed, menuShown](const WindowEvent &event) -> void {
			if (std::holds_alternative<WindowReady>(event)) {
				Application::postAfter(presentationDelay(), [] -> void {
					stana::testing::fixtures::platform::macos::pressWindow(
						"Stana menu and print contracts"
					);
				});
			}
			if (const auto *interaction = std::get_if<WindowInteraction>(&event);
				interaction && interaction->token) {
				require(
					requests
						->emit(
							ShowWindowMenu{
								.interaction = *interaction->token,
								.location = interaction->location
							}
						)
						.has_value(),
					"gesture menu request accepted"
				);
				Application::postAfter(presentationDelay(), [] -> void {
					stana::testing::fixtures::platform::macos::dismissWindowMenu();
				});
			}
			if (const auto *completion = std::get_if<WindowRequestCompleted>(&event)) {
				if (std::holds_alternative<ShowWindowMenu>(completion->result.action) &&
					completion->result.outcome == WindowOutcome::applied) {
					*menuShown = true;
					require(requests->emit(PrintWindow{}).has_value(), "print request accepted");
					// Poll only the real modal panel; cancellation must not submit a print job.
					for (int attempt = 1; attempt <= 4; ++attempt) {
						Application::postAfter(presentationDelay() * attempt, [] -> void {
							stana::testing::fixtures::platform::macos::cancelPrintPanel();
						});
					}
				} else if (std::holds_alternative<PrintWindow>(completion->result.action) &&
					completion->result.outcome == WindowOutcome::cancelled) {
					*completed = true;
					Application::stop();
				} else if (completion->result.outcome != WindowOutcome::pending) {
					Application::stop();
				}
			}
		}}
	};
	Application::postAfter(std::chrono::seconds{watchdogSeconds}, [completed] -> void {
		if (!*completed) {
			Application::stop();
		}
	});
	const int result = Application::run(window);
	require(
		result == 0 && *completed && *menuShown,
		"native menu shown and print cancelled without a job"
	);
	return 0;
}

auto ownerClosureContracts() -> int {
	const auto source = std::make_shared<WindowRequestSource>();
	const auto closed = std::make_shared<unsigned>(0);
	const auto childReady = std::make_shared<bool>(false);
	const auto owner = WindowId::create();
	const Window parent{
		.id = owner,
		.content = stana::ui::Text{.text = "Owner closure"},
		.options = {.title = "Stana owner lifecycle contracts", .requests = source->stream()},
		.events = {.onEvent = [source, closed](const WindowEvent &event) -> void {
			if (std::holds_alternative<WindowReady>(event)) {
				require(source->emit(CloseWindow{}).has_value(), "owner closes during readiness");
			}
			if (std::holds_alternative<WindowClosed>(event)) {
				++*closed;
			}
		}}
	};
	const Window child{
		.content = stana::ui::Text{.text = "Unpresented modal child"},
		.options =
			{.role = DialogWindow{.owner = owner, .modal = true},
				.title = "Stana owned modal lifecycle contracts"},
		.events = {.onEvent = [closed, childReady](const WindowEvent &event) -> void {
			if (std::holds_alternative<WindowReady>(event)) {
				*childReady = true;
			}
			if (std::holds_alternative<WindowClosed>(event)) {
				++*closed;
			}
		}}
	};
	const int result = Application::run(std::tuple{parent, child});
	require(
		result == 0 && *closed == 2 && !*childReady,
		"owner closure cancels unpresented modal children without substituting roles"
	);
	return 0;
}

} // namespace

auto main() -> int {
	int result = 1;
	try {
		result = runContracts();
		if (result == 0) {
			result = mixedCaptureContracts();
		}
		if (result == 0) {
			result = transparentCaptureContracts();
		}
		if (result == 0) {
			result = menuAndPrintContracts();
		}
		if (result == 0) {
			result = ownerClosureContracts();
		}
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
	}
	const auto path = stana::testing::fixtures::platform::macos::resultPath();
	if (!path.empty()) {
		std::ofstream{path} << result;
	}
	return result;
}
