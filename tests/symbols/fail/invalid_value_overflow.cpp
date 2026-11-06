import stana;

constexpr auto asset =
	stana::symbol::compile<R"(<svg viewBox="0 0 1 1"><path opacity="1e999"/></svg>)">();

auto main() -> int {
	return asset.valid() ? 0 : 1;
}
