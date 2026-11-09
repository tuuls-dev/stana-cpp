import stana;

auto main() -> int {
	// Constructing a macOS extension into PlatformWindowOptions must fail on non-Apple targets.
	stana::application::PlatformWindowOptions options{
		stana::application::platform::macos::WindowOptions{}
	};
	static_cast<void>(options);
	return 0;
}
