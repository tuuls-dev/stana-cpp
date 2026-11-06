#include "shadows.hpp"
#include "../../gpu/limits.hpp"
#import <Foundation/Foundation.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace stana::platform::detail::gpu {
namespace {
[[nodiscard]] auto failure(Diagnostic::Category category, std::string message)
	-> std::unexpected<Failure> {
	return std::unexpected(Failure{.category = category, .message = std::move(message)});
}

[[nodiscard]] auto pipeline(const Resources &resources, NSString *name)
	-> Result<id<MTLComputePipelineState>> {
	NSError *error = nil;
	id<MTLFunction> const function = [resources.device->builtins newFunctionWithName:name];
	if (function == nil) {
		return failure(Diagnostic::Category::capability, "Missing built-in shadow function");
	}
	id<MTLComputePipelineState> const result =
		[resources.device->device newComputePipelineStateWithFunction:function error:&error];
	if (result == nil) {
		return failure(
			Diagnostic::Category::capability,
			errorMessage(error, "Shadow kernel unavailable")
		);
	}
	return result;
}

[[nodiscard]] auto encode(
	id<MTLCommandBuffer> commands,
	id<MTLComputePipelineState> pipelineState,
	id<MTLTexture> source,
	const ShadowUniform &uniform,
	id<MTLTexture> destination
) -> Result<void> {
	id<MTLComputeCommandEncoder> const encoder = [commands computeCommandEncoder];
	if (encoder == nil) {
		return failure(Diagnostic::Category::execution, "Cannot encode shadow compute pass");
	}
	[encoder setComputePipelineState:pipelineState];
	[encoder setBytes:&uniform length:sizeof(uniform) atIndex:0];
	[encoder setTexture:source atIndex:0];
	[encoder setTexture:destination atIndex:1];
	const NSUInteger width = pipelineState.threadExecutionWidth;
	const NSUInteger height =
		std::min<NSUInteger>(8, pipelineState.maxTotalThreadsPerThreadgroup / width);
	[encoder dispatchThreads:MTLSizeMake(destination.width, destination.height, 1)
		threadsPerThreadgroup:MTLSizeMake(width, height, 1)];
	[encoder endEncoding];
	return {};
}
} // namespace

auto packContour(const stana::gpu::detail::BoxContour &contour) -> PackedContour {
	PackedContour result;
	for (std::size_t index = 0; index < 4; ++index) {
		result.bounds.at(index) = static_cast<float>(contour.bounds.at(index));
	}
	result.top = {
		static_cast<float>(contour.radii.at(0).at(0)),
		static_cast<float>(contour.radii.at(0).at(1)),
		static_cast<float>(contour.radii.at(1).at(0)),
		static_cast<float>(contour.radii.at(1).at(1))
	};
	result.bottom = {
		static_cast<float>(contour.radii.at(3).at(0)),
		static_cast<float>(contour.radii.at(3).at(1)),
		static_cast<float>(contour.radii.at(2).at(0)),
		static_cast<float>(contour.radii.at(2).at(1))
	};
	return result;
}

auto Shadows::initialize(const Resources &resources) -> Result<void> {
	const auto mask = pipeline(resources, @"stana_shadow_mask");
	const auto blur = pipeline(resources, @"stana_shadow_blur");
	if (!mask || !blur) {
		return !mask ? std::unexpected(mask.error()) : std::unexpected(blur.error());
	}
	mask_ = *mask;
	blur_ = *blur;
	return {};
}

auto Shadows::prepare(
	Resources &resources,
	id<MTLCommandBuffer> commands,
	const stana::gpu::detail::Shadow &shadow,
	double scale,
	LogicalRect viewport
) -> Result<PreparedShadow> {
	// Three sigma discards <0.54% total separable Gaussian mass. A coverage pixel and
	// rounded-up physical support are included in both allocation and native damage.
	const double sigma = shadow.blurRadius * scale / 2;
	const double support = std::ceil(3 * sigma);
	constexpr double maximumSupport = 512;
	constexpr double minimumSigma = 0.0001;
	constexpr double identitySigma = 0.001;
	if (!std::isfinite(sigma) || support > maximumSupport) {
		return failure(
			Diagnostic::Category::capability,
			"Shadow blur support exceeds bounded GPU work profile"
		);
	}
	const double halo = support + 1;
	const auto &caster = shadow.caster.bounds;
	const double anchorX = caster.at(0) + shadow.offset.at(0);
	const double anchorY = caster.at(1) + shadow.offset.at(1);
	std::array<double, 4> region{
		-halo,
		-halo,
		std::ceil(caster.at(2) * scale) + 2 * halo,
		std::ceil(caster.at(3) * scale) + 2 * halo
	};
	constexpr double localPixelLimit = 4 * 1024 * 1024;
	if (region.at(2) > stana::gpu::detail::axisLimit ||
		region.at(3) > stana::gpu::detail::axisLimit ||
		region.at(2) * region.at(3) > localPixelLimit) {
		// Huge logical boxes allocate only visible output plus the filtering halo, never their full
		// silhouette.
		region.at(0) = std::floor((viewport.origin.x - anchorX) * scale) - halo;
		region.at(1) = std::floor((viewport.origin.y - anchorY) * scale) - halo;
		region.at(2) = std::ceil(viewport.size.width * scale) + 2 * halo;
		region.at(3) = std::ceil(viewport.size.height * scale) + 2 * halo;
	}
	constexpr double sampleWorkLimit = 256 * 1024 * 1024;
	if (region.at(2) > stana::gpu::detail::axisLimit ||
		region.at(3) > stana::gpu::detail::axisLimit ||
		region.at(2) * region.at(3) * std::max(1.0, 2 * (2 * support + 1)) > sampleWorkLimit) {
		return failure(
			Diagnostic::Category::capability,
			"Shadow intermediates exceed bounded GPU work profile"
		);
	}
	for (const double value :
		{anchorX, anchorY, caster.at(2), caster.at(3), region.at(0), region.at(1)}) {
		if (!std::isfinite(value) ||
			std::abs(value) > static_cast<double>(std::numeric_limits<float>::max()) / 4) {
			return failure(
				Diagnostic::Category::capability,
				"Shadow geometry exceeds GPU coordinate range"
			);
		}
	}
	auto localCaster = shadow.caster;
	localCaster.bounds.at(0) = 0;
	localCaster.bounds.at(1) = 0;
	const ShadowKey key{
		.caster = localCaster,
		.region = region,
		.scale = scale,
		.blurRadius = shadow.blurRadius,
		.inset = shadow.isInset
	};
	PreparedShadow result;
	result.uniform.caster = packContour(localCaster);
	result.uniform.clip = packContour(shadow.clip);
	result.uniform.options = {
		static_cast<float>(scale),
		static_cast<float>(std::max(minimumSigma, sigma)),
		shadow.isInset ? 1.0F : 0.0F,
		0
	};
	result.uniform.blur.at(0) = static_cast<std::uint32_t>(support);
	for (std::size_t index = 0; index < 4; ++index) {
		result.uniform.region.at(index) = static_cast<float>(region.at(index));
	}
	result.bounds = {
		.origin = {.x = anchorX + region.at(0) / scale, .y = anchorY + region.at(1) / scale},
		.size = {.width = region.at(2) / scale, .height = region.at(3) / scale}
	};
	// Inset exterior is infinite. Composite the whole padding clip and sample its mask
	// with explicit opaque out-of-range coverage rather than cropping to the opening.
	if (shadow.isInset) {
		result.bounds = {
			.origin = {.x = shadow.clip.bounds.at(0), .y = shadow.clip.bounds.at(1)},
			.size = {.width = shadow.clip.bounds.at(2), .height = shadow.clip.bounds.at(3)}
		};
	}
	if (const auto found = cache_.find(key); found != cache_.end()) {
		result.mask = found->second.lock();
		if (result.mask) {
			++work_.cacheHits;
		}
	}
	if (!result.mask) {
		const auto width = static_cast<std::uint32_t>(region.at(2));
		const auto height = static_cast<std::uint32_t>(region.at(3));
		auto mask = resources.texture(
			width,
			height,
			Format::rgba16,
			MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite
		);
		if (!mask) {
			return std::unexpected(mask.error());
		}
		const auto made = encode(commands, mask_, nil, result.uniform, (*mask)->texture);
		if (!made) {
			return std::unexpected(made.error());
		}
		++work_.maskBuilds;
		work_.maskPixels += static_cast<std::uint64_t>(width) * height;
		if (sigma >= identitySigma) {
			auto scratch = resources.texture(
				width,
				height,
				Format::rgba16,
				MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite
			);
			auto blurred = resources.texture(
				width,
				height,
				Format::rgba16,
				MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite
			);
			if (!scratch || !blurred) {
				return !scratch ? std::unexpected(scratch.error())
								: std::unexpected(blurred.error());
			}
			const auto horizontal =
				encode(commands, blur_, (*mask)->texture, result.uniform, (*scratch)->texture);
			result.uniform.blur.at(1) = 1;
			const auto vertical = horizontal
				? encode(commands, blur_, (*scratch)->texture, result.uniform, (*blurred)->texture)
				: horizontal;
			if (!vertical) {
				return std::unexpected(vertical.error());
			}
			result.scratch = {*mask, *scratch};
			mask = std::move(blurred);
			work_.blurPasses += 2;
		}
		result.mask = std::make_shared<ShadowMask>(ShadowMask{.texture = *mask});
		constexpr std::size_t metadataLimit = 4096;
		if (cache_.size() >= metadataLimit) {
			std::erase_if(cache_, [](const auto &entry) -> bool {
				return entry.second.expired();
			});
		}
		if (cache_.size() >= metadataLimit) {
			cache_.erase(cache_.begin());
		}
		cache_.insert_or_assign(key, result.mask);
	}
	// Composite uses document-space sampling origin; cache keys above are strictly local.
	result.uniform.region.at(0) = static_cast<float>(anchorX + region.at(0) / scale);
	result.uniform.region.at(1) = static_cast<float>(anchorY + region.at(1) / scale);
	result.uniform.region.at(2) = static_cast<float>(region.at(2) / scale);
	result.uniform.region.at(3) = static_cast<float>(region.at(3) / scale);
	return result;
}
} // namespace stana::platform::detail::gpu
