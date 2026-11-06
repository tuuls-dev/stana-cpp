import stana;

constexpr auto asset = stana::symbol::compile<R"(<g/>)">();

auto main() -> int {
	return asset.valid() ? 0 : 1;
}
