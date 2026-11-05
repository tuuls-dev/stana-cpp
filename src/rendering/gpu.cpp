module;
#include "../gpu/color.hpp"
#include "../gpu/limits.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
module stana;
import :rendering;

namespace stana::rendering {
namespace {

struct RenderVersion {
	std::uint64_t value = stana::detail::nextIdentity();
};

[[nodiscard]] auto invalid(std::string message) -> std::unexpected<GPUError> {
	return std::unexpected(
		GPUError{.category = GPUError::Category::invalidDescriptor, .message = std::move(message)}
	);
}

[[nodiscard]] auto pixelBytes(GPUTextureFormat format) -> std::optional<std::size_t> {
	switch (format) {
	case GPUTextureFormat::bgra8Unorm:
		return 4;
	case GPUTextureFormat::rgba16Float: {
		constexpr std::size_t halfPixelBytes = 8;
		return halfPixelBytes;
	}
	case GPUTextureFormat::r8Unorm:
		return 1;
	}
	return {};
}

void point(Point value) {
	for (const double component : {value.x, value.y}) {
		stana::detail::requireFinite(component);
		if (std::abs(component) > static_cast<double>(std::numeric_limits<float>::max())) {
			throw std::invalid_argument("GPU coordinate exceeds float range");
		}
	}
}

void rectangle(Rect rect) {
	stana::detail::requireNonnegative(rect.size.width);
	stana::detail::requireNonnegative(rect.size.height);
	point(rect.origin);
	point({.x = rect.origin.x + rect.size.width, .y = rect.origin.y + rect.size.height});
	point({.x = rect.size.width, .y = rect.size.height});
}

void color(Color value) {
	for (const double component : {value.red, value.green, value.blue, value.alpha}) {
		stana::detail::requireNonnegative(component);
		if (component > 1) {
			throw std::invalid_argument("GPU SDR color exceeds one");
		}
	}
}
} // namespace

struct GPUShader::Data {
	RenderVersion version{};
	std::string source;
};

auto GPUShader::source(std::string source) -> std::expected<GPUShader, GPUError> {
	if (source.empty() || source.size() > stana::gpu::detail::sourceLimit ||
		source.find('\0') != std::string::npos) {
		return invalid("WGSL source is empty, oversized or contains NUL");
	}
	return GPUShader{std::make_shared<const Data>(Data{.source = std::move(source)})};
}

auto GPUShader::source() const noexcept -> const std::string & {
	return data_->source;
}

auto GPUShader::version() const noexcept -> std::uint64_t {
	return data_->version.value;
}

auto GPUShader::operator==(const GPUShader &other) const noexcept -> bool {
	return data_ == other.data_;
}

struct GPUBuffer::Data {
	RenderVersion version{};
	std::vector<std::byte> bytes;
};

auto GPUBuffer::create(std::span<const std::byte> bytes) -> std::expected<GPUBuffer, GPUError> {
	if (bytes.size() > stana::gpu::detail::payloadLimit) {
		return invalid("Buffer payload exceeds budget");
	}
	return GPUBuffer{std::make_shared<const Data>(Data{.bytes = {bytes.begin(), bytes.end()}})};
}

auto GPUBuffer::bytes() const noexcept -> std::span<const std::byte> {
	return data_->bytes;
}

auto GPUBuffer::version() const noexcept -> std::uint64_t {
	return data_->version.value;
}

auto GPUBuffer::operator==(const GPUBuffer &other) const noexcept -> bool {
	return data_ == other.data_;
}

struct GPUTexture::Data {
	RenderVersion version{};
	GPUTextureDescriptor descriptor;
};

auto GPUTexture::create(GPUTextureDescriptor descriptor) -> std::expected<GPUTexture, GPUError> {
	const auto bpp = pixelBytes(descriptor.format);
	if (!bpp ||
		(descriptor.encoding != GPUColorEncoding::encodedSRGB &&
			descriptor.encoding != GPUColorEncoding::extendedLinear)) {
		return invalid("Unknown texture format or color encoding");
	}
	const auto length = stana::gpu::detail::textureBytes(descriptor.width, descriptor.height, *bpp);
	if (!length) {
		return invalid(length.error());
	}
	if (*length != descriptor.bytes.size()) {
		return invalid("Texture byte length must match tightly packed extent");
	}
	if (!std::isfinite(descriptor.sourcePeak) || descriptor.sourcePeak <= 0 ||
		descriptor.sourcePeak > stana::gpu::detail::halfMaximum) {
		return invalid("Invalid texture source peak");
	}
	if (descriptor.format == GPUTextureFormat::r8Unorm &&
		(descriptor.encoding != GPUColorEncoding::encodedSRGB || !descriptor.premultiplied ||
			std::is_neq(descriptor.sourcePeak <=> stana::gpu::detail::defaultSourcePeak))) {
		return invalid("Scalar textures cannot carry color metadata");
	}
	const auto bytes = std::span<const std::byte>{descriptor.bytes};
	if (descriptor.format == GPUTextureFormat::rgba16Float) {
		constexpr std::size_t stride = 8;
		constexpr std::size_t alphaOffset = 6;
		for (std::size_t offset = 0; offset < bytes.size(); offset += stride) {
			const double alpha = stana::gpu::detail::halfAt(bytes, offset + alphaOffset);
			if (!std::isfinite(alpha) || alpha < 0 || alpha > 1) {
				return invalid("Invalid half-float alpha");
			}
			for (std::size_t channel = 0; channel < 3; ++channel) {
				const double component = stana::gpu::detail::halfAt(bytes, offset + 2 * channel);
				if (!std::isfinite(component) ||
					(descriptor.premultiplied && std::is_eq(alpha <=> 0.0) &&
						std::is_neq(component <=> 0.0))) {
					return invalid(
						"Non-finite half color or nonzero premultiplied transparent color"
					);
				}
			}
		}
	} else if (descriptor.format == GPUTextureFormat::bgra8Unorm && descriptor.premultiplied) {
		constexpr std::size_t stride = 4;
		for (std::size_t offset = 0; offset < bytes.size(); offset += stride) {
			if (stana::gpu::detail::byteAt(bytes, offset + 3) == std::byte{0} &&
				(stana::gpu::detail::byteAt(bytes, offset) != std::byte{0} ||
					stana::gpu::detail::byteAt(bytes, offset + 1) != std::byte{0} ||
					stana::gpu::detail::byteAt(bytes, offset + 2) != std::byte{0})) {
				return invalid("Nonzero premultiplied transparent color");
			}
		}
	}
	return GPUTexture{std::make_shared<const Data>(Data{.descriptor = std::move(descriptor)})};
}

auto GPUTexture::descriptor() const noexcept -> const GPUTextureDescriptor & {
	return data_->descriptor;
}

auto GPUTexture::bytes() const noexcept -> std::span<const std::byte> {
	return data_->descriptor.bytes;
}

auto GPUTexture::version() const noexcept -> std::uint64_t {
	return data_->version.value;
}

auto GPUTexture::operator==(const GPUTexture &other) const noexcept -> bool {
	return data_ == other.data_;
}

struct GPUStateSlot::Data {
	RenderVersion version{};
	GPUStateSlotDescriptor descriptor;
};

auto GPUStateSlot::create(GPUStateSlotDescriptor descriptor)
	-> std::expected<GPUStateSlot, GPUError> {
	const auto bpp = pixelBytes(descriptor.format);
	if (!bpp) {
		return invalid("Unknown texture format");
	}
	const auto length = stana::gpu::detail::textureBytes(descriptor.width, descriptor.height, *bpp);
	if (!length) {
		return invalid(length.error());
	}
	if (!descriptor.initialBytes.empty() && descriptor.initialBytes.size() != *length) {
		return invalid("State initializer byte length mismatch");
	}
	if (descriptor.resetKey.size() > stana::gpu::detail::nameLimit ||
		descriptor.resetKey.find('\0') != std::string::npos) {
		return invalid("Invalid state reset key");
	}
	return GPUStateSlot{std::make_shared<const Data>(Data{.descriptor = std::move(descriptor)})};
}

auto GPUStateSlot::descriptor() const noexcept -> const GPUStateSlotDescriptor & {
	return data_->descriptor;
}

auto GPUStateSlot::version() const noexcept -> std::uint64_t {
	return data_->version.value;
}

auto GPUStateSlot::operator==(const GPUStateSlot &other) const noexcept -> bool {
	return data_ == other.data_;
}

ExtendedColor::ExtendedColor(double red, double green, double blue, double alpha)
	: red_(red),
	  green_(green),
	  blue_(blue),
	  alpha_(alpha) {
	const std::array components{red, green, blue, alpha};
	for (const double component : components) {
		if (!std::isfinite(component) || std::abs(component) > stana::gpu::detail::halfMaximum) {
			throw std::invalid_argument("Invalid extended linear color");
		}
	}
	if (components.back() < 0 || components.back() > 1) {
		throw std::invalid_argument("Invalid extended alpha");
	}
}

struct GPUMesh::Data {
	RenderVersion version{};
	std::vector<GPUVertex> vertices;
	std::vector<std::uint32_t> indices;
};

GPUMesh::GPUMesh(std::shared_ptr<const Data> data) : data_(std::move(data)) {
}

auto GPUMesh::create(std::span<const GPUVertex> vertices, std::span<const std::uint32_t> indices)
	-> std::expected<GPUMesh, GPUError> {
	if (vertices.size() > stana::gpu::detail::pointLimit ||
		indices.size() > stana::gpu::detail::indexLimit || indices.size() % 3 != 0) {
		return invalid("Mesh count exceeds limits or contains incomplete triangles");
	}
	for (const auto index : indices) {
		if (index >= vertices.size()) {
			return invalid("Mesh index out of range");
		}
	}
	try {
		for (const auto &vertex : vertices) {
			point(vertex.position);
		}
	} catch (const std::invalid_argument &error) {
		return invalid(error.what());
	}
	return GPUMesh{std::make_shared<const Data>(Data{
		.vertices = {vertices.begin(), vertices.end()},
		.indices = {indices.begin(), indices.end()}
	})};
}

auto GPUMesh::vertices() const noexcept -> std::span<const GPUVertex> {
	return data_->vertices;
}

auto GPUMesh::indices() const noexcept -> std::span<const std::uint32_t> {
	return data_->indices;
}

auto GPUMesh::version() const noexcept -> std::uint64_t {
	return data_->version.value;
}

auto GPUMesh::operator==(const GPUMesh &other) const noexcept -> bool {
	return data_ == other.data_;
}

struct GPUDrawing::Data {
	RenderVersion version{};
	std::vector<stana::rendering::detail::GPUCommand> commands;
	std::map<std::uint64_t, std::size_t> owned;
	std::size_t bytes = 0;
};

GPUDrawing::GPUDrawing() : data_(std::make_shared<Data>()) {
}

GPUDrawing::GPUDrawing(GPUProgram program) : GPUDrawing() {
	append(std::move(program));
}

GPUDrawing::GPUDrawing(GPUDrawing &&) noexcept = default;
auto GPUDrawing::operator=(GPUDrawing &&) noexcept -> GPUDrawing & = default;
GPUDrawing::~GPUDrawing() = default;

auto GPUDrawing::operator==(const GPUDrawing &other) const noexcept -> bool {
	return data_ == other.data_;
}

auto GPUDrawing::version() const noexcept -> std::uint64_t {
	return data_ ? data_->version.value : 0;
}

auto stana::rendering::detail::GPUAccess::commands(const GPUDrawing &drawing) noexcept
	-> std::span<const GPUCommand> {
	return drawing.data_ ? std::span<const GPUCommand>{drawing.data_->commands}
						 : std::span<const GPUCommand>{};
}

void GPUDrawing::append(stana::rendering::detail::GPUCommand command) {
	if (data_ && data_->commands.size() >= stana::gpu::detail::commandLimit) {
		throw std::invalid_argument("GPU drawing command limit");
	}
	std::map<std::uint64_t, std::size_t> additions;
	std::size_t direct = sizeof(stana::rendering::detail::GPUCommand);
	std::visit(
		[&](const auto &operation) -> void {
			using T = std::decay_t<decltype(operation)>;
			if constexpr (std::is_same_v<T, GPUMesh>) {
				additions.emplace(
					operation.version(),
					operation.vertices().size() * sizeof(GPUVertex) +
						operation.indices().size() * sizeof(std::uint32_t)
				);
			} else if constexpr (std::is_same_v<T, stana::rendering::detail::GPUPolyline>) {
				direct += operation.points.size() * sizeof(Point);
			} else if constexpr (std::is_same_v<T, GPUProgram>) {
				const auto &descriptor = operation.descriptor();
				additions.emplace(descriptor.shader.version(), descriptor.shader.source().size());
				for (const auto &[name, resource] : descriptor.resources) {
					direct += name.size() + sizeof(GPUResource);
					std::visit(
						[&](const auto &value) -> void {
							using R = std::decay_t<decltype(value)>;
							if constexpr (std::is_same_v<R, GPUBuffer> ||
								std::is_same_v<R, GPUTexture>) {
								additions.emplace(value.version(), value.bytes().size());
							} else if constexpr (std::is_same_v<R, GPUStateSlot>) {
								additions.emplace(
									value.version(),
									value.descriptor().initialBytes.size() +
										value.descriptor().resetKey.size()
								);
							}
						},
						resource
					);
				}
				for (const auto &pass : descriptor.passes) {
					std::visit(
						[&](const auto &value) -> void {
							for (const auto &bound : value.bindings) {
								direct += sizeof(GPUBinding) + bound.resource.size();
							}
						},
						pass
					);
				}
			}
		},
		command
	);
	std::size_t bytes = data_ ? data_->bytes : 0;
	if (direct > stana::gpu::detail::payloadLimit - bytes) {
		throw std::invalid_argument("GPU drawing aggregate input exceeds budget");
	}
	bytes += direct;
	for (auto iterator = additions.begin(); iterator != additions.end();) {
		if (data_ && data_->owned.contains(iterator->first)) {
			iterator = additions.erase(iterator);
		} else {
			if (iterator->second > stana::gpu::detail::payloadLimit - bytes) {
				throw std::invalid_argument("GPU drawing aggregate input exceeds budget");
			}
			bytes += iterator->second;
			++iterator;
		}
	}
	const RenderVersion next;
	if (!data_ || data_.use_count() != 1) {
		auto candidate = data_ ? std::make_shared<Data>(*data_) : std::make_shared<Data>();
		candidate->commands.push_back(std::move(command));
		candidate->owned.merge(additions);
		candidate->bytes = bytes;
		candidate->version = next;
		data_ = std::move(candidate);
	} else {
		data_->commands.push_back(std::move(command));
		data_->owned.merge(additions);
		data_->bytes = bytes;
		data_->version = next;
	}
}

void GPUDrawing::fillRect(Rect rect, Color value, double radius) {
	rectangle(rect);
	color(value);
	stana::detail::requireNonnegative(radius);
	append(
		stana::rendering::detail::GPURect{
			.bounds = rect,
			.radius = std::min({radius, rect.size.width / 2, rect.size.height / 2}),
			.color = value
		}
	);
}

void GPUDrawing::fillRect(Rect rect, ExtendedColor value, double radius) {
	rectangle(rect);
	stana::detail::requireNonnegative(radius);
	append(
		stana::rendering::detail::GPURect{
			.bounds = rect,
			.radius = std::min({radius, rect.size.width / 2, rect.size.height / 2}),
			.color = value
		}
	);
}

void GPUDrawing::fillEllipse(Rect rect, Color value) {
	rectangle(rect);
	color(value);
	append(stana::rendering::detail::GPUEllipse{.bounds = rect, .color = value});
}

void GPUDrawing::fillEllipse(Rect rect, ExtendedColor value) {
	rectangle(rect);
	append(stana::rendering::detail::GPUEllipse{.bounds = rect, .color = value});
}

void GPUDrawing::drawMesh(GPUMesh mesh) {
	append(std::move(mesh));
}

void GPUDrawing::strokePolyline(
	std::span<const Point> points,
	Color value,
	double width,
	bool closed
) {
	color(value);
	polyline(points, value, width, closed);
}

void GPUDrawing::strokePolyline(
	std::span<const Point> points,
	ExtendedColor value,
	double width,
	bool closed
) {
	polyline(points, value, width, closed);
}

void GPUDrawing::polyline(
	std::span<const Point> points,
	stana::rendering::detail::GPUColor value,
	double width,
	bool closed
) {
	stana::detail::requireNonnegative(width);
	if (points.size() > stana::gpu::detail::pointLimit) {
		throw std::invalid_argument("Polyline point limit");
	}
	point({.x = width, .y = width});
	std::vector<Point> prepared;
	for (const auto position : points) {
		point(position);
		point({.x = position.x - width / 2, .y = position.y - width / 2});
		point({.x = position.x + width / 2, .y = position.y + width / 2});
		if (prepared.empty() || prepared.back() != position) {
			prepared.push_back(position);
		}
	}
	if (prepared.empty() || std::is_eq(width <=> 0.0)) {
		return;
	}
	if (closed && prepared.size() > 1 && prepared.front() == prepared.back()) {
		prepared.pop_back();
	}
	append(
		stana::rendering::detail::GPUPolyline{
			.points = std::move(prepared),
			.width = width,
			.color = value,
			.closed = closed
		}
	);
}

auto GPUDrawing::requiresExtended() const -> bool {
	for (const auto &command : stana::rendering::detail::GPUAccess::commands(*this)) {
		const bool extended = std::visit(
			[](const auto &operation) -> bool {
				using T = std::decay_t<decltype(operation)>;
				if constexpr (std::is_same_v<T, GPUMesh>) {
					return true;
				} else if constexpr (std::is_same_v<T, GPUProgram>) {
					return operation.descriptor().outputColor == GPUColorEncoding::extendedLinear;
				} else {
					return std::holds_alternative<ExtendedColor>(operation.color);
				}
			},
			command
		);
		if (extended) {
			return true;
		}
	}
	return false;
}

auto stana::rendering::detail::GPUAccess::sourceOwner(const GPUShader &value)
	-> std::shared_ptr<const std::string> {
	return {value.data_, &value.data_->source};
}

auto stana::rendering::detail::GPUAccess::bytesOwner(const GPUBuffer &value)
	-> std::shared_ptr<const std::vector<std::byte>> {
	return {value.data_, &value.data_->bytes};
}

auto stana::rendering::detail::GPUAccess::bytesOwner(const GPUTexture &value)
	-> std::shared_ptr<const std::vector<std::byte>> {
	return {value.data_, &value.data_->descriptor.bytes};
}

auto stana::rendering::detail::GPUAccess::initialOwner(const GPUStateSlot &value)
	-> std::shared_ptr<const std::vector<std::byte>> {
	return {value.data_, &value.data_->descriptor.initialBytes};
}

auto stana::rendering::detail::GPUAccess::indicesOwner(const GPUMesh &value)
	-> std::shared_ptr<const std::vector<std::uint32_t>> {
	return {value.data_, &value.data_->indices};
}
} // namespace stana::rendering
