import stana;

auto main() -> int {
	const stana::reactive::Signal source{1.0};
	stana::reactive::Property<stana::Points> size = source;
	static_cast<void>(size);
}
