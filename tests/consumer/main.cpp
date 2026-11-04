#include <array>
#include <string_view>

import stana;

auto main() -> int {
	using namespace std::string_view_literals;
	constexpr std::array tiles{stana::TileId{0}, stana::TileId{1}};
	stana::PresentationTracker tracker;
	const auto attempt = tracker.begin(1, 1, tiles);
	if (!attempt ||
		tracker.reportPresented(
			*attempt,
			stana::TilePresentation{.tile = tiles.front(), .timestamp = 1}
		)) {
		return 1;
	}
	stana::Metrics metrics;
	stana::WindowDiagnostics diagnostics;
	if (metrics.measurements != 0 || diagnostics.renderingWork.cpuUploadBytes != 0) {
		return 1;
	}
	return stana::version() == "0.1.0"sv ? 0 : 1;
}
