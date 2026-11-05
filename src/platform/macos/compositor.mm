#include "compositor.hpp"
#include "../../graphics/color.hpp"
#include "../../graphics/limits.hpp"
#import <Foundation/Foundation.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace stana::native::gpu {
namespace {
[[nodiscard]] auto failed(Diagnostic::Category category, std::string message)
	-> std::unexpected<Failure> {
	return std::unexpected(Failure{.category = category, .message = std::move(message)});
}

struct BuiltinFunctions {
	NSString *vertex = nil;
	NSString *fragment = nil;
};

[[nodiscard]] auto pipeline(
	id<MTLDevice> device,
	id<MTLLibrary> library,
	const BuiltinFunctions &functions,
	Format format,
	bool blend
) -> Result<id<MTLRenderPipelineState>> {
	MTLRenderPipelineDescriptor *const descriptor = [[MTLRenderPipelineDescriptor alloc] init];
	descriptor.vertexFunction = [library newFunctionWithName:functions.vertex];
	descriptor.fragmentFunction = [library newFunctionWithName:functions.fragment];
	MTLRenderPipelineColorAttachmentDescriptor *const color =
		[descriptor.colorAttachments objectAtIndexedSubscript:0];
	color.pixelFormat = pixelFormat(format);
	color.blendingEnabled = blend;
	color.sourceRGBBlendFactor = MTLBlendFactorOne;
	color.sourceAlphaBlendFactor = MTLBlendFactorOne;
	color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
	color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
	NSError *error = nil;
	id<MTLRenderPipelineState> const result =
		[device newRenderPipelineStateWithDescriptor:descriptor error:&error];
	if (result == nil) {
		return failed(
			Diagnostic::Category::capability,
			errorMessage(error, "Built-in pipeline is unavailable")
		);
	}
	return result;
}

[[nodiscard]] auto intersects(LogicalRect left, LogicalRect right) -> bool {
	return left.origin.x < right.origin.x + right.size.width &&
		right.origin.x < left.origin.x + left.size.width &&
		left.origin.y < right.origin.y + right.size.height &&
		right.origin.y < left.origin.y + left.size.height;
}

[[nodiscard]] auto intersect(LogicalRect left, LogicalRect right) -> LogicalRect {
	const double x = std::max(left.origin.x, right.origin.x);
	const double y = std::max(left.origin.y, right.origin.y);
	return {
		.origin = {.x = x, .y = y},
		.size = {
			.width = std::max(
				0.0,
				std::min(left.origin.x + left.size.width, right.origin.x + right.size.width) - x
			),
			.height = std::max(
				0.0,
				std::min(left.origin.y + left.size.height, right.origin.y + right.size.height) - y
			)
		}
	};
}

[[nodiscard]] auto floats(LogicalRect value) -> std::array<float, 4> {
	return {
		static_cast<float>(value.origin.x),
		static_cast<float>(value.origin.y),
		static_cast<float>(value.size.width),
		static_cast<float>(value.size.height)
	};
}

[[nodiscard]] auto logical(const Rect &rect, LogicalPoint origin = {}) -> LogicalRect {
	return {
		.origin = {.x = origin.x + rect.origin.x, .y = origin.y + rect.origin.y},
		.size = {.width = rect.width, .height = rect.height}
	};
}

[[nodiscard]] auto expanded(LogicalRect rect, double amount) -> LogicalRect {
	return {
		.origin = {.x = rect.origin.x - amount, .y = rect.origin.y - amount},
		.size = {.width = rect.size.width + 2 * amount, .height = rect.size.height + 2 * amount}
	};
}

[[nodiscard]] auto packedColor(const Color &color, bool linear) -> std::array<float, 4> {
	std::array<float, 4> result{};
	for (std::size_t channel = 0; channel < 3; ++channel) {
		double value = color.rgba.at(channel);
		if (color.linear != linear) {
			value =
				linear ? graphics_detail::decodeSRGB(value) : graphics_detail::encodeSRGB(value);
		}
		result.at(channel) = static_cast<float>(value * color.rgba.back());
	}
	result.back() = static_cast<float>(color.rgba.back());
	return result;
}

[[nodiscard]] auto normalized(const native::Command &value) -> native::Command {
	native::Command result{.operation = value.operation};
	if (value.operation == PaintOperation::popClip) {
		return result;
	}
	result.x = value.x;
	result.y = value.y;
	result.width = value.width;
	result.height = value.height;
	if (value.operation == PaintOperation::fill || value.operation == PaintOperation::stroke ||
		value.operation == PaintOperation::pushClip) {
		result.radius = value.radius;
	}
	if (value.operation != PaintOperation::pushClip) {
		result.red = value.red;
		result.green = value.green;
		result.blue = value.blue;
		result.alpha = value.alpha;
	}
	if (value.operation == PaintOperation::stroke || value.operation == PaintOperation::line) {
		result.lineWidth = value.lineWidth;
	}
	if (value.operation == PaintOperation::text) {
		result.text = value.text;
		result.fontSize = value.fontSize;
		result.fontWeight = value.fontWeight;
	}
	return result;
}

[[nodiscard]] auto backing(double logicalSize, double scale) -> Result<std::uint32_t> {
	const double pixels = std::ceil(logicalSize * scale);
	if (!std::isfinite(pixels) || pixels < 0 || pixels > graphics_detail::axisLimit) {
		return failed(
			Diagnostic::Category::capability,
			"Canvas backing extent exceeds the portable/device profile"
		);
	}
	return static_cast<std::uint32_t>(pixels);
}

void increment(std::uint64_t &value) {
	if (value == std::numeric_limits<std::uint64_t>::max()) {
		throw std::overflow_error("Renderer identity exhausted");
	}
	++value;
}

struct DrawItem {
	enum class Kind : std::uint8_t { shape, image, mesh } kind = Kind::shape;
	Quad quad{};
	LogicalRect bounds{};
	std::shared_ptr<TextureOwner> texture{};
	std::shared_ptr<MeshUpload> mesh{};
	std::shared_ptr<BufferOwner> clips{};
	std::shared_ptr<BufferOwner> points{};
};

[[nodiscard]] auto allGPU(const std::shared_ptr<const SceneBranch> &root, bool visibleOnly)
	-> std::vector<const GPUCanvasLeaf *> {
	std::vector<const GPUCanvasLeaf *> result;
	const auto visit = [&](const auto &self,
						   const std::shared_ptr<const SceneBranch> &node) -> void {
		if (!node || (visibleOnly && !node->visible)) {
			return;
		}
		if (node->paint->gpu) {
			result.push_back(&*node->paint->gpu);
		}
		for (const auto &child : node->children) {
			self(self, child);
		}
	};
	visit(visit, root);
	return result;
}
} // namespace

auto Compositor::create(
	std::shared_ptr<DeviceResources> device,
	std::shared_ptr<RasterAccounting> accounting
) -> Result<std::shared_ptr<Compositor>> {
	auto result = std::make_shared<Compositor>();
	result->resources_.device = std::move(device);
	result->rasterAccounting_ = std::move(accounting);
	result->rasterAccounting_->deviceBudget = result->resources_.device->budget;
	result->resources_.budget = result->rasterAccounting_->windowBudget;
	static std::uint64_t windows = 0;
	increment(windows);
	result->window_ = windows;
	for (std::size_t index = 0; index < 2; ++index) {
		const auto format = index == 0 ? Format::bgra8 : Format::rgba16;
		const auto shape = pipeline(
			result->resources_.device->device,
			result->resources_.device->builtins,
			{.vertex = @"stana_quad", .fragment = @"stana_shape"},
			format,
			true
		);
		const auto texture = pipeline(
			result->resources_.device->device,
			result->resources_.device->builtins,
			{.vertex = @"stana_quad", .fragment = @"stana_texture"},
			format,
			true
		);
		const auto mesh = pipeline(
			result->resources_.device->device,
			result->resources_.device->builtins,
			{.vertex = @"stana_mesh", .fragment = @"stana_mesh_fragment"},
			format,
			true
		);
		const auto display = pipeline(
			result->resources_.device->device,
			result->resources_.device->builtins,
			{.vertex = @"stana_quad", .fragment = @"stana_display"},
			format,
			false
		);
		if (!shape || !texture || !mesh || !display) {
			return !shape ? std::unexpected(shape.error())
						  : (!texture ? std::unexpected(texture.error())
									  : (!mesh ? std::unexpected(mesh.error())
											   : std::unexpected(display.error())));
		}
		result->shape_.at(index) = *shape;
		result->texture_.at(index) = *texture;
		result->mesh_.at(index) = *mesh;
		result->display_.at(index) = *display;
	}
	return result;
}

auto Compositor::requiresHybrid(const std::shared_ptr<const Scene> &scene, ColorMode mode) -> bool {
	return mode == ColorMode::extendedLinear || (scene && !allGPU(scene->root, true).empty());
}

auto Compositor::requiresLinear(const std::shared_ptr<const Scene> &scene, ColorMode mode) -> bool {
	if (mode == ColorMode::extendedLinear) {
		return true;
	}
	if (scene) {
		for (const auto *leaf : allGPU(scene->root, true)) {
			if (leaf->drawing->extended) {
				return true;
			}
		}
	}
	return false;
}

void Compositor::desired(std::shared_ptr<const Scene> scene) {
	desired_ = std::move(scene);
	if (!desired_) {
		return;
	}
	std::set<NodeIdentity> alive;
	for (const auto *leaf : allGPU(desired_->root, false)) {
		alive.insert(leaf->instance);
	}
	for (auto iterator = instances_.begin(); iterator != instances_.end();) {
		if (!alive.contains(iterator->first)) {
			iterator = instances_.erase(iterator);
		} else {
			++iterator;
		}
	}
}

void Compositor::reset(std::uint64_t epoch) {
	epoch_ = epoch;
	ready_.reset();
	instances_.clear();
	changed_.clear();
	cpuCache_.clear();
	lineCache_.clear();
	cancelCompilation(resources_.device, window_);
	compilerActive_ = false;
}

void Compositor::close() {
	closed_ = true;
	reset(epoch_);
	desired_.reset();
}

void Compositor::report(const GPUCanvasLeaf &leaf, std::uint64_t program, const Failure &failure) {
	constexpr std::size_t queueLimit = 256;
	if (!diagnostic_ || diagnosticPending_ >= queueLimit) {
		return;
	}
	++diagnosticPending_;
	const auto weak = weak_from_this();
	const auto delivery = diagnostic_;
	const Diagnostic value{
		.category = failure.category,
		.message = failure.message,
		.document = leaf.instance.document,
		.instance = leaf.instance.serial,
		.drawingVersion = leaf.drawing->version,
		.programVersion = program,
		.epoch = epoch_
	};
	dispatch_async(dispatch_get_main_queue(), ^{
	  if (const auto owner = weak.lock()) {
		  --owner->diagnosticPending_;
		  if (!owner->closed_ && owner->epoch_ == value.epoch) {
			  delivery(value);
		  }
	  }
	});
}

void Compositor::compile(const GPUCanvasLeaf &leaf, const std::shared_ptr<const Program> &program) {
	if (compilerActive_) {
		return;
	}
	auto &instance = instances_[leaf.instance];
	if (instance.failedVersion == program->version ||
		instance.requestedVersion == program->version) {
		return;
	}
	compilerActive_ = true;
	instance.requestedVersion = program->version;
	const auto weak = weak_from_this();
	const auto epoch = epoch_;
	requestCompilation(
		resources_.device,
		window_,
		program,
		[weak, epoch, leaf, program](Result<std::shared_ptr<const Pipeline>> compiled) -> void {
			if (const auto owner = weak.lock();
				owner && !owner->closed_ && owner->epoch_ == epoch) {
				owner->compilerActive_ = false;
				const auto found = owner->instances_.find(leaf.instance);
				if (found != owner->instances_.end() && found->second.desired &&
					found->second.desired->version == program->version) {
					if (compiled) {
						found->second.pipeline = std::move(*compiled);
						++owner->work_.pipelineBuilds;
					} else {
						found->second.failedVersion = program->version;
						owner->report(leaf, program->version, compiled.error());
					}
					owner->changed_.push_back(leaf.bounds);
					increment(owner->generation_);
				}
				if (owner->wake_) {
					owner->wake_();
				}
			}
		}
	);
}

auto Compositor::cpu(
	const PreparedOperation &operation,
	const std::vector<PreparedOperation> &clips,
	const PreparedScene &scene,
	const TileGrid &grid,
	id<MTLCommandBuffer> commands
) -> Result<std::shared_ptr<CPUAsset>> {
	if (!operation.ink.bounds && !operation.ink.unknown) {
		return std::shared_ptr<CPUAsset>{};
	}
	CPUKey key{.scale = scene.scale, .viewport = {grid.viewport.width, grid.viewport.height}};
	key.commands.reserve(clips.size() + 1);
	for (const auto &clip : clips) {
		key.commands.push_back(normalized(clip.command));
	}
	key.commands.push_back(normalized(operation.command));
	if (const auto found = cpuCache_.find(key); found != cpuCache_.end()) {
		if (auto value = found->second.lock()) {
			return value;
		}
	}
	LogicalRect ink = operation.ink.bounds.value_or(LogicalRect{.size = scene.source->size});
	ink = intersect(
		ink,
		{.size = {
			 .width = static_cast<double>(grid.viewport.width) / scene.scale,
			 .height = static_cast<double>(grid.viewport.height) / scene.scale
		 }}
	);
	for (const auto &clip : clips) {
		ink = intersect(
			ink,
			{.origin = {.x = clip.command.x, .y = clip.command.y},
				.size = {.width = clip.command.width, .height = clip.command.height}}
		);
	}
	if (ink.size.width <= 0 || ink.size.height <= 0 || operation.command.alpha <= 0) {
		return std::shared_ptr<CPUAsset>{};
	}
	const auto left =
		static_cast<std::size_t>(std::max(0.0, std::floor(ink.origin.x * scene.scale)));
	const auto top =
		static_cast<std::size_t>(std::max(0.0, std::floor(ink.origin.y * scene.scale)));
	const auto right = std::min(
		grid.viewport.width,
		static_cast<std::size_t>(std::ceil((ink.origin.x + ink.size.width) * scene.scale))
	);
	const auto bottom = std::min(
		grid.viewport.height,
		static_cast<std::size_t>(std::ceil((ink.origin.y + ink.size.height) * scene.scale))
	);
	if (right <= left || bottom <= top) {
		return std::shared_ptr<CPUAsset>{};
	}
	const auto pitch = alignedRasterRow(right - left);
	if (pitch > maximumRasterBytes / (bottom - top)) {
		return failed(Diagnostic::Category::capability, "CPU run exceeds raster budget");
	}
	auto asset = std::make_shared<CPUAsset>();
	asset->raster = std::make_shared<RasterTile>(
		TileLayout{
			.pixels = {.x = left, .y = top, .width = right - left, .height = bottom - top},
			.bytesPerRow = pitch,
			.allocationBytes = pitch * (bottom - top)
		},
		resources_.device->device,
		rasterAccounting_
	);
	asset->bounds = {
		.origin =
			{.x = static_cast<double>(left) / scene.scale,
				.y = static_cast<double>(top) / scene.scale},
		.size = {
			.width = static_cast<double>(right - left) / scene.scale,
			.height = static_cast<double>(bottom - top) / scene.scale
		}
	};
	std::vector<PreparedOperation> operations = clips;
	operations.push_back(operation);
	for (std::size_t index = 0; index < clips.size(); ++index) {
		operations.push_back({.command = {.operation = PaintOperation::popClip}});
	}
	rasterRun(asset->raster->bitmapContext, scene.scale, asset->raster->layout.pixels, operations);
	auto texture = resources_.texture(
		static_cast<std::uint32_t>(right - left),
		static_cast<std::uint32_t>(bottom - top),
		Format::bgra8,
		MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget
	);
	if (!texture) {
		return std::unexpected(texture.error());
	}
	asset->texture = std::move(*texture);
	id<MTLBlitCommandEncoder> const copy = [commands blitCommandEncoder];
	if (copy == nil) {
		return failed(Diagnostic::Category::execution, "Cannot encode a CPU run texture");
	}
	encodeTile(copy, *asset->raster, asset->texture->texture);
	[copy endEncoding];
	work_.cpuRasterPixels += (right - left) * (bottom - top);
	constexpr std::size_t metadataLimit = 4096;
	if (cpuCache_.size() >= metadataLimit) {
		std::erase_if(cpuCache_, [](const auto &entry) -> bool {
			return entry.second.expired();
		});
	}
	if (cpuCache_.size() >= metadataLimit) {
		cpuCache_.erase(cpuCache_.begin());
	}
	cpuCache_.insert_or_assign(std::move(key), asset);
	return asset;
}

auto Compositor::prepare(
	std::shared_ptr<const PreparedScene> scene,
	const TileGrid &grid,
	std::uint64_t epoch,
	ColorMode mode
) -> Result<bool> {
	if (!desired_ || scene->source->revision >= desired_->revision) {
		desired(scene->source);
	}
	if (closed_ || inFlight_ || grid.tiles.empty()) {
		return false;
	}
	if (epoch_ != epoch) {
		reset(epoch);
	}
	const bool linear = requiresLinear(scene->source, mode);
	const bool outputLinear = linear && mode != ColorMode::sdr;
	if (ready_ && ready_->scene->source == scene->source && ready_->generation == generation_ &&
		ready_->linear == linear) {
		return true;
	}
	RenderingWork ignored;
	auto dirty = sceneDamage(ready_ ? ready_->scene : nullptr, scene, grid, ignored);
	for (const auto &rect : changed_) {
		selectTiles(grid, scene->scale, rect, dirty);
	}
	changed_.clear();
	if (!ready_ || ready_->linear != linear || ready_->tiles.size() != grid.tiles.size()) {
		dirty = allTiles(grid);
	}
	if (dirty.none() && ready_) {
		auto alias = std::make_shared<CompositePacket>(*ready_);
		alias->scene = scene;
		alias->generation = generation_;
		alias->dirty = {};
		alias->candidates.clear();
		alias->candidateInstances.clear();
		ready_ = std::move(alias);
		work_.completedRevision = scene->source->revision;
		return true;
	}
	auto packet = std::make_shared<CompositePacket>();
	packet->scene = std::move(scene);
	packet->budget = resources_.budget;
	packet->linear = linear;
	packet->outputLinear = outputLinear;
	packet->epoch = epoch;
	packet->generation = generation_;
	packet->dirty = dirty;
	if (ready_) {
		packet->tiles = ready_->tiles;
	}
	packet->tiles.resize(grid.tiles.size());
	id<MTLCommandBuffer> const commands = [resources_.device->queue commandBuffer];
	if (commands == nil) {
		return failed(
			Diagnostic::Category::execution,
			"Cannot create complete composite submission"
		);
	}
	// The packet retains every owner even if later encoding fails before submission.
	[commands addCompletedHandler:^(id<MTLCommandBuffer> completed) {
	  static_cast<void>(completed);
	  static_cast<void>(packet);
	}];
	std::vector<DrawItem> items;
	std::vector<PreparedOperation> clips;
	const auto appendCPU = [&](const PreparedOperation &operation) -> Result<void> {
		if (operation.command.operation == PaintOperation::pushClip) {
			clips.push_back(operation);
			return {};
		}
		if (operation.command.operation == PaintOperation::popClip) {
			if (clips.empty()) {
				return failed(Diagnostic::Category::execution, "Mixed clip stack underflow");
			}
			clips.pop_back();
			return {};
		}
		const auto asset = cpu(operation, clips, *packet->scene, grid, commands);
		if (!asset) {
			return std::unexpected(asset.error());
		}
		if (!*asset) {
			return {};
		}
		packet->cpuAssets.push_back(*asset);
		Quad quad;
		quad.bounds = floats((*asset)->bounds);
		quad.options = {0, 1, 0, 1};
		quad.flags.at(2) = static_cast<std::uint32_t>(linear);
		items.push_back(
			{.kind = DrawItem::Kind::image,
				.quad = quad,
				.bounds = (*asset)->bounds,
				.texture = (*asset)->texture}
		);
		return {};
	};
	const auto appendGPU = [&](const GPUCanvasLeaf &leaf) -> Result<void> {
		if (leaf.opacity <= 0 || leaf.bounds.size.width <= 0 || leaf.bounds.size.height <= 0) {
			return {};
		}
		std::vector<Clip> packedClips;
		packedClips.reserve(clips.size() + 1);
		for (const auto &clip : clips) {
			packedClips.push_back(
				{.bounds =
						{static_cast<float>(clip.command.x),
							static_cast<float>(clip.command.y),
							static_cast<float>(clip.command.width),
							static_cast<float>(clip.command.height)},
					.options = {static_cast<float>(clip.command.radius), 0, 0, 0}}
			);
		}
		packedClips.push_back({.bounds = floats(leaf.bounds)});
		if (packedClips.size() > graphics_detail::payloadLimit / sizeof(Clip)) {
			return failed(
				Diagnostic::Category::capability,
				"Clip stack exceeds bounded upload size"
			);
		}
		auto clipBuffer = resources_.buffer(
			packedClips.size() * sizeof(Clip),
			std::as_bytes(std::span{packedClips})
		);
		if (!clipBuffer) {
			return std::unexpected(clipBuffer.error());
		}
		packet->buffers.push_back(*clipBuffer);
		const auto width = backing(leaf.bounds.size.width, packet->scene->scale);
		const auto height = backing(leaf.bounds.size.height, packet->scene->scale);
		if (!width || !height) {
			report(leaf, 0, !width ? width.error() : height.error());
			return {};
		}
		std::size_t commandIndex = 0;
		for (const auto &command : leaf.drawing->commands) {
			Quad quad;
			quad.flags.at(1) = static_cast<std::uint32_t>(packedClips.size());
			quad.flags.at(2) = static_cast<std::uint32_t>(linear);
			quad.options.at(1) = static_cast<float>(leaf.opacity);
			const auto encoded = std::visit(
				[&](const auto &value) -> Result<void> {
					using T = std::decay_t<decltype(value)>;
					if constexpr (std::is_same_v<T, Shape>) {
						if (value.bounds.width <= 0 || value.bounds.height <= 0) {
							return {};
						}
						const auto geometry = logical(value.bounds, leaf.bounds.origin);
						const auto bounds = expanded(geometry, 1 / packet->scene->scale);
						quad.bounds = floats(bounds);
						quad.geometry = floats(geometry);
						quad.color = packedColor(value.color, linear);
						quad.options.at(0) = static_cast<float>(value.radius);
						quad.flags.at(0) = value.ellipse ? 1U : 0U;
						items.push_back({.quad = quad, .bounds = bounds, .clips = *clipBuffer});
					} else if constexpr (std::is_same_v<T, std::shared_ptr<const Mesh>>) {
						if (value->indices->empty()) {
							return {};
						}
						auto mesh = resources_.mesh(value);
						if (!mesh) {
							return std::unexpected(mesh.error());
						}
						quad.geometry = floats(leaf.bounds);
						packet->meshes.push_back(*mesh);
						items.push_back(
							{.kind = DrawItem::Kind::mesh,
								.quad = quad,
								.bounds = leaf.bounds,
								.mesh = std::move(*mesh),
								.clips = *clipBuffer}
						);
					} else if constexpr (std::is_same_v<T, Polyline>) {
						if (value.points.empty() || value.width <= 0) {
							return {};
						}
						const auto key = std::pair{leaf.drawing->version, commandIndex};
						std::shared_ptr<BufferOwner> points{};
						if (const auto found = lineCache_.find(key); found != lineCache_.end()) {
							points = found->second.lock();
						}
						const bool closed = value.closed && value.points.size() > 1;
						if (!points) {
							std::vector<std::array<float, 2>> packed;
							packed.reserve(value.points.size() + static_cast<std::size_t>(closed));
							for (const auto point : value.points) {
								packed.push_back(
									{static_cast<float>(point.x), static_cast<float>(point.y)}
								);
							}
							if (closed) {
								packed.push_back(packed.front());
							}
							auto uploaded = resources_.buffer(
								packed.size() * sizeof(packed.front()),
								std::as_bytes(std::span{packed})
							);
							if (!uploaded) {
								return std::unexpected(uploaded.error());
							}
							points = std::move(*uploaded);
							constexpr std::size_t lineLimit = 4096;
							if (lineCache_.size() >= lineLimit) {
								std::erase_if(lineCache_, [](const auto &entry) -> bool {
									return entry.second.expired();
								});
							}
							if (lineCache_.size() >= lineLimit) {
								lineCache_.erase(lineCache_.begin());
							}
							lineCache_.insert_or_assign(key, points);
						}
						packet->buffers.push_back(points);
						quad.bounds = floats(leaf.bounds);
						quad.geometry = floats(leaf.bounds);
						quad.color = packedColor(value.color, linear);
						quad.options.at(0) = static_cast<float>(value.width);
						quad.options.at(3) = static_cast<float>(
							value.points.size() + static_cast<std::size_t>(closed)
						);
						quad.flags.at(0) = 2;
						items.push_back(
							{.quad = quad,
								.bounds = leaf.bounds,
								.clips = *clipBuffer,
								.points = std::move(points)}
						);
					} else if constexpr (std::is_same_v<T, std::shared_ptr<const Program>>) {
						auto &instance = instances_[leaf.instance];
						instance.desired = value;
						if ((!instance.pipeline ||
								instance.pipeline->version != value->pipelineVersion) &&
							instance.failedVersion != value->version) {
							compile(leaf, value);
						}
						std::shared_ptr<const ProgramBundle> output = instance.committed;
						const bool newInput = !output ||
							output->source->version != value->version || output->width != *width ||
							output->height != *height;
						const bool available = instance.pipeline &&
							instance.pipeline->version == value->pipelineVersion &&
							instance.failedVersion != value->version;
						if (newInput && available) {
							auto candidate = encodeProgram(
								resources_,
								commands,
								value,
								instance.pipeline,
								*width,
								*height,
								output
							);
							if (candidate) {
								output = candidate->next;
								packet->candidates.push_back(std::move(*candidate));
								packet->candidateInstances.push_back(leaf.instance);
								selectTiles(grid, packet->scene->scale, leaf.bounds, packet->dirty);
							} else {
								instance.failedVersion = value->version;
								report(leaf, value->version, candidate.error());
							}
						} else if (output &&
							(output->width != *width || output->height != *height)) {
							auto candidate = encodeProgram(
								resources_,
								commands,
								output->source,
								output->pipeline,
								*width,
								*height,
								output
							);
							if (candidate) {
								output = candidate->next;
								packet->candidates.push_back(std::move(*candidate));
								packet->candidateInstances.push_back(leaf.instance);
							} else {
								report(leaf, value->version, candidate.error());
							}
						}
						if (output) {
							quad.bounds = floats(leaf.bounds);
							quad.flags.at(3) = static_cast<std::uint32_t>(
								output->source->outputColor == Encoding::linear
							);
							quad.options.at(3) = 1;
							items.push_back(
								{.kind = DrawItem::Kind::image,
									.quad = quad,
									.bounds = leaf.bounds,
									.texture = output->output,
									.clips = *clipBuffer}
							);
						}
					} else {
						static_assert(!sizeof(T), "Unhandled GPU contribution");
					}
					return {};
				},
				command
			);
			if (!encoded) {
				report(leaf, 0, encoded.error());
			}
			++commandIndex;
		}
		return {};
	};
	const auto visit = [&](const auto &self,
						   const std::shared_ptr<const PreparedBranch> &branch) -> Result<void> {
		if (!branch || !branch->source->visible) {
			return {};
		}
		for (const auto &operation : branch->paint->prefix) {
			const auto appended = appendCPU(operation);
			if (!appended) {
				return appended;
			}
		}
		if (branch->source->paint->gpu) {
			const auto appended = appendGPU(*branch->source->paint->gpu);
			if (!appended) {
				return appended;
			}
		}
		for (const auto &child : branch->children) {
			const auto appended = self(self, child);
			if (!appended) {
				return appended;
			}
		}
		for (const auto &operation : branch->paint->suffix) {
			const auto appended = appendCPU(operation);
			if (!appended) {
				return appended;
			}
		}
		return {};
	};
	const auto built = visit(visit, packet->scene->root);
	if (!built) {
		return std::unexpected(built.error());
	}
	if (!clips.empty()) {
		return failed(Diagnostic::Category::execution, "Mixed clip stack did not balance");
	}
	std::size_t finalBytes = 0;
	for (const auto &tile : grid.tiles) {
		if (packet->dirty.test(tile.ordinal)) {
			const auto bpp = pixelBytes(linear ? Format::rgba16 : Format::bgra8);
			const auto bytes = tile.pixels.width * tile.pixels.height * bpp;
			if (bytes > windowAllocationLimit - finalBytes) {
				return failed(
					Diagnostic::Category::capability,
					"Complete final tile candidate exceeds budget"
				);
			}
			finalBytes += bytes;
		}
	}
	auto reservation = resources_.reserve(finalBytes);
	if (!reservation) {
		return std::unexpected(reservation.error());
	}
	const std::array<std::byte, sizeof(Clip)> zero{};
	auto empty = resources_.buffer(zero.size(), zero);
	if (!empty) {
		return std::unexpected(empty.error());
	}
	packet->buffers.push_back(*empty);
	for (const auto &tile : grid.tiles) {
		if (!packet->dirty.test(tile.ordinal)) {
			continue;
		}
		auto texture = resources_.texture(
			static_cast<std::uint32_t>(tile.pixels.width),
			static_cast<std::uint32_t>(tile.pixels.height),
			linear ? Format::rgba16 : Format::bgra8,
			MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead,
			*reservation
		);
		if (!texture) {
			return std::unexpected(texture.error());
		}
		packet->tiles.at(tile.ordinal) = std::move(*texture);
		MTLRenderPassDescriptor *const descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
		MTLRenderPassColorAttachmentDescriptor *const color =
			[descriptor.colorAttachments objectAtIndexedSubscript:0];
		color.texture = packet->tiles.at(tile.ordinal)->texture;
		color.loadAction = MTLLoadActionClear;
		color.storeAction = MTLStoreActionStore;
		const auto background = packet->scene->source->background;
		color.clearColor = MTLClearColorMake(
			linear ? graphics_detail::decodeSRGB(background.red) : background.red,
			linear ? graphics_detail::decodeSRGB(background.green) : background.green,
			linear ? graphics_detail::decodeSRGB(background.blue) : background.blue,
			1
		);
		id<MTLRenderCommandEncoder> const encoder =
			[commands renderCommandEncoderWithDescriptor:descriptor];
		if (encoder == nil) {
			return failed(
				Diagnostic::Category::execution,
				"Complete tile compositor allocation failed"
			);
		}
		const LogicalRect viewport{
			.origin =
				{.x = static_cast<double>(tile.pixels.x) / packet->scene->scale,
					.y = static_cast<double>(tile.pixels.y) / packet->scene->scale},
			.size = {
				.width = static_cast<double>(tile.pixels.width) / packet->scene->scale,
				.height = static_cast<double>(tile.pixels.height) / packet->scene->scale
			}
		};
		for (const auto &item : items) {
			if (!intersects(viewport, item.bounds)) {
				continue;
			}
			auto quad = item.quad;
			quad.viewport = floats(viewport);
			const auto profile = static_cast<std::size_t>(linear);
			[encoder setCullMode:MTLCullModeNone];
			[encoder setFragmentBytes:&quad length:sizeof(quad) atIndex:0];
			[encoder setFragmentBuffer:item.clips ? item.clips->buffer : (*empty)->buffer
								offset:0
							   atIndex:1];
			if (item.kind == DrawItem::Kind::mesh) {
				[encoder setRenderPipelineState:mesh_.at(profile)];
				[encoder setVertexBuffer:item.mesh->vertices->buffer offset:0 atIndex:0];
				[encoder setVertexBytes:&quad length:sizeof(quad) atIndex:1];
				[encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
									indexCount:item.mesh->indexCount
									 indexType:MTLIndexTypeUInt32
								   indexBuffer:item.mesh->indices->buffer
							 indexBufferOffset:0];
			} else {
				[encoder setVertexBytes:&quad length:sizeof(quad) atIndex:0];
				if (item.kind == DrawItem::Kind::image) {
					[encoder setRenderPipelineState:texture_.at(profile)];
					[encoder setFragmentTexture:item.texture->texture atIndex:0];
				} else {
					[encoder setRenderPipelineState:shape_.at(profile)];
					[encoder setFragmentBuffer:item.points ? item.points->buffer : (*empty)->buffer
										offset:0
									   atIndex:2];
				}
				constexpr NSUInteger quadVertices = 6;
				[encoder drawPrimitives:MTLPrimitiveTypeTriangle
							vertexStart:0
							vertexCount:quadVertices];
			}
			++work_.gpuLeafDraws;
		}
		[encoder endEncoding];
		++work_.compositedTiles;
	}
	inFlight_ = true;
	const auto weak = weak_from_this();
	[commands addCompletedHandler:^(id<MTLCommandBuffer> completed) {
	  const bool success = completed.status == MTLCommandBufferStatusCompleted;
	  dispatch_async(dispatch_get_main_queue(), ^{
		if (const auto owner = weak.lock(); owner && !owner->closed_) {
			owner->inFlight_ = false;
			if (owner->epoch_ != epoch) {
				if (owner->wake_) {
					owner->wake_();
				}
				return;
			}
			if (success) {
				for (std::size_t index = 0; index < packet->candidates.size(); ++index) {
					const auto alive =
						allGPU(owner->desired_ ? owner->desired_->root : nullptr, false);
					const auto id = packet->candidateInstances.at(index);
					if (std::ranges::none_of(alive, [&](const auto *leaf) -> bool {
							return leaf->instance == id;
						})) {
						continue;
					}
					if (const auto found = owner->instances_.find(id);
						found != owner->instances_.end()) {
						found->second.committed = packet->candidates.at(index).next;
						++owner->work_.stateSteps;
					}
				}
				owner->ready_ = packet;
				owner->work_.completedRevision = packet->scene->source->revision;
			} else if (owner->executionFailure_) {
				owner->executionFailure_(
					{.category = Diagnostic::Category::execution,
						.message =
							"Offscreen GPU execution failed; candidate state was not committed"}
				);
			}
			if (owner->wake_) {
				owner->wake_();
			}
		}
	  });
	}];
	[commands commit];
	return false;
}

void Compositor::display(
	id<MTLCommandBuffer> commands,
	const CompositePacket &packet,
	std::size_t tile,
	id<MTLTexture> destination,
	double headroom
) const {
	const auto &source = packet.tiles.at(tile);
	if (!packet.linear) {
		id<MTLBlitCommandEncoder> const encoder = [commands blitCommandEncoder];
		if (encoder == nil) {
			throw std::runtime_error("Cannot encode complete SDR tile");
		}
		[encoder copyFromTexture:source->texture
					 sourceSlice:0
					 sourceLevel:0
					sourceOrigin:MTLOriginMake(0, 0, 0)
					  sourceSize:MTLSizeMake(source->texture.width, source->texture.height, 1)
					   toTexture:destination
				destinationSlice:0
				destinationLevel:0
			   destinationOrigin:MTLOriginMake(0, 0, 0)];
		[encoder endEncoding];
		return;
	}
	MTLRenderPassDescriptor *const descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
	MTLRenderPassColorAttachmentDescriptor *const color =
		[descriptor.colorAttachments objectAtIndexedSubscript:0];
	color.texture = destination;
	color.loadAction = MTLLoadActionDontCare;
	color.storeAction = MTLStoreActionStore;
	id<MTLRenderCommandEncoder> const encoder =
		[commands renderCommandEncoderWithDescriptor:descriptor];
	if (encoder == nil) {
		throw std::runtime_error("Cannot encode final display mapping");
	}
	Quad quad;
	quad.bounds =
		{0, 0, static_cast<float>(destination.width), static_cast<float>(destination.height)};
	quad.viewport = quad.bounds;
	quad.options.at(2) =
		static_cast<float>(std::isfinite(headroom) ? std::max(1.0, headroom) : 1.0);
	quad.flags.at(2) = static_cast<std::uint32_t>(packet.outputLinear);
	[encoder setRenderPipelineState:display_.at(static_cast<std::size_t>(packet.outputLinear))];
	[encoder setVertexBytes:&quad length:sizeof(quad) atIndex:0];
	[encoder setFragmentBytes:&quad length:sizeof(quad) atIndex:0];
	[encoder setFragmentTexture:source->texture atIndex:0];
	constexpr NSUInteger vertices = 6;
	[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vertices];
	[encoder endEncoding];
}
} // namespace stana::native::gpu

namespace stana::native::gpu {
void capture(
	const std::shared_ptr<DeviceResources> &device,
	std::shared_ptr<const CompositePacket> packet,
	const TileGrid &grid,
	std::function<void(Result<Capture>)> callback
) {
	const auto retainedPacket = std::move(packet);
	const auto deliver = std::move(callback);
	if (!retainedPacket || retainedPacket->tiles.size() != grid.tiles.size() ||
		grid.viewport.width == 0 || grid.viewport.height == 0) {
		deliver(failed(
			Diagnostic::Category::capability,
			"No complete offscreen retainedPacket is available"
		));
		return;
	}
	const auto format = retainedPacket->linear ? Format::rgba16 : Format::bgra8;
	const auto bpp = pixelBytes(format);
	const auto pitch = ((grid.viewport.width * bpp + rasterRowAlignment - 1) / rasterRowAlignment) *
		rasterRowAlignment;
	if (pitch > graphics_detail::payloadLimit / grid.viewport.height) {
		deliver(failed(Diagnostic::Category::capability, "Readback exceeds bounded payload size"));
		return;
	}
	Resources resources_{.device = device, .budget = retainedPacket->budget};
	auto staging = resources_.buffer(pitch * grid.viewport.height);
	if (!staging) {
		deliver(std::unexpected(staging.error()));
		return;
	}
	id<MTLCommandBuffer> const commands = [device->queue commandBuffer];
	id<MTLBlitCommandEncoder> const encoder = [commands blitCommandEncoder];
	if (commands == nil || encoder == nil) {
		deliver(failed(Diagnostic::Category::execution, "Readback encoder is unavailable"));
		return;
	}
	for (const auto &tile : grid.tiles) {
		[encoder copyFromTexture:retainedPacket->tiles.at(tile.ordinal)->texture
						 sourceSlice:0
						 sourceLevel:0
						sourceOrigin:MTLOriginMake(0, 0, 0)
						  sourceSize:MTLSizeMake(tile.pixels.width, tile.pixels.height, 1)
							toBuffer:(*staging)->buffer
				   destinationOffset:tile.pixels.y * pitch + tile.pixels.x * bpp
			  destinationBytesPerRow:pitch
			destinationBytesPerImage:0];
	}
	if (!device->device.hasUnifiedMemory) {
		[encoder synchronizeResource:(*staging)->buffer];
	}
	[encoder endEncoding];
	const auto owner = std::move(*staging);
	const auto size = grid.viewport;
	[commands addCompletedHandler:^(id<MTLCommandBuffer> completed) {
	  if (completed.status != MTLCommandBufferStatusCompleted) {
		  deliver(failed(Diagnostic::Category::execution, "Readback GPU completion failed"));
		  return;
	  }
	  Capture result{
		  .revision = retainedPacket->scene->source->revision,
		  .width = static_cast<std::uint32_t>(size.width),
		  .height = static_cast<std::uint32_t>(size.height),
		  .scale = retainedPacket->scene->scale,
		  .format = format
	  };
	  const auto row = size.width * bpp;
	  result.bytes.resize(row * size.height);
	  NSData *const source = [NSData dataWithBytes:owner->buffer.contents length:owner->bytes];
	  for (std::size_t y = 0; y < size.height; ++y) {
		  auto destination = std::span{result.bytes}.subspan(y * row, row);
		  [source getBytes:destination.data() range:NSMakeRange(y * pitch, row)];
	  }
	  deliver(std::move(result));
	}];
	[commands commit];
}
} // namespace stana::native::gpu
