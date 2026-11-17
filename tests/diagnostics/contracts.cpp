#include "../../src/diagnostics/log.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

import stana;

namespace {
using stana::diagnostics::LogLevel;
using stana::diagnostics::LogRecord;
using stana::diagnostics::LogSink;
using stana::diagnostics::setLogSink;
using stana::diagnostics::detail::Field;
using stana::diagnostics::detail::Level;
using stana::diagnostics::detail::log;

constexpr std::string_view category = "diagnostics.test";

void require(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

class ApplicationSink final : public LogSink {
  public:
	explicit ApplicationSink(std::uint64_t windowId) : windowId_(windowId) {}

	[[nodiscard]] auto enabled(LogLevel level, std::string_view subsystem) const noexcept
		-> bool override {
		return level == LogLevel::error && subsystem == "application";
	}

	void write(const LogRecord &record) noexcept override {
		++writes_;
		if (record.level != LogLevel::error || record.category != "application" ||
			record.message != "Window document missing after preparation" ||
			!std::string_view{record.location.file_name()}.ends_with("application.cpp") ||
			!std::string_view{record.location.function_name()}.contains("runWindows") ||
			record.location.line() == 0 || record.fields.size() != 1) {
			return;
		}
		const auto &field = record.fields.front();
		const auto *id = std::get_if<std::uint64_t>(&field.value);
		matched_ = field.name == "windowId" && id != nullptr && *id == windowId_;
	}

	[[nodiscard]] auto verified() const noexcept -> bool { return writes_ == 1 && matched_; }

  private:
	std::uint64_t windowId_;
	std::atomic<unsigned> writes_{0};
	std::atomic<bool> matched_{false};
};

void applicationDelivery() {
	std::vector<stana::application::detail::WindowSession> sessions(1);
	sessions.front().id = stana::application::WindowId::create();
	require(stana::application::detail::runWindows(sessions) == 1, "default failure outcome");
	const auto sink = std::make_shared<ApplicationSink>(sessions.front().id.value);
	setLogSink(sink);
	const auto result = stana::application::detail::runWindows(sessions);
	setLogSink({});
	require(result == 1, "failure outcome with a sink");
	require(sink->verified(), "application diagnostic record");
}

class CountingSink final : public LogSink {
  public:
	explicit CountingSink(bool accepts) : accepts_(accepts) {}

	[[nodiscard]] auto enabled(LogLevel level, std::string_view subsystem) const noexcept
		-> bool override {
		++checks_;
		return accepts_ && level == LogLevel::trace && subsystem == category;
	}

	void write(const LogRecord &) noexcept override { ++writes_; }

	[[nodiscard]] auto counted(unsigned checks, unsigned writes) const noexcept -> bool {
		return checks_ == checks && writes_ == writes;
	}

  private:
	bool accepts_;
	mutable std::atomic<unsigned> checks_{0};
	std::atomic<unsigned> writes_{0};
};

void lazyPreparation() {
	unsigned attempts = 0;
	const auto prepare = [&] -> auto {
		++attempts;
		return std::array{Field{"attempts", static_cast<std::uint64_t>(attempts)}};
	};
	log(Level::trace, category, "no sink", prepare);
	require(attempts == 0, "preparation skipped without a sink");

	const auto disabled = std::make_shared<CountingSink>(false);
	setLogSink(disabled);
	log(Level::trace, category, "filtered", prepare);
	require(disabled->counted(1, 0) && attempts == 0, "lazy filtering");

	const auto enabled = std::make_shared<CountingSink>(true);
	setLogSink(enabled);
	log(Level::trace, category, "failed preparation", [&] -> std::array<Field, 1> {
		++attempts;
		throw std::runtime_error("diagnostic preparation failed");
	});
	require(attempts == 1 && enabled->counted(1, 0), "preparation exceptions are contained");
	log(Level::trace, category, "successful preparation", prepare);
	setLogSink({});
	constexpr unsigned expectedAttempts = 2;
	require(
		attempts == expectedAttempts && enabled->counted(expectedAttempts, 1),
		"delivery after failed preparation"
	);
}

struct InFlight {
	std::promise<void> filtering;
	std::future<void> resume;
	std::atomic<unsigned> writes{0};
};

class BlockingSink final : public LogSink {
  public:
	explicit BlockingSink(std::shared_ptr<InFlight> state) : state_(std::move(state)) {}

	[[nodiscard]] auto enabled(LogLevel, std::string_view) const noexcept -> bool override {
		state_->filtering.set_value();
		state_->resume.wait();
		return true;
	}

	void write(const LogRecord &) noexcept override { ++state_->writes; }

  private:
	std::shared_ptr<InFlight> state_;
};

void replaceInFlight() {
	const auto state = std::make_shared<InFlight>();
	std::promise<void> resume;
	state->resume = resume.get_future();
	const auto filtering = state->filtering.get_future();
	auto first = std::make_shared<BlockingSink>(state);
	const std::weak_ptr lifetime{first};
	const auto replacement = std::make_shared<CountingSink>(true);
	setLogSink(std::move(first));
	std::jthread emitter{[] -> void {
		log(Level::trace, category, "in-flight event");
	}};
	filtering.wait();
	setLogSink(replacement);
	log(Level::trace, category, "replacement event");
	setLogSink({});
	log(Level::trace, category, "removed sink");
	const bool retained = !lifetime.expired();
	resume.set_value();
	emitter.join();

	require(retained && lifetime.expired(), "old sink lives exactly through in-flight delivery");
	require(state->writes == 1, "in-flight record stays with the sink that filtered it");
	require(replacement->counted(1, 1), "replacement and removal");
}

class ConcurrentSink final : public LogSink {
  public:
	[[nodiscard]] auto enabled(LogLevel, std::string_view) const noexcept -> bool override {
		return true;
	}

	void write(const LogRecord &) noexcept override {
		std::unique_lock lock{mutex_};
		++writes_;
		arrivals_.notify_all();
		arrivals_.wait(lock, [this] -> bool {
			return writes_ == expectedWrites;
		});
	}

	[[nodiscard]] auto completed() const noexcept -> bool {
		const std::scoped_lock lock{mutex_};
		return writes_ == expectedWrites;
	}

  private:
	static constexpr unsigned expectedWrites = 2;
	mutable std::mutex mutex_;
	std::condition_variable arrivals_;
	unsigned writes_ = 0;
};

void concurrentDelivery() {
	const auto sink = std::make_shared<ConcurrentSink>();
	setLogSink(sink);
	{
		const auto emit = [] -> void {
			log(Level::trace, category, "concurrent event");
		};
		const std::jthread first{emit};
		const std::jthread second{emit};
	}
	setLogSink({});
	require(sink->completed(), "both threads delivered to the same sink");
}
} // namespace

auto main() -> int {
	try {
		applicationDelivery();
		lazyPreparation();
		replaceInFlight();
		concurrentDelivery();
		return 0;
	} catch (const std::exception &error) {
		setLogSink({});
		std::cerr << error.what() << '\n';
		return 1;
	}
}
