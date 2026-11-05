module;

#include "../platform/native_contracts.hpp"

#include "../graphics/limits.hpp"
#include <algorithm>
#include <array>
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

module stana.application;
import stana.core;
import stana.graphics;
#include "gpu_conversion.hpp"

namespace stana {
namespace {
using application_detail::convertMode;
using application_detail::GPUConversion;

[[nodiscard]] auto diagnosticCategory(native::gpu::Diagnostic::Category value)
	-> GPUDiagnostic::Category {
	switch (value) {
	case native::gpu::Diagnostic::Category::compiler:
		return GPUDiagnostic::Category::compiler;
	case native::gpu::Diagnostic::Category::binding:
		return GPUDiagnostic::Category::binding;
	case native::gpu::Diagnostic::Category::capability:
		return GPUDiagnostic::Category::capability;
	case native::gpu::Diagnostic::Category::execution:
		return GPUDiagnostic::Category::execution;
	}
	throw std::logic_error("Unknown GPU diagnostic category");
}

/// @brief Converts the private transport identity without losing retry/surface qualification.
[[nodiscard]] auto portableAttempt(native::FrameIdentity frame) -> PresentationAttempt {
	return {.epoch = frame.epoch, .sequence = frame.sequence, .sceneRevision = frame.sceneRevision};
}

[[nodiscard]] auto convertCommand(const PaintCommand &command) -> native::Command {

	native::PaintOperation operation{};
	switch (command.kind) {
	case PaintKind::fillRect:
		operation = native::PaintOperation::fill;
		break;
	case PaintKind::strokeRect:
		operation = native::PaintOperation::stroke;
		break;
	case PaintKind::text:
		operation = native::PaintOperation::text;
		break;
	case PaintKind::line:
		operation = native::PaintOperation::line;
		break;
	case PaintKind::pushClip:
		operation = native::PaintOperation::pushClip;
		break;
	case PaintKind::popClip:
		operation = native::PaintOperation::popClip;
		break;
	}
	return {
		.operation = operation,
		.x = command.bounds.origin.x,
		.y = command.bounds.origin.y,
		.width = command.bounds.size.width,
		.height = command.bounds.size.height,
		.red = command.color.red,
		.green = command.color.green,
		.blue = command.color.blue,
		.alpha = command.color.alpha,
		.radius = command.cornerRadius,
		.lineWidth = command.lineWidth,
		.text = command.text,
		.fontSize = command.font.size,
		.fontWeight = command.font.weight
	};
}

[[nodiscard]] auto convertRect(Rect rect) -> native::LogicalRect {
	return {
		.origin = {.x = rect.origin.x, .y = rect.origin.y},
		.size = {.width = rect.size.width, .height = rect.size.height}
	};
}

/// @brief One window's last paired roots; no process-global cache or borrowed-address lookup.
struct SceneConversion {
	GPUConversion gpuConversion;
	std::shared_ptr<const detail::RenderSnapshot> source;
	std::shared_ptr<const native::Scene> converted;

	[[nodiscard]] auto branch(
		const std::shared_ptr<const detail::SceneBranch> &next,
		const std::shared_ptr<const detail::SceneBranch> &old,
		const std::shared_ptr<const native::SceneBranch> &previous
	) -> std::shared_ptr<const native::SceneBranch> {
		if (!next) {
			return {};
		}
		if (old && previous && next->id == old->id && next->generation == old->generation) {
			return previous;
		}
		auto result = std::make_shared<native::SceneBranch>();
		result->id = {.document = next->id.document, .serial = next->id.serial};
		result->generation = next->generation;
		result->visible = next->context.visible;
		if (old && previous && next->paint->identity == old->paint->identity) {
			result->paint = previous->paint;
		} else {
			auto paint = std::make_shared<native::PaintSegments>();
			paint->identity = next->paint->identity;
			for (const auto &command : next->paint->prefix) {
				paint->prefix.push_back(convertCommand(command));
			}
			for (const auto &command : next->paint->suffix) {
				paint->suffix.push_back(convertCommand(command));
			}
			if (next->paint->childClipBounds) {
				paint->childClipBounds = convertRect(*next->paint->childClipBounds);
			}
			if (const auto &gpu = next->paint->gpu) {
				paint->gpu = native::GPUCanvasLeaf{
					.instance =
						{.document = gpu->instance.document, .serial = gpu->instance.serial},
					.bounds = convertRect(gpu->bounds),
					.drawing = gpuConversion.drawing(gpu->drawing, gpu->environment),
					.opacity = gpu->opacity
				};
			}
			result->paint = std::move(paint);
		}
		std::map<NodeId, std::size_t> pairs;
		if (old && previous) {
			for (std::size_t index = 0; index < old->children.size(); ++index) {
				pairs.emplace(old->children.at(index)->id, index);
			}
		}
		for (const auto &child : next->children) {
			const auto paired = pairs.find(child->id);
			result->children.push_back(
				paired == pairs.end() ? branch(child, {}, {})
									  : branch(
											child,
											old->children.at(paired->second),
											previous->children.at(paired->second)
										)
			);
		}
		return result;
	}

	[[nodiscard]] auto convert(std::shared_ptr<const detail::RenderSnapshot> next)
		-> std::shared_ptr<const native::Scene> {
		if (next == source) {
			return converted;
		}
		auto scene = std::make_shared<native::Scene>();
		scene->revision = next->revision;
		scene->size = {.width = next->size.width, .height = next->size.height};
		scene->background = {
			.red = next->background.red,
			.green = next->background.green,
			.blue = next->background.blue,
			.alpha = next->background.alpha
		};
		scene->textEnvironment = next->textEnvironment;
		scene->root = branch(
			next->root,
			source ? source->root : nullptr,
			converted ? converted->root : nullptr
		);
		source = std::move(next);
		converted = std::move(scene);
		return converted;
	}
};

} // namespace

namespace detail {
void installNativeTaskDispatcher() {
	TaskScheduler::current().dispatcher = [](std::chrono::milliseconds delay,
											  std::function<void()> callback) -> void {
		native::postAfter(delay, std::move(callback));
	};
}

auto runWindows(const std::vector<WindowSession> &sessions) -> int {
	try {
		std::vector<native::Window> windows;
		for (const auto &session : sessions) {
			const auto sessionDocument = session.document;
			sessionDocument->enablePresentationTracking();
			sessionDocument->setTextMeasure(
				[](const std::string &text, Font font, double width) -> Size {
					const auto [measuredWidth, measuredHeight] = native::measureText(
						text,
						{.size = font.size, .weight = font.weight},
						width
					);
					return {.width = measuredWidth, .height = measuredHeight};
				}
			);
			sessionDocument->commit();
			const auto weakDocument = std::weak_ptr{sessionDocument};
			const auto conversion = std::make_shared<SceneConversion>();
			windows.push_back(
				{.title = session.title,
					.width = session.size.width,
					.height = session.size.height,
					.colorMode = convertMode(session.renderingColorMode),
					.gpuError = [weakDocument](native::gpu::Diagnostic diagnostic) -> void {
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
							detail::ReactiveRuntime::current().flush();
						}
					},
					.scene = [weakDocument, conversion] -> native::SceneResult {
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
						document->resize({.width = width, .height = height});
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
						document->pointerDown({.x = x, .y = y});
					},
					.pointerUp = [weakDocument](double x, double y) -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						document->pointerUp({.x = x, .y = y});
					},
					.scroll = [weakDocument](double x, double y, double delta) -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						document->scroll({.x = x, .y = y}, delta);
					},
					.key = [weakDocument](native::KeyAction action) -> void {
						const auto document = weakDocument.lock();
						if (!document) {
							return;
						}
						switch (action) {
						case native::KeyAction::tabForward:
							document->tab();
							break;
						case native::KeyAction::tabBackward:
							document->tab(true);
							break;
						case native::KeyAction::activate:
							document->keyActivate();
							break;
						}
					},
					.beginPresentation = [weakDocument](const native::PresentationRequest &request)
						-> std::expected<native::FrameIdentity, std::string> {
						const auto owner = weakDocument.lock();
						if (!owner) {
							return std::unexpected("Window document closed");
						}
						std::vector<TileId> tiles;
						tiles.reserve(request.expectedTiles.size());
						for (const auto tile : request.expectedTiles) {
							tiles.push_back(TileId{tile.value});
						}
						const auto attempt =
							owner->beginPresentation(request.epoch, request.sceneRevision, tiles);
						if (!attempt) {
							return std::unexpected("Presentation admission failed");
						}
						return native::FrameIdentity{
							.epoch = attempt->epoch,
							.sequence = attempt->sequence,
							.sceneRevision = attempt->sceneRevision
						};
					},
					.gpuCompleted =
						[weakDocument](native::FrameIdentity frame, bool success) -> bool {
						const auto owner = weakDocument.lock();
						return owner && owner->reportGPUCompletion(portableAttempt(frame), success);
					},
					.drawablePresented = [weakDocument](
											 native::FrameIdentity frame,
											 native::TilePresentation observation
										 ) -> bool {
						const auto owner = weakDocument.lock();
						return owner &&
							owner->reportPresentation(
								portableAttempt(frame),
								TilePresentation{
									.tile = TileId{observation.tile.value},
									.timestamp = observation.timestamp
								}
							);
					},
					.abandonPresentation = [weakDocument](native::FrameIdentity frame) -> void {
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
					.gatePendingPresentation = [weakDocument](native::FrameIdentity frame) -> void {
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
					}}
			);
		}
		const auto result = native::run(std::move(windows));
		TaskScheduler::current().dispatcher = {};
		for (const auto &session : sessions) {
			session.document->close();
		}
		if (!result) {
			std::cerr << "Stana: " << result.error() << '\n';
			return 1;
		}
		return *result;
	} catch (const std::exception &error) {
		TaskScheduler::current().dispatcher = {};
		std::cerr << "Stana application failed: " << error.what() << '\n';
		return 1;
	}
}
} // namespace detail

auto Application::renderingDiagnostics() -> std::vector<WindowDiagnostics> {
	std::vector<WindowDiagnostics> result;
	for (const auto &window : native::diagnostics()) {
		result.push_back(
			{.surfaceEpoch = window.surfaceEpoch,
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
	native::stop();
}

void Application::postAfter(std::chrono::milliseconds delay, std::function<void()> callback) {
	native::postAfter(delay, std::move(callback));
}
} // namespace stana
