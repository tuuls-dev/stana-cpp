import stana;

auto main() -> int {
	stana::ui::FrameTime frame{
		.timestamp = stana::PresentationTimePoint{},
		.targetTimestamp = stana::PresentationTimePoint{}
	};
	static_cast<void>(frame);
}
