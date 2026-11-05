module;

#include <cstdint>
#include <optional>
#include <string>

export module stana:rendering_diagnostics;

export namespace stana::rendering {
/// @brief Cumulative renderer work and current gauges; drawable payload is an estimate.
struct RenderingWork {
	std::uint64_t tileCount = 0;		///< Current tile-layer count.
	std::uint64_t rasterizedTiles = 0;	///< Cumulative fully cleared/replayed tiles.
	std::uint64_t presentedTiles = 0;	///< Cumulative tiles whose native presentation was issued.
	std::uint64_t rasterizedPixels = 0; ///< Cumulative visible physical pixels replayed.
	std::uint64_t copiedBytes =
		0; ///< Committed buffer-to-drawable pixel payload, excluding row padding.
	std::uint64_t cpuUploadBytes = 0;	 ///< Bitmap upload payload; zero in the shared-buffer path.
	std::uint64_t bufferAllocations = 0; ///< Cumulative successful shared Metal buffer allocations.
	std::uint64_t rasterBytes =
		0; ///< Current plus retired aligned shared-buffer bytes; excludes drawables.
	std::uint64_t peakRasterBytes =
		0; ///< Peak owned shared-buffer bytes during this window lifetime.
	std::uint64_t estimatedDrawableBytes =
		0; ///< Estimated tile pixel payload times configured drawable count, not measured VRAM.
	std::uint64_t fullDamageFallbacks =
		0; ///< Conservative viewport selections for unknown ink bounds.
	std::uint64_t recoveries =
		0; ///< Consumed recovery cycles; successful confirmation resets the allowance.
	std::uint64_t anchorAttempts = 0; ///< Committed paint-equivalent revision evidence anchors.
	bool hasActiveBatch = false; ///< Whether a composite or its GPU drain currently owns admission.
	bool hasPendingScene =
		false; ///< Whether latest document demand differs from confirmed display.
};

/// @brief Last-known rendering progress for one active native window.
///
/// `Application::renderingDiagnostics()` returns these values for debugging and
/// native verification. A submitted scene is not necessarily displayed: only a
/// confirmed revision joins GPU success for every copied tile, an issued batch
/// transaction and a positive native timestamp from that batch's proof tile.
/// Other tile timestamps remain diagnostic, not independent display proof. This is acknowledged
/// last-known geometry, not a promise of scanout-atomic input or a performance qualification.
struct WindowDiagnostics {
	std::string deviceName{};
	bool unifiedMemory = false;
	bool linearWorking = false;
	bool linearOutput = false;
	std::uint32_t meshSampleCount = 1;
	std::uint64_t offscreenRevision = 0;
	std::uint64_t stateSteps = 0;
	std::uint64_t pipelineBuilds = 0;
	std::uint64_t pipelineCacheHits = 0;
	std::uint64_t resourceUploadBytes = 0;
	std::uint64_t compositedTiles = 0;
	std::uint64_t gpuLeafDraws = 0; ///< Ordered GPU command contributions encoded into dirty tiles.
	std::uint64_t trackedBytes = 0;
	std::uint64_t peakTrackedBytes = 0;
	std::uint64_t managedWriteBytes = 0;
	std::uint64_t surfaceEpoch = 0; ///< Current native geometry/surface generation.
	std::optional<std::uint64_t> submittedSceneRevision{}; ///< Last GPU submission, if any.
	std::optional<std::uint64_t>
		confirmedSceneRevision{}; ///< Last joined display confirmation, if any.
	std::optional<std::string>
		lastFailure{}; ///< Native failure message, absent when none was recorded.
	RenderingWork
		renderingWork{}; ///< Tile-sized copies remain; cpuUploadBytes excludes command values.
};

} // namespace stana::rendering
