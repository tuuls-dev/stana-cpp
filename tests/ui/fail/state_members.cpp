import stana;

constexpr auto rejected = stana::ui::VisualStateAppearance{
	.visibility = stana::Visibility::hidden,
};

auto main() -> int {
	static_cast<void>(rejected);
	return 0;
}
