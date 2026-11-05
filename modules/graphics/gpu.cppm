module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>
export module stana.graphics:gpu;
export import :program;
import stana.primitives;

export namespace stana {
/// @brief Straight scene-linear extended-sRGB, bounded to finite binary16 color range.
class ExtendedColor {
  public:
	ExtendedColor(double red, double green, double blue, double alpha = 1);

	[[nodiscard]] auto red() const noexcept -> double { return red_; }

	[[nodiscard]] auto green() const noexcept -> double { return green_; }

	[[nodiscard]] auto blue() const noexcept -> double { return blue_; }

	[[nodiscard]] auto alpha() const noexcept -> double { return alpha_; }

	auto operator<=>(const ExtendedColor &) const = default;

  private:
	double red_;
	double green_;
	double blue_;
	double alpha_;
};

struct GPUVertex {
	Point position{};
	ExtendedColor color{0, 0, 0};
};

class GPUMesh {
  public:
	[[nodiscard]] static auto create(std::span<const GPUVertex>, std::span<const std::uint32_t>)
		-> std::expected<GPUMesh, GPUError>;
	[[nodiscard]] auto vertices() const noexcept -> std::span<const GPUVertex>;
	[[nodiscard]] auto indices() const noexcept -> std::span<const std::uint32_t>;
	[[nodiscard]] auto version() const noexcept -> std::uint64_t;
	[[nodiscard]] auto operator==(const GPUMesh &) const noexcept -> bool;

  private:
	friend struct detail::GPUAccess;
	struct Data;
	std::shared_ptr<const Data> data_;
	explicit GPUMesh(std::shared_ptr<const Data>);
};

namespace detail {
using GPUColor = std::variant<Color, ExtendedColor>;

struct GPURect {
	Rect bounds;
	double radius;
	GPUColor color;
};

struct GPUEllipse {
	Rect bounds;
	GPUColor color;
};

struct GPUPolyline {
	std::vector<Point> points;
	double width;
	GPUColor color;
	bool closed;
};

using GPUCommand = std::variant<GPURect, GPUEllipse, GPUMesh, GPUPolyline, GPUProgram>;
struct GPUAccess;
} // namespace detail

/// @brief Copy-on-write ordered local-coordinate GPU drawing; copies share immutable versions.
class GPUDrawing {
  public:
	GPUDrawing();
	explicit GPUDrawing(GPUProgram);
	GPUDrawing(const GPUDrawing &) = default;
	auto operator=(const GPUDrawing &) -> GPUDrawing & = default;
	GPUDrawing(GPUDrawing &&) noexcept;
	auto operator=(GPUDrawing &&) noexcept -> GPUDrawing &;
	~GPUDrawing();
	void fillRect(Rect, Color, double cornerRadius = 0);
	void fillRect(Rect, ExtendedColor, double cornerRadius = 0);
	void fillEllipse(Rect, Color);
	void fillEllipse(Rect, ExtendedColor);
	void drawMesh(GPUMesh);
	void strokePolyline(std::span<const Point>, Color, double width = 1, bool closed = false);
	void
	strokePolyline(std::span<const Point>, ExtendedColor, double width = 1, bool closed = false);
	[[nodiscard]] auto operator==(const GPUDrawing &) const noexcept -> bool;
	[[nodiscard]] auto version() const noexcept -> std::uint64_t;
	[[nodiscard]] auto requiresExtended() const -> bool;

  private:
	friend struct detail::GPUAccess;
	struct Data;
	std::shared_ptr<Data> data_;
	void append(detail::GPUCommand);
	void polyline(std::span<const Point>, detail::GPUColor, double, bool);
};

namespace detail {
/// @brief Internal read-only seam for production projection/transport and deliberate testing.
struct GPUAccess {
	[[nodiscard]] static auto sourceOwner(const GPUShader &) -> std::shared_ptr<const std::string>;
	[[nodiscard]] static auto bytesOwner(const GPUBuffer &)
		-> std::shared_ptr<const std::vector<std::byte>>;
	[[nodiscard]] static auto bytesOwner(const GPUTexture &)
		-> std::shared_ptr<const std::vector<std::byte>>;
	[[nodiscard]] static auto initialOwner(const GPUStateSlot &)
		-> std::shared_ptr<const std::vector<std::byte>>;
	[[nodiscard]] static auto indicesOwner(const GPUMesh &)
		-> std::shared_ptr<const std::vector<std::uint32_t>>;

	[[nodiscard]] static auto commands(const GPUDrawing &) noexcept -> std::span<const GPUCommand>;
};
} // namespace detail
} // namespace stana
