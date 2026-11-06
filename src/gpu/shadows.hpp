#ifndef STANA_GPU_SHADOWS_HPP
#define STANA_GPU_SHADOWS_HPP
#include <array>
#include <compare>

namespace stana::gpu::detail {
/// @brief Module-free physical contour shared by CPU paint, GPU clipping, and shadow kernels.
struct BoxContour {
	std::array<double, 4> bounds{};
	std::array<std::array<double, 2>, 4> radii{};
	auto operator<=>(const BoxContour &) const = default;
};

struct BoxGeometry {
	BoxContour outer{};
	BoxContour padding{};
	auto operator<=>(const BoxGeometry &) const = default;
};

struct Shadow {
	BoxContour clip{};
	BoxContour caster{};
	std::array<double, 2> offset{};
	std::array<double, 4> color{};
	double blurRadius = 0;
	bool isInset = false;
	std::array<double, 4> ink{};
	auto operator<=>(const Shadow &) const = default;
};
} // namespace stana::gpu::detail
#endif
