#ifndef STANA_GPU_LIMITS_HPP
#define STANA_GPU_LIMITS_HPP
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

namespace stana::gpu::detail {
inline constexpr std::size_t payloadLimit = 256ULL * 1024 * 1024;
inline constexpr std::uint32_t axisLimit = 16384;
inline constexpr std::size_t sourceLimit = 16ULL * 1024 * 1024;
inline constexpr std::size_t commandLimit = 65536;
inline constexpr std::size_t pointLimit = 1048576;
inline constexpr std::size_t indexLimit = 3145728;
inline constexpr std::size_t descriptorLimit = 128;
inline constexpr std::size_t nameLimit = 1024;
inline constexpr std::uint64_t invocationLimit = 268435456;
inline constexpr double halfMaximum = 65504;
inline constexpr double defaultSourcePeak = 16;

[[nodiscard]] inline auto textureBytes(
	std::uint32_t width,
	std::uint32_t height,
	std::size_t bytesPerPixel,
	bool automatic = false
) -> std::expected<std::size_t, std::string> {
	if (width > axisLimit || height > axisLimit || (!automatic && (width == 0 || height == 0))) {
		return std::unexpected("Texture extent exceeds the portable profile");
	}
	if (width == 0 || height == 0) {
		return 0;
	}
	if (width > payloadLimit / bytesPerPixel / height) {
		return std::unexpected("Texture payload exceeds budget");
	}
	return static_cast<std::size_t>(width) * height * bytesPerPixel;
}
} // namespace stana::gpu::detail
#endif
