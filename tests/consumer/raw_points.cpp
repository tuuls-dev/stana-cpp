import stana;

auto main() -> int {
	// Quantity types reject implicit scalar conversion; Property may still accept
	// constructible scalars via its converting constructor, so this fixture targets
	// Points/Opacity themselves.
	stana::Points gap = 12.0;
	stana::Opacity fade = 0.5;
	static_cast<void>(gap);
	static_cast<void>(fade);
}
