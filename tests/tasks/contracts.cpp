#include <chrono>
#include <coroutine>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

import stana;
import stana.testing;

namespace {
constexpr auto delay = std::chrono::milliseconds{10};

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

/// @brief Keeps signal handles in the coroutine frame rather than a temporary closure.
[[nodiscard]] auto delayedWrite(stana::Signal<int> value, std::shared_ptr<int> lifetime)
	-> stana::Task {
	auto ownedValue = std::move(value);
	auto ownedLifetime = std::move(lifetime);
	ownedValue.set(1);
	co_await stana::sleepFor(delay);
	ownedValue.set(2);
	static_cast<void>(ownedLifetime);
}

[[nodiscard]] auto failLater() -> stana::Task {
	co_await stana::sleepFor(delay);
	throw std::runtime_error("Expected task failure");
}

struct AsyncComponent {
	stana::Signal<int> value;
	std::shared_ptr<int> lifetime;

	[[nodiscard]] auto render(stana::ComponentContext &context) const {
		context.task(delayedWrite(value, lifetime), [](const std::exception_ptr &error) -> void {
			std::rethrow_exception(error);
		});
		return stana::Text{.text = value.map([](int current) -> std::string {
			return std::to_string(current);
		})};
	}
};

void lifetimeAndCancellation() {
	using namespace stana;
	TestHost host;
	const Signal value{0};
	const Signal visible{true};
	auto lifetime = std::make_shared<int>(0);
	const auto weak = std::weak_ptr{lifetime};
	host.mount(
		If{.condition = visible,
			.thenContent = AsyncComponent{.value = value, .lifetime = lifetime}}
	);
	lifetime.reset();
	require(value.get() == 1 && !weak.expired(), "task starts after mount");
	visible.set(false);
	// If retains branch descriptions, so the fixture's asset remains retained by
	// the branch factory. Cancellation is proven by the absence of the late write.
	host.advanceTime(delay);
	require(value.get() == 1, "disposed task cannot resume");
	visible.set(true);
	host.advanceTime(delay);
	require(value.get() == 2, "remounted task reaches its continuation");
}

void errorAndFrameRelease() {
	using namespace stana;
	TestHost clock;
	const Scope scope;
	bool observedError = false;
	auto operation = failLater();
	std::move(operation).start(scope, [&](const std::exception_ptr &error) -> void {
		observedError = static_cast<bool>(error);
	});
	clock.advanceTime(delay);
	require(observedError, "live task failures reach explicit handler");
	const Signal value{0};
	auto resource = std::make_shared<int>(0);
	const auto weak = std::weak_ptr{resource};
	auto task = delayedWrite(value, resource);
	resource.reset();
	std::move(task).start(scope, [](const std::exception_ptr &error) -> void {
		std::rethrow_exception(error);
	});
	clock.advanceTime(delay);
	require(weak.expired(), "completed task unregisters and releases its frame");
}
} // namespace

auto main() -> int {
	try {
		lifetimeAndCancellation();
		errorAndFrameRelease();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
