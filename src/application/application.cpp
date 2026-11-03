module;

#include "../platform/native_contracts.hpp"

#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module stana.application;
import stana.core;
import stana.graphics;

namespace stana {
namespace {
/// @brief Converts the private transport identity without losing retry/surface qualification.
[[nodiscard]] auto portableAttempt(native::FrameIdentity frame) -> PresentationAttempt {
	return {.epoch = frame.epoch, .sequence = frame.sequence, .sceneRevision = frame.sceneRevision};
}

/// @brief Copies a portable scene into an owning module-free native transport value.
/// No native callback borrows the document or its mutable retained records.
[[nodiscard]] auto nativeScene(const SceneSnapshot &snapshot)
	-> std::shared_ptr<const native::Scene> {
	auto result = std::make_shared<native::Scene>();
	result->revision = snapshot.revision;
	result->red = snapshot.background.red;
	result->green = snapshot.background.green;
	result->blue = snapshot.background.blue;
	result->commands.reserve(snapshot.commands.size());
	for (const auto &command : snapshot.commands) {
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
		result->commands.push_back(
			{.operation = operation,
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
				.fontWeight = command.font.weight}
		);
	}
	return result;
}
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
			const auto document = session.document;
			document->enablePresentationTracking();
			document->setTextMeasure([](const std::string &text, Font font, double width) -> Size {
				const auto [measuredWidth, measuredHeight] =
					native::measureText(text, {.size = font.size, .weight = font.weight}, width);
				return {.width = measuredWidth, .height = measuredHeight};
			});
			document->commit();
			windows.push_back(
				{.title = session.title,
					.width = session.size.width,
					.height = session.size.height,
					.scene = [document] -> std::shared_ptr<const native::Scene> {
						return nativeScene(*document->scene());
					},
					.resize = [document](double width, double height) -> void {
						document->resize({.width = width, .height = height});
					},
					.pointerDown = [document](double x, double y) -> void {
						document->pointerDown({.x = x, .y = y});
					},
					.pointerUp = [document](double x, double y) -> void {
						document->pointerUp({.x = x, .y = y});
					},
					.scroll = [document](double x, double y, double delta) -> void {
						document->scroll({.x = x, .y = y}, delta);
					},
					.key = [document](native::KeyAction action) -> void {
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
					.beginPresentation = [document](std::uint64_t epoch, std::uint64_t revision)
						-> std::expected<native::FrameIdentity, std::string> {
						const auto attempt = document->beginPresentation(epoch, revision);
						if (!attempt) {
							return std::unexpected("Presentation admission failed");
						}
						return native::FrameIdentity{
							.epoch = attempt->epoch,
							.sequence = attempt->sequence,
							.sceneRevision = attempt->sceneRevision
						};
					},
					.gpuCompleted = [document](native::FrameIdentity frame, bool success) -> bool {
						return document->reportGPUCompletion(portableAttempt(frame), success);
					},
					.drawablePresented =
						[document](native::FrameIdentity frame, double timestamp) -> bool {
						return document->reportPresentation(portableAttempt(frame), timestamp);
					},
					.abandonPresentation = [document](native::FrameIdentity frame) -> void {
						document->abandonPresentation(portableAttempt(frame));
					},
					.resetPresentation = [document](std::uint64_t epoch) -> void {
						document->resetPresentation(epoch);
					},
					.close = [document] -> void {
						document->close();
					},
					.ready = [document](std::function<void()> invalidate) -> void {
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
					window.failure.empty() ? std::nullopt : std::optional{window.failure}}
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
