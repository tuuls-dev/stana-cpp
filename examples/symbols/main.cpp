#include <exception>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

import stana;

// Artwork from
// https://github.com/lucide-icons/lucide/tree/a630cec77eac6568b989e87e4947a8bfe45caacd/icons ISC
// License, Copyright (c) 2026 Lucide Icons and Contributors. Search derives from Feather. MIT
// License, Copyright (c) 2013-present Cole Bemis. Full notices:
// examples/symbols/Lucide-LICENSE.txt.

namespace {

constexpr int columnCount = 5;
constexpr int rowCount = 2;
constexpr double cardWidth = 153;
constexpr double cardHeight = 132;
constexpr double cardPadding = 16;
constexpr double cardSpacing = 10;
constexpr double gridSpacing = 14;
constexpr double sectionSpacing = 20;
constexpr double contentPadding = 28;
constexpr double headingSpacing = 7;
constexpr double headingHeight = 62;
constexpr double footerHeight = 16;
constexpr double symbolSize = 36;
constexpr double symbolHeight = 64;
constexpr double titleSize = 26;
constexpr double subtitleSize = 12;
constexpr double labelSize = 12;
constexpr double footerSize = 11;
constexpr double cornerRadius = 14;
constexpr double borderWidth = 1;
constexpr double shadowOffset = 5;
constexpr double shadowBlur = 16;
constexpr double shadowOpacity = 0.32;
constexpr double insetHighlightOpacity = 0.045;
// System-font weights: NSFontWeightMedium and NSFontWeightSemibold. Zero is regular.
constexpr double mediumWeight = 0.23;
constexpr double semiboldWeight = 0.3;
constexpr double symbolWidth = cardWidth - cardPadding * 2;
constexpr double gridWidth = columnCount * cardWidth + (columnCount - 1) * gridSpacing;
constexpr double gridHeight = rowCount * cardHeight + (rowCount - 1) * gridSpacing;
constexpr double windowWidth = gridWidth + contentPadding * 2;
constexpr double windowHeight =
	gridHeight + headingHeight + footerHeight + sectionSpacing * 2 + contentPadding * 2;

constexpr stana::Frame cardFrame{
	.width = stana::Length::points(cardWidth),
	.height = stana::Length::points(cardHeight)
};
constexpr stana::Frame symbolFrame{
	.width = stana::Length::points(symbolWidth),
	.height = stana::Length::points(symbolHeight)
};
constexpr stana::Frame headingFrame{
	.width = stana::Length::points(gridWidth),
	.height = stana::Length::points(headingHeight)
};
constexpr stana::Frame footerFrame{
	.width = stana::Length::points(gridWidth),
	.height = stana::Length::points(footerHeight)
};

constexpr stana::Color pageColor = stana::Color::rgb(0.035, 0.051, 0.078);
constexpr stana::Color cardColor = stana::Color::rgb(0.067, 0.094, 0.137);
constexpr stana::Color cardBorder = stana::Color::rgb(0.14, 0.19, 0.26);
constexpr stana::Color subtitleColor = stana::Color::rgb(0.56, 0.63, 0.74);
constexpr stana::Color labelColor = stana::Color::rgb(0.77, 0.82, 0.90);
constexpr stana::Color footerColor = stana::Color::rgb(0.47, 0.56, 0.68);
constexpr stana::Color heartTint = stana::Color::rgb(0.99, 0.48, 0.62);
constexpr stana::Color cameraTint = stana::Color::rgb(0.47, 0.72, 1);
constexpr stana::Color searchTint = stana::Color::rgb(0.43, 0.89, 0.74);
constexpr stana::Color houseTint = stana::Color::rgb(0.99, 0.76, 0.40);
constexpr stana::Color settingsTint = stana::Color::rgb(0.73, 0.61, 1);
constexpr stana::Color bellTint = stana::Color::rgb(0.99, 0.85, 0.47);
constexpr stana::Color cloudTint = stana::Color::rgb(0.39, 0.84, 0.94);
constexpr stana::Color codeTint = stana::Color::rgb(0.96, 0.59, 0.39);
constexpr stana::Color starTint = stana::Color::rgb(0.45, 0.82, 0.55);
constexpr stana::Color mailTint = stana::Color::rgb(0.93, 0.45, 0.38);

constexpr auto heart = stana::symbol::compile<R"svg(
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
<path d="M2 9.5a5.5 5.5 0 0 1 9.591-3.676.56.56 0 0 0 .818 0A5.49 5.49 0 0 1 22 9.5c0 2.29-1.5 4-3 5.5l-5.492 5.313a2 2 0 0 1-3 .019L5 15c-1.5-1.5-3-3.2-3-5.5"/>
</svg>
)svg">();

constexpr auto camera = stana::symbol::compile<R"svg(
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
<path d="M13.997 4a2 2 0 0 1 1.76 1.05l.486.9A2 2 0 0 0 18.003 7H20a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V9a2 2 0 0 1 2-2h1.997a2 2 0 0 0 1.759-1.048l.489-.904A2 2 0 0 1 10.004 4z"/>
<circle cx="12" cy="13" r="3"/>
</svg>
)svg">();

constexpr auto search = stana::symbol::compile<R"svg(
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
<path d="m21 21-4.34-4.34"/>
<circle cx="11" cy="11" r="8"/>
</svg>
)svg">();

constexpr auto house = stana::symbol::compile<R"svg(
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
<path d="M15 21v-8a1 1 0 0 0-1-1h-4a1 1 0 0 0-1 1v8"/>
<path d="M3 10a2 2 0 0 1 .709-1.528l7-6a2 2 0 0 1 2.582 0l7 6A2 2 0 0 1 21 10v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>
</svg>
)svg">();

constexpr auto star = stana::symbol::compile<R"svg(
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
<path d="M11.525 2.295a.53.53 0 0 1 .95 0l2.31 4.679a2.123 2.123 0 0 0 1.595 1.16l5.166.756a.53.53 0 0 1 .294.904l-3.736 3.638a2.123 2.123 0 0 0-.611 1.878l.882 5.14a.53.53 0 0 1-.771.56l-4.618-2.428a2.122 2.122 0 0 0-1.973 0L6.396 21.01a.53.53 0 0 1-.77-.56l.881-5.139a2.122 2.122 0 0 0-.611-1.879L2.16 9.795a.53.53 0 0 1 .294-.906l5.165-.755a2.122 2.122 0 0 0 1.597-1.16z"/>
</svg>
)svg">();

constexpr auto settings = stana::symbol::compile<R"svg(
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
<path d="M9.671 4.136a2.34 2.34 0 0 1 4.659 0 2.34 2.34 0 0 0 3.319 1.915 2.34 2.34 0 0 1 2.33 4.033 2.34 2.34 0 0 0 0 3.831 2.34 2.34 0 0 1-2.33 4.033 2.34 2.34 0 0 0-3.319 1.915 2.34 2.34 0 0 1-4.659 0 2.34 2.34 0 0 0-3.32-1.915 2.34 2.34 0 0 1-2.33-4.033 2.34 2.34 0 0 0 0-3.831A2.34 2.34 0 0 1 6.35 6.051a2.34 2.34 0 0 0 3.319-1.915"/>
<circle cx="12" cy="12" r="3"/>
</svg>
)svg">();

constexpr auto bell = stana::symbol::compile<R"svg(
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
<path d="M10.268 21a2 2 0 0 0 3.464 0"/>
<path d="M3.262 15.326A1 1 0 0 0 4 17h16a1 1 0 0 0 .74-1.673C19.41 13.956 18 12.499 18 8A6 6 0 0 0 6 8c0 4.499-1.411 5.956-2.738 7.326"/>
</svg>
)svg">();

constexpr auto cloudDownload = stana::symbol::compile<R"svg(
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
<path d="M12 13v8l-4-4"/>
<path d="m12 21 4-4"/>
<path d="M4.393 15.269A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 2.436 8.284"/>
</svg>
)svg">();

constexpr auto codeXml = stana::symbol::compile<R"svg(
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
<path d="m18 16 4-4-4-4"/>
<path d="m6 8-4 4 4 4"/>
<path d="m14.5 4-5 16"/>
</svg>
)svg">();

constexpr auto mail = stana::symbol::compile<R"svg(
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
<path d="m22 7-8.991 5.727a2 2 0 0 1-2.009 0L2 7"/>
<rect x="2" y="4" width="20" height="16" rx="2"/>
</svg>
)svg">();

auto reportSymbol(const stana::symbol::Diagnostic &diagnostic) -> void {
	std::cerr << diagnostic.message << '\n';
}

/// @brief One labeled Lucide symbol on the gallery's dark card.
[[nodiscard]] auto iconCard(std::string name, const stana::symbol::Asset &asset, stana::Color tint)
	-> stana::ui::VStack<std::tuple<stana::symbol::Symbol, stana::ui::Text>> {
	const std::string label = "Lucide " + name;
	return {
		.spacing = cardSpacing,
		.alignment = stana::Alignment::center,
		.layout = {.padding = stana::Insets::all(cardPadding), .frame = cardFrame},
		.appearance =
			{.background = cardColor,
				.borderColor = cardBorder,
				.borderWidth = borderWidth,
				.cornerRadius = cornerRadius,
				.boxShadows =
					std::vector<stana::rendering::BoxShadow>{
						{.offset = {.x = 0, .y = shadowOffset},
							.blurRadius = shadowBlur,
							.color = stana::Color::rgb(0, 0, 0, shadowOpacity)},
						{.offset = {.x = 0, .y = 1},
							.color = stana::Color::rgb(1, 1, 1, insetHighlightOpacity),
							.isInset = true}
					}},
		.children = stana::ui::children(
			stana::symbol::Symbol{
				.asset = asset,
				.size = symbolSize,
				.color = tint,
				.layout = {.frame = symbolFrame},
				.accessibility = {.label = label},
				.onError = reportSymbol
			},
			stana::ui::Text{
				.text = std::move(name),
				.textStyle =
					{.font = stana::Font::system(labelSize, mediumWeight), .foreground = labelColor}
			}
		)
	};
}

} // namespace

/// @brief Opens the Lucide symbol gallery in a native window.
auto main() -> int {
	try {
		const auto window =
			stana::application::Window{
				.title = "Stana · Lucide icons",
				.size = {.width = windowWidth, .height = windowHeight},
				.content = stana::ui::VStack{
					.spacing = sectionSpacing,
					.alignment = stana::Alignment::center,
					.layout = {.padding = stana::Insets::all(contentPadding)},
					.appearance = {.background = pageColor},
					.children = stana::ui::children(
						stana::ui::VStack{
							.spacing = headingSpacing,
							.layout = {.frame = headingFrame},
							.children = stana::ui::children(
								stana::ui::Text{
									.text = "Lucide, rendered by Stana",
									.textStyle =
										{.font = stana::Font::system(titleSize, semiboldWeight),
											.foreground = stana::Color::white()}
								},
								stana::ui::Text{
									.text = "Ten SVG symbols. Compile-time geometry. Native macOS "
											"rendering.",
									.textStyle =
										{.font = stana::Font::system(subtitleSize),
											.foreground = subtitleColor}
								}
							)
						},
						stana::ui::VStack{
							.spacing = gridSpacing,
							.children = stana::ui::children(
								stana::ui::HStack{
									.spacing = gridSpacing,
									.children = stana::ui::children(
										iconCard("Heart", heart, heartTint),
										iconCard("Camera", camera, cameraTint),
										iconCard("Search", search, searchTint),
										iconCard("House", house, houseTint),
										iconCard("Star", star, starTint)
									)
								},
								stana::ui::HStack{
									.spacing = gridSpacing,
									.children = stana::ui::children(
										iconCard("Settings", settings, settingsTint),
										iconCard("Bell", bell, bellTint),
										iconCard("Cloud Download", cloudDownload, cloudTint),
										iconCard("Code XML", codeXml, codeTint),
										iconCard("Mail", mail, mailTint)
									)
								}
							)
						},
						stana::ui::Text{
							.text =
								"STNSymbol  ·  36 pt  ·  currentColor     /     Artwork: Lucide",
							.layout = {.frame = footerFrame},
							.textStyle =
								{.font = stana::Font::system(footerSize), .foreground = footerColor}
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
