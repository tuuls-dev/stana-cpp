#include "window_fixture.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <future>
#include <iostream>
#include <latch>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

import stana;

namespace {
using namespace stana;
using namespace std::chrono_literals;

constexpr double width = 480;
constexpr double height = 320;
constexpr auto pollDelay = 50ms;
constexpr auto scenarioDeadline = 8s;

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

struct LivePayload {
	std::shared_ptr<std::atomic<int>> live;
	int identity = 0;

	LivePayload(std::shared_ptr<std::atomic<int>> counter, int value)
		: live(std::move(counter)),
		  identity(value) {
		if (live) {
			++*live;
		}
	}

	LivePayload(const LivePayload &other) : live(other.live), identity(other.identity) {
		if (live) {
			++*live;
		}
	}

	LivePayload(LivePayload &&other) noexcept
		: live(std::move(other.live)),
		  identity(other.identity) {}

	auto operator=(const LivePayload &other) -> LivePayload & {
		if (this != &other) {
			if (live) {
				--*live;
			}
			live = other.live;
			identity = other.identity;
			if (live) {
				++*live;
			}
		}
		return *this;
	}

	auto operator=(LivePayload &&other) noexcept -> LivePayload & {
		if (this != &other) {
			if (live) {
				--*live;
			}
			live = std::move(other.live);
			identity = other.identity;
		}
		return *this;
	}

	~LivePayload() {
		if (live) {
			--*live;
		}
	}

	auto operator==(const LivePayload &other) const -> bool { return identity == other.identity; }
};

struct WorkerScenario : std::enable_shared_from_this<WorkerScenario> {
	Signal<std::string> text{std::string{"Ready"}};
	std::latch releaseWorker{1};
	std::atomic<int> ticks{0};
	std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
	bool verified = false;
	std::future<void> worker;

	void queue() {
		const auto owner = shared_from_this();
		Application::postAfter(pollDelay, [owner] -> void {
			owner->tick();
		});
	}

	void tick() {
		try {
			require(
				std::chrono::steady_clock::now() - started < scenarioDeadline,
				"native worker scenario timed out"
			);
			const auto observed = ++ticks;
			constexpr int releaseAfterTicks = 3;
			if (observed == releaseAfterTicks) {
				releaseWorker.count_down();
			}
			if (text.get() == "Done") {
				require(observed >= releaseAfterTicks, "UI stayed live before worker release");
				verified = true;
				Application::stop();
				return;
			}
			queue();
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			Application::stop();
		}
	}
};

[[nodiscard]] auto runWorkerWakeup() -> int {
	const auto scenario = std::make_shared<WorkerScenario>();
	scenario->worker = std::async(std::launch::async, [text = scenario->text, scenario] -> void {
		scenario->releaseWorker.wait();
		text.setThreadSafe(std::string{"Done"});
	});
	const auto code = Application::run(
		Window{
			.title = "Stana thread-safe writes",
			.size = {.width = width, .height = height},
			.content = Text{
				.text = scenario->text,
				.lifecycle = {.onMount = [scenario](const NodeHandle &) -> void {
					const auto path = fixtures::resultPath();
					if (!path.empty()) {
						std::ofstream ready{path + ".ready"};
						ready << "ready";
					}
					scenario->queue();
				}}
			}
		}
	);
	scenario->worker.get();
	require(code == 0 && scenario->verified, "native scenario 1 worker result");
	return 0;
}

[[nodiscard]] auto runShutdownWithPendingWork() -> int {
	const auto live = std::make_shared<std::atomic<int>>(0);
	const Signal value{LivePayload{live, 0}};
	const auto code = Application::run(
		Window{
			.title = "Stana queued shutdown",
			.size = {.width = width, .height = height},
			.content = Text{
				.text = "shutdown",
				.lifecycle = {.onMount = [value, live](const NodeHandle &) -> void {
					constexpr int queued = 8;
					for (int index = 0; index < queued; ++index) {
						value.setThreadSafe(LivePayload{live, index + 1});
					}
					Application::stop();
				}}
			}
		}
	);
	require(code == 0, "native scenario 2 shutdown");
	require(*live == 1, "native scenario 2 unapplied payloads released");
	bool rejected = false;
	try {
		value.setThreadSafe(LivePayload{live, 0});
	} catch (const std::logic_error &) {
		rejected = true;
	}
	require(rejected, "native scenario 2 late admission rejects");
	return 0;
}

[[nodiscard]] auto runApplyException() -> int {
	const Signal value{0};
	const auto subscription = observe(value, [](int next) -> void {
		if (next == 1) {
			throw std::runtime_error("native apply failed");
		}
	});
	const auto code = Application::run(
		Window{
			.title = "Stana queued failure",
			.size = {.width = width, .height = height},
			.content = Text{
				.text = "failure",
				.lifecycle = {.onMount = [value](const NodeHandle &) -> void {
					value.setThreadSafe(1);
				}}
			}
		}
	);
	require(code != 0, "native scenario 3 contained apply exception");
	static_cast<void>(subscription);
	return 0;
}
} // namespace

auto main() -> int {
	try {
		const auto output = fixtures::resultPath();
		if (!output.empty()) {
			std::ofstream pid{output + ".pid"};
			pid << fixtures::processIdentity();
		}
		int result = 1;
		try {
			result = runWorkerWakeup();
			if (result == 0) {
				result = runShutdownWithPendingWork();
			}
			if (result == 0) {
				result = runApplyException();
			}
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			result = 1;
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
