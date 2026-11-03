#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <utility>

import stana;

/// @brief Opens a real Metal-backed Counter, exercises an action and exits its loop.
/// This is opt-in because it requires a logged-in macOS graphical session.
auto main() -> int {
	try {
		using namespace stana;
		const Signal count{0};
		NodeHandle button;
		bool verified = false;
		constexpr auto activationDelay = std::chrono::milliseconds{1500};
		constexpr auto shutdownDelay = std::chrono::milliseconds{300};
		constexpr double width = 320;
		constexpr double height = 200;
		constexpr double padding = 24;
		Application::postAfter(activationDelay, [&] -> void {
			const auto diagnostics = Application::renderingDiagnostics();
			if (!diagnostics.empty() && diagnostics.front().confirmedSceneRevision &&
				button.isAlive() && button.activate()) {
				verified = count.get() == 1;
			} else {
				std::cerr << "No positively confirmed native scene before activation\n";
			}
			Application::postAfter(shutdownDelay, [] -> void {
				Application::stop();
			});
		});
		const auto result = Application::run(
			Window{
				.title = "Stana native smoke",
				.size = {.width = width, .height = height},
				.content = VStack{
					.layout = {.padding = Insets::all(padding)},
					.children = children(
						Text{.text = count.map([](int value) -> std::string {
							return std::to_string(value);
						})},
						Button{
							.text = "Increment",
							.onClick = [count] -> void {
								++count;
							},
							.lifecycle = {.onMount = [&](NodeHandle handle) -> void {
								button = std::move(handle);
							}}
						}
					)
				}
			}
		);
		if (result != 0 || !verified || button.isAlive()) {
			return 1;
		}
		std::cout << "Native window, reactive activation and shutdown passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
