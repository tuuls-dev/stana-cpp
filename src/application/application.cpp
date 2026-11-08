module;

#include "../platform/native_contracts.hpp"

#include "../gpu/limits.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
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
import :document;
import :rendering;
import :reactive;
#include "gpu_conversion.hpp"
#include "scene_conversion.hpp"

namespace stana::application {
namespace {
using stana::application::detail::convertMode;
using stana::application::detail::SceneConversion;

[[nodiscard]] auto diagnosticCategory(stana::gpu::detail::Diagnostic::Category value)
	-> stana::rendering::GPUDiagnostic::Category {
	switch (value) {
	case stana::gpu::detail::Diagnostic::Category::compiler:
		return stana::rendering::GPUDiagnostic::Category::compiler;
	case stana::gpu::detail::Diagnostic::Category::binding:
		return stana::rendering::GPUDiagnostic::Category::binding;
	case stana::gpu::detail::Diagnostic::Category::capability:
		return stana::rendering::GPUDiagnostic::Category::capability;
	case stana::gpu::detail::Diagnostic::Category::execution:
		return stana::rendering::GPUDiagnostic::Category::execution;
	}
	throw std::logic_error("Unknown GPU diagnostic category");
}

/// @brief Converts the private transport identity without losing retry/surface qualification.
[[nodiscard]] auto portableAttempt(stana::platform::detail::FrameIdentity frame)
	-> stana::rendering::PresentationAttempt {
	return {.epoch = frame.epoch, .sequence = frame.sequence, .sceneRevision = frame.sceneRevision};
}

[[nodiscard]] auto symbolDiagnostic(stana::platform::detail::SymbolFailureCode code)
	-> stana::symbol::Diagnostic {
	using Code = stana::platform::detail::SymbolFailureCode;
	switch (code) {
	case Code::preparationFailed:
		return {
			.code = stana::symbol::Diagnostic::Code::preparationFailed,
			.message = "Symbol path preparation failed; replace the asset or change its "
					   "preparation inputs."
		};
	case Code::resourceLimitExceeded:
		return {
			.code = stana::symbol::Diagnostic::Code::resourceLimitExceeded,
			.message = "Symbol preparation exceeded the renderer resource limit; simplify "
					   "geometry or reduce scale."
		};
	case Code::renderingFailed:
		return {
			.code = stana::symbol::Diagnostic::Code::renderingFailed,
			.message = "Symbol raster rendering failed; retry after renderer recovery."
		};
	}
	throw std::logic_error("Unknown symbol diagnostic");
}

} // namespace

namespace detail {
struct NativeExecutorSession::State {
	std::shared_ptr<stana::reactive::detail::ExecutorEndpoint> endpoint;
	std::shared_ptr<std::string> failure = std::make_shared<std::string>();
};

NativeExecutorSession::NativeExecutorSession() : state_(std::make_unique<State>()) {
	state_->endpoint = stana::reactive::detail::ExecutorEndpoint::current();
	auto wakeup = stana::platform::detail::createMailboxWakeup(
		[weak = std::weak_ptr{state_->endpoint}, failure = state_->failure] -> void {
			try {
				if (const auto endpoint = weak.lock()) {
					endpoint->drain();
				}
			} catch (const std::exception &error) {
				if (failure->empty()) {
					*failure = error.what();
				}
				if (const auto endpoint = weak.lock()) {
					endpoint->close();
				}
				stana::platform::detail::stop();
			} catch (...) {
				if (failure->empty()) {
					*failure = "Unknown queued signal-write failure";
				}
				if (const auto endpoint = weak.lock()) {
					endpoint->close();
				}
				stana::platform::detail::stop();
			}
		}
	);
	if (!wakeup) {
		*state_->failure = wakeup.error();
		return;
	}
	try {
		state_->endpoint->activateNative(std::move(wakeup->notify), std::move(wakeup->close));
	} catch (...) {
		state_->endpoint->close();
		throw;
	}
}

NativeExecutorSession::~NativeExecutorSession() {
	if (state_ && state_->endpoint) {
		state_->endpoint->close();
	}
}

auto NativeExecutorSession::complete(int code) const -> int {
	if (state_ && state_->failure && !state_->failure->empty()) {
		std::cerr << "Stana application failed: " << *state_->failure << '\n';
		return 1;
	}
	return code;
}

void installNativeTaskDispatcher() {
	stana::tasks::detail::TaskScheduler::current().dispatcher =
		[](std::chrono::milliseconds delay, const std::function<void()> &callback) -> void {
		stana::platform::detail::postAfter(delay, callback);
	};
}

auto runWindows(const std::vector<WindowSession> &sessions) -> int {
	try {
		std::vector<stana::platform::detail::Window> windows;
		for (const auto &session : sessions) {
			const auto sessionDocument = session.document;
			sessionDocument->enablePresentationTracking();
			sessionDocument->setTextMeasure(
				[](const std::string &text, Font font, Points width) -> Size {
					const auto [measuredWidth, measuredHeight] =
						stana::platform::detail::measureText(
							text,
							{.size = font.size.value(), .weight = font.weight.value()},
							width.value()
						);
					return {.width = Points{measuredWidth}, .height = Points{measuredHeight}};
				}
			);
			sessionDocument->commit();
			const auto weakDocument = std::weak_ptr{sessionDocument};
			const auto conversion = std::make_shared<SceneConversion>();
			const auto animation = std::make_shared<stana::platform::detail::AnimationGate>();
			sessionDocument->setFrameDemandHandler([animation](bool demand) -> void {
				animation->set(demand);
			});
			conversion->symbolError =
				[weakDocument](stana::platform::detail::SymbolFailure failure) -> void {
				const auto owner = weakDocument.lock();
				if (!owner) {
					return;
				}
				const auto preparation = stana::document::detail::Document::SymbolPreparation{
					.instance =
						{.document = failure.instance.document, .serial = failure.instance.serial},
					.generation = failure.generation,
					.epoch = failure.epoch
				};
				const bool changed = failure.code
					? owner->queueSymbolResult(symbolDiagnostic(*failure.code), preparation)
					: owner->queueSymbolResult(std::nullopt, preparation);
				if (changed || failure.code.has_value()) {
					stana::reactive::detail::ReactiveRuntime::current().flush();
				}
			};
			windows.push_back(
				{.title = session.title,
					.width = session.size.width.value(),
					.height = session.size.height.value(),
					.colorMode = convertMode(session.renderingColorMode),
					.gpuError = [weakDocument](stana::gpu::detail::Diagnostic diagnostic) -> void {
						if (const auto owner = weakDocument.lock()) {
							owner->queueGPUDiagnostic(
								{.category = diagnosticCategory(diagnostic.category),
									.message = std::move(diagnostic.message),
									.instance =
										{.document = diagnostic.document,
											.serial = diagnostic.instance},
									.programVersion = diagnostic.programVersion},
								diagnostic.drawingVersion,
								diagnostic.epoch
							);
							stana::reactive::detail::ReactiveRuntime::current().flush();
						}
					},
					.scene = [weakDocument, conversion] -> stana::platform::detail::SceneResult {
						if (const auto owner = weakDocument.lock()) {
							const auto snapshot = owner->renderSnapshot();
							if (snapshot) {
								return conversion->convert(snapshot);
							}
						}
						return std::unexpected("Window document closed");
					},
					.resize = [weakDocument](double width, double height) -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						document->resize({.width = Points{width}, .height = Points{height}});
					},
					.appearance = [weakDocument](bool isDark) -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						auto environment = document->environment();
						const auto theme = isDark ? Theme::dark : Theme::light;
						if (environment.theme == theme) {
							return;
						}
						environment.theme = theme;
						document->setEnvironment(environment);
					},
					.pointerDown = [weakDocument](double x, double y) -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						document->pointerDown({.x = Points{x}, .y = Points{y}});
					},
					.pointerUp = [weakDocument](double x, double y) -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						document->pointerUp({.x = Points{x}, .y = Points{y}});
					},
					.pointerMove = [weakDocument](double x, double y) -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						document->pointerMove({.x = Points{x}, .y = Points{y}});
					},
					.pointerLeave = [weakDocument]() -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						document->pointerLeave();
					},
					.scroll = [weakDocument](double x, double y, double delta) -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						document->scroll({.x = Points{x}, .y = Points{y}}, Points{delta});
					},
					.key = [weakDocument](
							   stana::platform::detail::KeyAction action,
							   stana::platform::detail::KeyPhase phase
						   ) -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						using stana::document::Key;
						using stana::platform::detail::KeyAction;
						using stana::platform::detail::KeyPhase;
						const bool released = phase == KeyPhase::up;
						switch (action) {
						case KeyAction::tabForward:
							if (released) {
								document->keyUp(Key::tab);
							} else {
								document->keyDown(Key::tab);
							}
							break;
						case KeyAction::tabBackward:
							if (released) {
								document->keyUp(Key::tab, true);
							} else {
								document->keyDown(Key::tab, true);
							}
							break;
						case KeyAction::space:
							if (released) {
								document->keyUp(Key::space);
							} else {
								document->keyDown(Key::space);
							}
							break;
						case KeyAction::enter:
							if (released) {
								document->keyUp(Key::enter);
							} else {
								document->keyDown(Key::enter);
							}
							break;
						}
					},
					.beginPresentation =
						[weakDocument](const stana::platform::detail::PresentationRequest &request)
						-> std::expected<stana::platform::detail::FrameIdentity, std::string> {
						const auto owner = weakDocument.lock();
						if (!owner) {
							return std::unexpected("Window document closed");
						}
						std::vector<stana::rendering::TileId> tiles;
						tiles.reserve(request.expectedTiles.size());
						for (const auto tile : request.expectedTiles) {
							tiles.push_back(stana::rendering::TileId{tile.value});
						}
						const auto attempt =
							owner->beginPresentation(request.epoch, request.sceneRevision, tiles);
						if (!attempt) {
							return std::unexpected("Presentation admission failed");
						}
						return stana::platform::detail::FrameIdentity{
							.epoch = attempt->epoch,
							.sequence = attempt->sequence,
							.sceneRevision = attempt->sceneRevision
						};
					},
					.gpuCompleted = [weakDocument](
										stana::platform::detail::FrameIdentity frame,
										bool success
									) -> bool {
						const auto owner = weakDocument.lock();
						return owner && owner->reportGPUCompletion(portableAttempt(frame), success);
					},
					.drawablePresented = [weakDocument](
											 stana::platform::detail::FrameIdentity frame,
											 stana::platform::detail::TilePresentation observation
										 ) -> bool {
						const auto owner = weakDocument.lock();
						return owner &&
							owner->reportPresentation(
								portableAttempt(frame),
								stana::rendering::TilePresentation{
									.tile = stana::rendering::TileId{observation.tile.value},
									.timestamp = PresentationTimePoint{
										PresentationDuration{observation.timestamp}
									}
								}
							);
					},
					.abandonPresentation =
						[weakDocument](stana::platform::detail::FrameIdentity frame) -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						document->abandonPresentation(portableAttempt(frame));
					},
					.resetPresentation = [weakDocument](std::uint64_t epoch) -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						document->resetPresentation(epoch);
					},
					.holdPreparedGeometry = [weakDocument](std::uint64_t revision) -> void {
						if (const auto owner = weakDocument.lock()) {
							owner->holdPreparedGeometry(revision);
						}
					},
					.gatePendingPresentation =
						[weakDocument](stana::platform::detail::FrameIdentity frame) -> void {
						if (const auto owner = weakDocument.lock()) {
							owner->gatePendingPresentation(portableAttempt(frame));
						}
					},
					.gateSurface = [weakDocument] -> void {
						if (const auto owner = weakDocument.lock()) {
							owner->gateSurface();
						}
					},
					.close = [weakDocument] -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						document->close();
					},
					.ready = [weakDocument](std::function<void()> invalidate) -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						document->setOnPublish(std::move(invalidate));
					},
					.frame = [weakDocument](double timestamp, double deadline) -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						document->tickFrames(
							{.timestamp = FrameTimePoint{FrameDuration{timestamp}},
								.targetTimestamp = FrameTimePoint{FrameDuration{deadline}}}
						);
						stana::reactive::detail::ReactiveRuntime::current().flush();
						document->commit();
					},
					.hostVisible = [weakDocument](bool visible) -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						document->setHostVisible(visible);
						document->commit();
					},
					.animation = animation}
			);
		}
		const auto result = stana::platform::detail::run(windows);
		stana::tasks::detail::TaskScheduler::current().dispatcher = {};
		for (const auto &session : sessions) {
			session.document->close();
		}
		if (!result) {
			std::cerr << "Stana: " << result.error() << '\n';
			return 1;
		}
		return *result;
	} catch (const std::exception &error) {
		stana::tasks::detail::TaskScheduler::current().dispatcher = {};
		std::cerr << "Stana application failed: " << error.what() << '\n';
		return 1;
	}
}
} // namespace detail

auto Application::renderingDiagnostics() -> std::vector<stana::rendering::WindowDiagnostics> {
	std::vector<stana::rendering::WindowDiagnostics> result;
	for (const auto &window : stana::platform::detail::diagnostics()) {
		result.push_back(
			{.deviceName = window.deviceName,
				.unifiedMemory = window.unifiedMemory,
				.linearWorking = window.linearWorking,
				.linearOutput = window.linearOutput,
				.meshSampleCount = window.meshSampleCount,
				.offscreenRevision = window.offscreenRevision,
				.stateSteps = window.stateSteps,
				.pipelineBuilds = window.pipelineBuilds,
				.pipelineCacheHits = window.pipelineCacheHits,
				.resourceUploadBytes = window.resourceUploadBytes,
				.compositedTiles = window.compositedTiles,
				.gpuLeafDraws = window.gpuLeafDraws,
				.shadowMaskBuilds = window.shadowMaskBuilds,
				.shadowBlurPasses = window.shadowBlurPasses,
				.shadowCacheHits = window.shadowCacheHits,
				.shadowMaskPixels = window.shadowMaskPixels,
				.trackedBytes = window.trackedBytes,
				.peakTrackedBytes = window.peakTrackedBytes,
				.managedWriteBytes = window.managedWriteBytes,
				.surfaceEpoch = window.surfaceEpoch,
				.submittedSceneRevision = window.submittedRevision == 0
					? std::nullopt
					: std::optional{window.submittedRevision},
				.confirmedSceneRevision = window.confirmedRevision == 0
					? std::nullopt
					: std::optional{window.confirmedRevision},
				.lastFailure =
					window.failure.empty() ? std::nullopt : std::optional{window.failure},
				.renderingWork = {
					.tileCount = window.renderingWork.tileCount,
					.rasterizedTiles = window.renderingWork.rasterizedTiles,
					.presentedTiles = window.renderingWork.presentedTiles,
					.rasterizedPixels = window.renderingWork.rasterizedPixels,
					.copiedBytes = window.renderingWork.copiedBytes,
					.cpuUploadBytes = window.renderingWork.cpuUploadBytes,
					.bufferAllocations = window.renderingWork.bufferAllocations,
					.rasterBytes = window.renderingWork.rasterBytes,
					.peakRasterBytes = window.renderingWork.peakRasterBytes,
					.estimatedDrawableBytes = window.renderingWork.estimatedDrawableBytes,
					.fullDamageFallbacks = window.renderingWork.fullDamageFallbacks,
					.recoveries = window.renderingWork.recoveries,
					.anchorAttempts = window.renderingWork.anchorAttempts,
					.hasActiveBatch = window.renderingWork.hasActiveBatch,
					.hasPendingScene = window.renderingWork.hasPendingScene
				}}
		);
	}
	return result;
}

void Application::stop() {
	stana::platform::detail::stop();
}

void Application::postAfter(
	std::chrono::milliseconds delay,
	const std::function<void()> &callback
) {
	stana::platform::detail::postAfter(delay, callback);
}
} // namespace stana::application
