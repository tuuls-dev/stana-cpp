#ifndef STANA_GPU_SHADER_COMPILER_HPP
#define STANA_GPU_SHADER_COMPILER_HPP
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace stana::gpu::detail::shader {
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

struct BindingRequirement {
	Binding logical;
	std::uint64_t minimumBytes = 0;
	bool uniform = false;
};

struct ParsedShader;

/// @brief Owning validated entry; the parsed program and its source outlive native lowering.
/// No backend handles, binding slots or generated shader text enter this transport.
struct ValidatedEntry {
	std::shared_ptr<const ParsedShader> parsed;
	std::string function;
	Stage stage = Stage::fragment;
	std::vector<BindingRequirement> bindings{};
	std::map<std::uint16_t, double> constants{};
};

/// @brief Parses WGSL once and validates the portable profile and logical resource contract.
[[nodiscard]] auto validate(const Request &) -> std::expected<ValidatedEntry, Error>;
/// @brief Rechecks logical replacements, including native-reflected minimum buffer sizes.
[[nodiscard]] auto validateBindings(std::span<const BindingRequirement>, std::span<const Binding>)
	-> std::expected<void, Error>;
} // namespace stana::gpu::detail::shader
#endif
