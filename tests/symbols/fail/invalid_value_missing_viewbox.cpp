import stana;

constexpr auto asset = stana::symbol::compile<R"(<svg/>)">();

auto main() -> int {
	return asset.valid() ? 0 : 1;
}
