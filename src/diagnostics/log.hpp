#ifndef STANA_DIAGNOSTICS_LOG_HPP
#define STANA_DIAGNOSTICS_LOG_HPP

#include <array>
#include <cstdint>
#include <memory>
#include <source_location>
#include <span>
#include <string_view>
#include <utility>
#include <variant>

// Private, module-free transport for the Objective-C++ native adapter.
namespace stana::diagnostics::detail {

enum class Level : std::uint8_t { trace, error };

using Field = std::pair<
	std::string_view,
	std::variant<bool, std::int64_t, std::uint64_t, double, std::string_view>>;

[[nodiscard]] auto enabledSink(Level level, std::string_view category) noexcept
	-> std::shared_ptr<void>;

void writeLog(
	const std::shared_ptr<void> &sink,
	Level level,
	std::string_view category,
	std::string_view message,
	std::span<const Field> fields,
	std::source_location location
) noexcept;

// Prepare returns an array of fields. Views must remain valid through synchronous delivery.
template <typename Prepare>
void log(
	Level level,
	std::string_view category,
	std::string_view message,
	const Prepare &prepare,
	std::source_location location = std::source_location::current()
) noexcept {
	if (const auto sink = enabledSink(level, category)) {
		try {
			const auto fields = prepare();
			writeLog(sink, level, category, message, fields, location);
		} catch (...) {
			// Drop diagnostics whose preparation failed.
			return;
		}
	}
}

inline void
log(Level level,
	std::string_view category,
	std::string_view message,
	std::source_location location = std::source_location::current()) noexcept {
	log(
		level,
		category,
		message,
		[] -> std::array<Field, 0> {
			return std::array<Field, 0>{};
		},
		location
	);
}

} // namespace stana::diagnostics::detail

#endif
