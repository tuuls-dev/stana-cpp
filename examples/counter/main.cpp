#include <exception>
#include <iostream>
#include <string>

import stana;

/// @brief Runs the introductory retained Counter without native APIs in consumer code.
auto main() -> int {
	using namespace std::string_literals;
	using namespace stana::literals;
	try {

		const stana::reactive::Signal count{0};

		const auto
			window =
				stana::application::
					Window{
						.content =
							stana::ui::VStack{
								.spacing = 16_pt,
								.layout = {.padding = stana::Insets::all(28_pt)},
								.children =
									stana::ui::children(
										stana::ui::Text{
											.text = count.map([](int value) -> std::string {
												return "Count: " + std::to_string(value);
											}),
											.textStyle = {.font = stana::Font::system(24_pt)}
										},
										stana::ui::Button{
											.text = "Increment",
											.accessibility = {.label = "Increment counter"},
											.events =
												{stana::
														ui::on(
															stana::ui::Event::click,
															[count] -> void {
																++count;
															}
														)}
										}
									)
							},
						.options = {
							.geometry = {.contentSize = {.width = 480_pt, .height = 320_pt}},
							.appearance = {.title = "Stana Counter"}
						}
					};
		return stana::application::Application::run(window);
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
