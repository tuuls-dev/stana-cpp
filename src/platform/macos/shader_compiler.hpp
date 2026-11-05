#ifndef STANA_MACOS_SHADER_COMPILER_HPP
#define STANA_MACOS_SHADER_COMPILER_HPP
#include "../../gpu/shader/compiler.hpp"
#include <array>
#include <optional>

namespace stana::platform::detail::gpu::shader {
using stana::gpu::detail::shader::Binding;
using stana::gpu::detail::shader::Error;
using stana::gpu::detail::shader::Request;
using stana::gpu::detail::shader::Stage;
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
/// @brief Validates portable WGSL, then lowers the owning parsed program to Metal source.
[[nodiscard]] auto compile(const Request &) -> std::expected<Entry, Error>;
/// @brief Lowers without reparsing; native layouts never enter the portable shader contract.
[[nodiscard]] auto lower(const stana::gpu::detail::shader::ValidatedEntry &)
	-> std::expected<Entry, Error>;
/// @brief Always recheck replacements, including pipeline-cache hits.
[[nodiscard]] auto validateBindings(const Entry &, const std::vector<Binding> &)
	-> std::expected<void, Error>;
} // namespace stana::platform::detail::gpu::shader
#endif
