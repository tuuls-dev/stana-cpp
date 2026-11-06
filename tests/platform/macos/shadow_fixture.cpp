#include "shadow_fixture.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
import stana;
#include "../../../src/application/gpu_conversion.hpp"
#include "../../../src/application/scene_conversion.hpp"

namespace stana::testing::fixtures::platform::macos {
constexpr double shadowOpacity = 0.5;
constexpr double editedOffsetMagnitude = 5;
constexpr double editedBlur = 18;
constexpr double insetOpacity = 0.8;
constexpr double changedTop = 10;
constexpr double changedLeading = 260;
constexpr double changedWidth = 125;
constexpr double changedHeight = 85;

auto shadowScenes(bool explicitDrawing)
	-> std::vector<std::shared_ptr<const stana::platform::detail::Scene>> {
	using Shadows = std::vector<stana::rendering::BoxShadow>;
	constexpr double viewportWidth = 600;
	constexpr double viewportHeight = 180;
	constexpr double boxWidth = 90;
	constexpr double boxHeight = 70;
	constexpr double insetX = 240.25;
	constexpr double insetY = 30.5;
	const stana::CornerRadii corners{
		.topLeading = {.width = 30, .height = 20},
		.topTrailing = {.width = 8, .height = 24},
		.bottomTrailing = {.width = 40, .height = 12}
	};
	const stana::BorderWidths edges{.top = 3, .leading = 7, .bottom = 9, .trailing = 2};
	const Shadows outer{
		{.offset = {.x = 8, .y = 6},
			.blurRadius = changedTop,
			.spreadRadius = 2,
			.color = stana::Color::rgb(0, 0, 0, shadowOpacity)}
	};
	const stana::reactive::Signal shadows{outer};
	const stana::reactive::Signal padding{stana::Insets{.top = insetY, .leading = insetX}};
	const stana::reactive::Signal width{stana::Frame{
		.width = stana::Length::points(boxWidth),
		.height = stana::Length::points(boxHeight)
	}};
	auto document = stana::document::detail::Document::create(
		{.width = viewportWidth, .height = viewportHeight}
	);
	const auto drawingFor = [&](const Shadows &list) -> stana::rendering::GPUDrawing {
		stana::rendering::GPUDrawing drawing;
		drawing.drawBoxShadows(
			{.origin = {.x = insetX, .y = insetY},
				.size = {.width = boxWidth, .height = boxHeight}},
			list,
			corners,
			edges
		);
		return drawing;
	};
	const stana::reactive::Signal drawing{drawingFor(outer)};
	stana::reactive::Transaction::perform([&] -> void {
		stana::ui::MountContext context{.document = document, .parent = document->root()};
		if (explicitDrawing) {
			static_cast<void>(stana::ui::mount(
				stana::ui::GPUCanvas{
					.drawing = drawing,
					.layout =
						{.frame =
								stana::Frame{
									.width = stana::Length::fill(),
									.height = stana::Length::fill()
								}}
				},
				context
			));
		} else {
			static_cast<void>(stana::ui::mount(
				stana::ui::Box{
					.layout = {.padding = padding},
					.appearance =
						{.cornerRadii = stana::CornerRadii::all(editedBlur), .clip = true},
					.children = stana::ui::children(
						stana::ui::Box{
							.layout = {.frame = width},
							.appearance =
								{.borderColor = stana::Color::rgb(0, 0, 1),
									.cornerRadii = corners,
									.borderWidths = edges,
									.boxShadows = shadows},
							.children = stana::ui::children(stana::ui::Text{.text = "CPU text"})
						}
					)
				},
				context
			));
		}
	});
	document->commit();
	stana::application::detail::SceneConversion conversion;
	std::vector<std::shared_ptr<const stana::platform::detail::Scene>> scenes;
	const auto capture = [&] -> void {
		document->commit();
		scenes.push_back(conversion.convert(document->renderSnapshot()));
	};
	const auto set = [&](const Shadows &list) -> void {
		shadows.set(list);
		drawing.set(drawingFor(list));
		capture();
	};
	capture();
	auto tinted = outer;
	tinted.front().color = stana::Color::rgb(1, 0, 0, shadowOpacity);
	set(tinted);
	tinted.front().offset.x = -editedOffsetMagnitude;
	set(tinted);
	tinted.front().blurRadius = editedBlur;
	set(tinted);
	tinted.push_back(
		{.offset = {.x = -1, .y = 2},
			.blurRadius = 3,
			.spreadRadius = -2,
			.color = stana::Color::rgb(0, 1, 0, insetOpacity),
			.isInset = true}
	);
	set(tinted);
	set({tinted.back(), outer.front(), tinted.front()});
	padding.set({.top = changedTop, .leading = changedLeading});
	capture();
	width.set(
		{.width = stana::Length::points(changedWidth),
			.height = stana::Length::points(changedHeight)}
	);
	capture();
	set({});
	set(outer);
	document->close();
	return scenes;
}
} // namespace stana::testing::fixtures::platform::macos
