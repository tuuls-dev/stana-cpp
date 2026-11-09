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
constexpr double markingX = 70;
constexpr double markingWidth = 20;

auto shadowScenes(ShadowSceneKind kind)
	-> std::vector<std::shared_ptr<const stana::platform::detail::Scene>> {
	using Shadows = std::vector<stana::rendering::BoxShadow>;
	constexpr double viewportWidth = 600;
	constexpr double viewportHeight = 180;
	constexpr double boxWidth = 90;
	constexpr double boxHeight = 70;
	constexpr double insetX = 240.25;
	constexpr double insetY = 30.5;
	const stana::CornerRadii corners{
		.topLeading = {.width = stana::Points{30}, .height = stana::Points{20}},
		.topTrailing = {.width = stana::Points{8}, .height = stana::Points{24}},
		.bottomTrailing = {.width = stana::Points{40}, .height = stana::Points{12}}
	};
	const stana::BorderWidths edges{
		.top = stana::Points{3},
		.leading = stana::Points{7},
		.bottom = stana::Points{9},
		.trailing = stana::Points{2}
	};
	const Shadows outer{
		{.offset = {.x = stana::Points{8}, .y = stana::Points{6}},
			.blurRadius = stana::Points{changedTop},
			.spreadRadius = stana::Points{2},
			.color = stana::Color::rgb(0, 0, 0, shadowOpacity)}
	};
	const stana::reactive::Signal shadows{outer};
	const stana::reactive::Signal padding{
		stana::Insets{.top = stana::Points{insetY}, .leading = stana::Points{insetX}}
	};
	const stana::reactive::Signal width{stana::Frame{
		.width = stana::Length::points(stana::Points{boxWidth}),
		.height = stana::Length::points(stana::Points{boxHeight})
	}};
	auto document = stana::document::detail::Document::create(
		{.width = stana::Points{viewportWidth}, .height = stana::Points{viewportHeight}}
	);
	const auto drawingFor = [&](const Shadows &list) -> stana::rendering::GPUDrawing {
		stana::rendering::GPUDrawing drawing;
		drawing.drawBoxShadows(
			{.origin = {.x = stana::Points{insetX}, .y = stana::Points{insetY}},
				.size = {.width = stana::Points{boxWidth}, .height = stana::Points{boxHeight}}},
			list,
			corners,
			edges
		);
		return drawing;
	};
	const stana::CornerRadii ancestorCorners{
		.topLeading = {.width = stana::Points{400}, .height = stana::Points{150}}
	};
	stana::rendering::GPUDrawing markings;
	markings.fillRect(
		{.origin = {.x = stana::Points{markingX}, .y = stana::Points{0}},
			.size = {.width = stana::Points{markingWidth}, .height = stana::Points{boxHeight}}},
		stana::Color::rgb(0, 0, 1, shadowOpacity)
	);
	const stana::reactive::Signal drawing{drawingFor(outer)};
	stana::reactive::Transaction::perform([&] -> void {
		stana::ui::MountContext context{.document = document, .parent = document->root()};
		if (kind == ShadowSceneKind::explicitDrawing) {
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
			const auto gpuChild = stana::ui::If{
				.condition = kind == ShadowSceneKind::mixed,
				.thenContent = stana::ui::GPUCanvas{
					.drawing = markings,
					.layout = {
						.frame = stana::Frame{
							.width = stana::Length::points(stana::Points{boxWidth}),
							.height = stana::Length::points(stana::Points{boxHeight})
						}
					}
				}
			};
			const auto card = stana::ui::Box{
				.layout = {.frame = width},
				.appearance =
					{.borderColor = stana::Color::rgb(0, 0, 1),
						.cornerRadii = corners,
						.borderWidths = edges,
						.boxShadows = shadows,
						.clip = true},
				.children = stana::ui::children(stana::ui::Text{.text = "CPU text"}, gpuChild)
			};
			static_cast<void>(stana::ui::mount(
				stana::ui::Box{
					.layout = {.padding = padding},
					.appearance = {.cornerRadii = ancestorCorners, .clip = true},
					.children = stana::ui::children(card)
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
	tinted.front().offset.x = stana::Points{-editedOffsetMagnitude};
	set(tinted);
	tinted.front().blurRadius = stana::Points{editedBlur};
	set(tinted);
	tinted.push_back(
		{.offset = {.x = stana::Points{-1}, .y = stana::Points{2}},
			.blurRadius = stana::Points{3},
			.spreadRadius = stana::Points{-2},
			.color = stana::Color::rgb(0, 1, 0, insetOpacity),
			.isInset = true}
	);
	set(tinted);
	set({tinted.back(), outer.front(), tinted.front()});
	padding.set({.top = stana::Points{changedTop}, .leading = stana::Points{changedLeading}});
	capture();
	width.set(
		{.width = stana::Length::points(stana::Points{changedWidth}),
			.height = stana::Length::points(stana::Points{changedHeight})}
	);
	capture();
	set({});
	set(outer);
	document->close();
	return scenes;
}
} // namespace stana::testing::fixtures::platform::macos
