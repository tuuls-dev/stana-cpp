#include "window_fixture.hpp"
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
import stana;

namespace {

namespace fixture = stana::testing::fixtures::platform::macos;

constexpr auto pollDelay = std::chrono::milliseconds{40};
constexpr auto scenarioDeadline = std::chrono::seconds{12};
constexpr auto windowTitle = "Stana pointer activation";
constexpr double clickX = 40;
constexpr double clickY = 40;

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] auto ready(const stana::platform::detail::WindowStatistics &statistics) -> bool {
	return statistics.confirmedRevision != 0 &&
		statistics.confirmedRevision == statistics.submittedRevision &&
		!statistics.renderingWork.hasActiveBatch && !statistics.renderingWork.hasPendingScene;
}

/// @brief A settled window must accept a click. A gated surface still shows the button.
struct Scenario : std::enable_shared_from_this<Scenario> {
	std::shared_ptr<stana::document::detail::Document> document;
	stana::reactive::Signal<int> count{0};
	bool verified = false;
	std::string failure;
	std::chrono::steady_clock::time_point stageStarted = std::chrono::steady_clock::now();

	void queue() {
		const auto owner = shared_from_this();
		stana::application::Application::postAfter(pollDelay, [owner] -> void {
			owner->tick();
		});
	}

	void tick() {
		try {
			require(
				std::chrono::steady_clock::now() - stageStarted < scenarioDeadline,
				"pointer activation timed out"
			);
			const auto windows = stana::platform::detail::diagnostics();
			require(windows.size() == 1, "pointer window detached");
			require(windows.front().failure.empty(), windows.front().failure);
			if (!ready(windows.front())) {
				queue();
				return;
			}
			const stana::Point point{.x = stana::Points{clickX}, .y = stana::Points{clickY}};
			require(
				document->hitTest(point).has_value(),
				"confirmed surface does not hit the button"
			);
			fixture::clickContent(windowTitle, clickX, clickY);
			require(count.get() == 1, "click did not activate the button");
			verified = true;
			stana::application::Application::stop();
		} catch (const std::exception &error) {
			failure = error.what();
			std::cerr << failure << '\n';
			stana::application::Application::stop();
		}
	}
};

[[nodiscard]] auto runScenario() -> int {
	const auto scenario = std::make_shared<Scenario>();
	scenario->document = stana::document::detail::Document::create(
		{.width = stana::Points{240}, .height = stana::Points{160}}
	);
	stana::application::detail::installNativeTaskDispatcher();
	const auto count = scenario->count;
	stana::reactive::Transaction::perform([&] -> void {
		stana::ui::MountContext context{
			.document = scenario->document,
			.parent = scenario->document->root()
		};
		const stana::Frame fill{
			.width = stana::Length::fill(),
			.height = stana::Length::fill()
		};
		static_cast<void>(stana::ui::mount(
			stana::ui::VStack{
				.layout = {.frame = fill},
				.children = stana::ui::children(stana::ui::Button{
					.text = "Increment",
					.layout = {.frame = fill},
					.events = {stana::ui::on(
						stana::ui::Event::click,
						[count] -> void {
							++count;
						}
					)}
				})
			},
			context
		));
	});
	scenario->queue();
	std::vector<stana::application::detail::WindowSession> sessions(1);
	sessions.front() = {
		.id = stana::application::WindowId::create(),
		.options =
			{.contentSize =
					stana::Size{.width = stana::Points{240}, .height = stana::Points{160}},
				.title = windowTitle},
		.document = scenario->document
	};
	const int status = stana::application::detail::runWindows(sessions);
	return status == 0 && scenario->verified && scenario->failure.empty() ? 0 : 1;
}
} // namespace

auto main() -> int {
	try {
		const auto output = fixture::resultPath();
		if (!output.empty()) {
			std::ofstream pid{output + ".pid"};
			pid << fixture::processIdentity();
		}
		int result = 1;
		try {
			result = runScenario();
		} catch (const std::exception &error) {
			result = 1;
			std::cerr << error.what() << '\n';
		}
		if (!output.empty()) {
			std::ofstream status{output};
			status << result;
		}
		return result;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
