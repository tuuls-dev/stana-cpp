#include <exception>
#include <iostream>
#include <string>

import stana;

using namespace stana;
using namespace stana::application;
using namespace stana::literals;
using namespace stana::reactive;
using namespace stana::ui;

/// @brief Runs the introductory retained Counter without native APIs in consumer code.
auto main() -> int {
	using namespace std::string_literals;

	try {
		const Signal count{0};

		const auto window = Window{
			.content =
				VStack{
					.spacing = 16_pt,
					.layout = {.padding = Insets::all(28_pt)},
					.children = children(
						TextLabel{
							.text = count.map([](int value) -> std::string {
								return "Count: " + std::to_string(value);
							}),
							.textStyle = {.font = Font::system(24_pt)}
						},
						Button{
							.text = "Increment",
							.accessibility = {.label = "Increment counter"},
							.events =
								{on(Event::click,
									[count] -> void {
										++count;
									})}
						}
					)
				},
			.options = {
				.contentSize = Size{.width = 480_pt, .height = 320_pt},
				.title = "Stana Counter"
			}
		};
		return Application::run(window);
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
