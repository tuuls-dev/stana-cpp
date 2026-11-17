module;

#include <cstdint>
#include <memory>
#include <source_location>
#include <span>
#include <string_view>
#include <variant>

export module stana:diagnostics;

export namespace stana::diagnostics {

/// @brief Diagnostic severity, ordered from least to most severe.
enum class LogLevel : std::uint8_t {
	trace, ///< Detailed execution events.
	debug, ///< Information for diagnosing behavior.
	info,  ///< Routine operational information.
	warn,  ///< An unexpected condition that permits continued operation.
	error  ///< A failed operation.
};

/// @brief Structured diagnostic value. String views borrow text for the duration of write().
using LogValue = std::variant<bool, std::int64_t, std::uint64_t, double, std::string_view>;

/// @brief A named structured value borrowed for the duration of write().
struct LogField {
	std::string_view name; ///< Key identifying the value within the event.
	LogValue value;		   ///< Typed value associated with the key.
};

/// @brief A diagnostic record borrowed for the duration of write().
struct LogRecord {
	LogLevel level;					  ///< Event severity.
	std::string_view category;		  ///< Subsystem used for filtering and routing.
	std::string_view message;		  ///< Human-readable description of the event.
	std::span<const LogField> fields; ///< Structured event data, which may be empty.
	std::source_location location;	  ///< Stana source site where the event originated.
};

/// @brief Application-provided destination for synchronous diagnostics.
///
/// Callbacks may run concurrently on any emitting thread. Implementations must be thread-safe
/// and contain backend exceptions before returning. Copy any borrowed data needed after write().
class LogSink {
  public:
	/// @brief Releases the sink when its last shared owner lets it go.
	virtual ~LogSink() = default;

	/// @brief Checks application filtering before the event is prepared.
	/// @param level Severity of the event.
	/// @param category Borrowed subsystem name, valid for this call.
	/// @return Whether the sink is interested in the event.
	[[nodiscard]] virtual auto enabled(LogLevel level, std::string_view category) const noexcept
		-> bool = 0;

	/// @brief Consumes an event synchronously on its emitting thread.
	/// @param record Event and borrowed data valid for this call.
	virtual void write(const LogRecord &record) noexcept = 0;

  protected:
	LogSink() = default;
	LogSink(const LogSink &) = default;
	LogSink(LogSink &&) noexcept = default;
	auto operator=(const LogSink &) -> LogSink & = default;
	auto operator=(LogSink &&) noexcept -> LogSink & = default;
};

/// @brief Replaces the process-wide diagnostic sink. Registration is thread-safe.
///
/// In-flight diagnostics retain their selected sink through filtering and delivery.
/// Stana holds shared ownership and the application manages the backend.
/// @param sink Application-owned destination. A null pointer disables delivery, the default.
void setLogSink(std::shared_ptr<LogSink> sink) noexcept;

} // namespace stana::diagnostics
