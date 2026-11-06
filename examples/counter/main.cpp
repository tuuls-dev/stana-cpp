#include <exception>
#include <iostream>
#include <string>

import stana;

/// @brief Runs the introductory retained Counter without native APIs in consumer code.
auto main() -> int {
	using namespace std::string_literals;
	try {

		const stana::reactive::Signal count{0};
		constexpr double windowWidth = 480;
		constexpr double windowHeight = 320;
		constexpr double contentPadding = 28;
		constexpr double stackSpacing = 16;
		constexpr double headingSize = 24;

		const auto window = stana::application::Window{
			.title = "Stana Counter",
			.size = {.width = windowWidth, .height = windowHeight},
			.content = stana::ui::VStack{
				.spacing = stackSpacing,
				.layout = {.padding = stana::Insets::all(contentPadding)},
				.children = stana::ui::children(
					stana::ui::Text{
						.text = count.map([](int value) -> std::string {
							return "Count: " + std::to_string(value);
						}),
						.textStyle = {.font = stana::Font::system(headingSize)}
					},
					stana::ui::Button{
						.text = "Increment",
						.accessibility = {.label = "Increment counter"},
						.events = {stana::ui::on(stana::ui::Event::click, [count] -> void {
							++count;
						})}
					}
				)
			}
		};
		return stana::application::Application::run(window);
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
