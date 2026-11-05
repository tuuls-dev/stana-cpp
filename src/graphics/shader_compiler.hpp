#ifndef STANA_SHADER_COMPILER_HPP
#define STANA_SHADER_COMPILER_HPP
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace stana::shader_detail {
enum class Stage : std::uint8_t { vertex, fragment, compute };
enum class Kind : std::uint8_t { buffer, storage, texture, target, state, sampler };
enum class Format : std::uint8_t { bgra8, rgba16, r8 };

struct Binding {
	std::uint32_t group = 0;
	std::uint32_t binding = 0;
	Kind kind = Kind::buffer;
	Format format = Format::rgba16;
	std::uint64_t byteLength = 0;
	bool writable = false;
};

using Constant = std::variant<bool, std::int32_t, float>;

struct Request {
	std::string source{};
	std::string entry;
	Stage stage = Stage::fragment;
	std::vector<Binding> bindings{};
	std::map<std::string, Constant, std::less<>> constants{};
};

struct Error {
	enum class Category : std::uint8_t { compiler, binding, capability };
	Category category = Category::compiler;
	std::string message;
};
enum class SlotKind : std::uint8_t { buffer, texture, sampler };

struct ReflectedBinding {
	Binding logical;
	SlotKind kind = SlotKind::buffer;
	std::uint32_t slot = 0;
	std::uint64_t minimumBytes = 0;
	std::optional<std::uint32_t> sizeWord{};
};

struct Entry {
	std::string source{};
	std::string function{};
	Stage stage = Stage::fragment;
	std::vector<ReflectedBinding> bindings{};
	std::array<std::uint32_t, 3> workgroupSize{1, 1, 1};
	std::uint64_t workgroupBytes = 0;
	std::vector<std::uint32_t> dynamicWorkgroupBytes{};
	std::vector<std::uint32_t> immediateWords{};
};

inline constexpr std::uint32_t immediateBufferSlot = 30;
/// @brief Production portable WGSL validator/inspector/lowerer. Creates no native device.
[[nodiscard]] auto compile(const Request &) -> std::expected<Entry, Error>;
/// @brief Always recheck replacements, including pipeline-cache hits.
[[nodiscard]] auto validateBindings(const Entry &, const std::vector<Binding> &)
	-> std::expected<void, Error>;
} // namespace stana::shader_detail
#endif
