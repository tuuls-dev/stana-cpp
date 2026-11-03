#include <exception>
#include <iostream>
#include <string>

import stana;

/// @brief Runs the introductory retained Counter without native APIs in consumer code.
auto main() -> int {
	try {
		using namespace stana;
		const Signal count{0};
		constexpr double windowWidth = 480;
		constexpr double windowHeight = 320;
		constexpr double contentPadding = 28;
		constexpr double stackSpacing = 16;
		constexpr double headingSize = 24;

		const auto window = Window{
			.title = "Stana Counter",
			.size = {.width = windowWidth, .height = windowHeight},
			.content = VStack{
				.spacing = stackSpacing,
				.layout = {.padding = Insets::all(contentPadding)},
				.children = children(
					Text{
						.text = count.map([](int value) -> std::string {
							return "Count: " + std::to_string(value);
						}),
						.textStyle = {.font = Font::system(headingSize)}
					},
					Button{
						.text = "Increment",
						.onClick = [count] -> void {
							++count;
						},
						.accessibility = {.label = "Increment counter"}
					}
				)
			}
		};
		return Application::run(window);
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
