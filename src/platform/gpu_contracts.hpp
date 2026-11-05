#ifndef STANA_GPU_CONTRACTS_HPP
#define STANA_GPU_CONTRACTS_HPP
#include "../graphics/shader_compiler.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace stana::native::gpu {
inline constexpr double defaultSourcePeak = 16;
using Bytes = std::vector<std::byte>;
using ByteOwner = std::shared_ptr<const Bytes>;
using Format = shader_detail::Format;
using Stage = shader_detail::Stage;
enum class Encoding : std::uint8_t { encoded, linear };
enum class ColorMode : std::uint8_t { automatic, sdr, extendedLinear };

struct Point {
	double x = 0;
	double y = 0;
};

struct Rect {
	Point origin;
	double width = 0;
	double height = 0;
};

struct Color {
	std::array<double, 4> rgba{0, 0, 0, 1};
	bool linear = false;
};

struct Buffer {
	std::uint64_t version = 0;
	ByteOwner bytes;
};

struct Storage {
	std::size_t length;
};

struct Texture {
	std::uint64_t version = 0;
	std::uint32_t width;
	std::uint32_t height;
	Format format;
	ByteOwner bytes;
	Encoding encoding;
	bool premultiplied;
	double sourcePeak = defaultSourcePeak;
};

struct Target {
	std::uint32_t width;
	std::uint32_t height;
	Format format;
};

struct State {
	std::uint64_t version = 0;
	std::uint32_t width;
	std::uint32_t height;
	Format format;
	ByteOwner initial;
	std::string resetKey;
};

struct Sampler {
	bool linear;
};

using Resource = std::variant<Buffer, Storage, Texture, Target, State, Sampler>;

struct Binding {
	std::string resource;
	std::uint32_t group;
	std::uint32_t binding;
	bool write;
	Stage stage;
	bool previous;
};

struct RenderPass {
	std::string vertex;
	std::string fragment;
	std::string target;
	std::uint32_t vertexCount;
	std::uint32_t instanceCount;
	std::optional<std::string> indexBuffer;
	std::vector<Binding> bindings;
	bool blends;
};

struct ComputePass {
	std::string function;
	std::array<std::uint32_t, 3> workgroupCount;
	std::vector<Binding> bindings;
};

using Pass = std::variant<RenderPass, ComputePass>;

struct Program {
	std::uint64_t version = 0;
	std::uint64_t pipelineVersion = 0;
	std::uint64_t stateVersion = 0;
	std::shared_ptr<const std::string> source;
	std::map<std::string, Resource, std::less<>> resources;
	std::vector<Pass> passes;
	std::vector<std::size_t> order;
	std::string output;
	Encoding outputColor = Encoding::linear;
	double sourcePeak = defaultSourcePeak;
	std::map<std::string, shader_detail::Constant, std::less<>> constants;
};

struct Shape {
	Rect bounds;
	double radius = 0;
	Color color;
	bool ellipse = false;
};

struct Vertex {
	std::array<float, 2> position;
	std::array<float, 4> color;
};

struct Mesh {
	std::uint64_t version = 0;
	std::vector<Vertex> vertices;
	std::shared_ptr<const std::vector<std::uint32_t>> indices;
};

struct Polyline {
	std::vector<Point> points{};
	double width;
	Color color;
	bool closed;
};

using Command =
	std::variant<Shape, std::shared_ptr<const Mesh>, Polyline, std::shared_ptr<const Program>>;

struct Drawing {
	std::uint64_t version = 0;
	bool extended = false;
	std::vector<Command> commands;
};

struct Diagnostic {
	enum class Category : std::uint8_t { compiler, binding, capability, execution };
	Category category;
	std::string message;
	std::uint64_t document;
	std::uint64_t instance;
	std::uint64_t drawingVersion;
	std::uint64_t programVersion;
	std::uint64_t epoch;
};
} // namespace stana::native::gpu
#endif
