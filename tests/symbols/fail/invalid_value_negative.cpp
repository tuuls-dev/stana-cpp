import stana;

constexpr auto asset =
	stana::symbol::compile<R"(<svg viewBox="0 0 24 24"><rect width="-1"/></svg>)">();

auto main() -> int {
	return asset.valid() ? 0 : 1;
}
