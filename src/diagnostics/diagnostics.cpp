module;

#include "log.hpp"

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

module stana;
import :diagnostics;

namespace stana::diagnostics {
namespace detail {
namespace {
struct SinkConfiguration {
	std::mutex mutex;
	std::shared_ptr<LogSink> sink;
};

[[nodiscard]] auto sinkConfiguration() noexcept -> SinkConfiguration & {
	static SinkConfiguration configuration;
	return configuration;
}

[[nodiscard]] auto publicLevel(Level level) noexcept -> LogLevel {
	return level == Level::trace ? LogLevel::trace : LogLevel::error;
}
} // namespace

// The native adapter calls these functions from outside the named module.
extern "C++" {
auto enabledSink(Level level, std::string_view category) noexcept -> std::shared_ptr<void> {
	try {
		auto sink = [] -> std::shared_ptr<LogSink> {
			auto &configuration = sinkConfiguration();
			const std::scoped_lock lock{configuration.mutex};
			return configuration.sink;
		}();
		if (sink && sink->enabled(publicLevel(level), category)) {
			return sink;
		}
		return {};
	} catch (...) {
		// A diagnostic must not affect the operation it describes.
		return {};
	}
}

void writeLog(
	const std::shared_ptr<void> &sink,
	Level level,
	std::string_view category,
	std::string_view message,
	std::span<const Field> fields,
	std::source_location location
) noexcept {
	try {
		std::vector<LogField> values;
		values.reserve(fields.size());
		for (const auto &[name, value] : fields) {
			values.push_back({.name = name, .value = value});
		}
		static_cast<LogSink *>(sink.get())
			->write(
				{.level = publicLevel(level),
					.category = category,
					.message = message,
					.fields = values,
					.location = location}
			);
	} catch (...) {
		// Drop diagnostics whose structured fields could not be prepared.
		return;
	}
}
} // extern "C++"
} // namespace detail

void setLogSink(std::shared_ptr<LogSink> sink) noexcept {
	auto &configuration = detail::sinkConfiguration();
	const std::scoped_lock lock{configuration.mutex};
	// The parameter releases the previous sink after the lock is destroyed.
	configuration.sink.swap(sink);
}
} // namespace stana::diagnostics
