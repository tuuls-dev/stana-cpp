#include "canvas_fixture.hpp"
#include "../../../src/gpu/limits.hpp"
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
auto mixedScene(bool extended) -> std::shared_ptr<const stana::platform::detail::Scene> {
	constexpr double extent = 32;
	constexpr double topOrigin = 8;
	constexpr double topExtent = 8;
	constexpr double translucent = 0.5;
	stana::rendering::Drawing back;
	stana::rendering::Drawing front;
	stana::rendering::GPUDrawing picture;
	if (extended) {
		picture.fillRect(
			{.size = {.width = stana::Points{extent}, .height = stana::Points{extent}}},
			stana::rendering::ExtendedColor{2, 0, 0}
		);
	} else {
		back.fillRect(
			{.size = {.width = stana::Points{extent}, .height = stana::Points{extent}}},
			stana::Color::rgb(1, 0, 0)
		);
		picture.fillRect(
			{.size = {.width = stana::Points{extent}, .height = stana::Points{extent}}},
			stana::Color::rgb(0, 1, 0, translucent)
		);
		front.fillRect(
			{.origin = {.x = stana::Points{topOrigin}, .y = stana::Points{topOrigin}},
				.size = {.width = stana::Points{topExtent}, .height = stana::Points{topExtent}}},
			stana::Color::rgb(0, 0, 1)
		);
	}
	const stana::Frame fill{.width = stana::Length::fill(), .height = stana::Length::fill()};
	auto document = stana::document::detail::Document::create(
		{.width = stana::Points{extent}, .height = stana::Points{extent}}
	);
	stana::reactive::Transaction::perform([&] -> void {
		stana::ui::MountContext context{.document = document, .parent = document->root()};
		static_cast<void>(stana::ui::mount(
			stana::ui::ZStack{
				.children = stana::ui::children(
					stana::ui::CPUCanvas{.drawing = back, .layout = {.frame = fill}},
					stana::ui::GPUCanvas{.drawing = picture, .layout = {.frame = fill}},
					stana::ui::CPUCanvas{.drawing = front, .layout = {.frame = fill}}
				)
			},
			context
		));
	});
	document->commit();
	stana::application::detail::SceneConversion conversion;
	const auto result = conversion.convert(document->renderSnapshot());
	document->close();
	return result;
}

auto builtinScenes() -> std::vector<std::shared_ptr<const stana::platform::detail::Scene>> {
	constexpr double width = 1031;
	constexpr double height = 173;
	constexpr double quarter = 0.25;
	constexpr double rounded = 48;
	constexpr double radius = 12;
	constexpr double ellipseX = 64;
	constexpr double ellipseWidth = 80;
	constexpr double ellipseHeight = 32;
	constexpr double top = 8;
	constexpr double meshX = 200;
	constexpr double meshExtent = 80;
	stana::rendering::GPUDrawing picture;
	picture.fillRect(
		{.origin = {.x = stana::Points{quarter}, .y = stana::Points{quarter}},
			.size = {.width = stana::Points{rounded}, .height = stana::Points{rounded}}},
		stana::rendering::ExtendedColor{1, 0, 0},
		stana::Points{radius}
	);
	picture.fillEllipse(
		{.origin = {.x = stana::Points{ellipseX + quarter}, .y = stana::Points{top + quarter}},
			.size = {.width = stana::Points{ellipseWidth}, .height = stana::Points{ellipseHeight}}},
		stana::rendering::ExtendedColor{0, 1, 0}
	);
	const std::array vertices{
		stana::rendering::GPUVertex{
			.position = {.x = stana::Points{meshX + quarter}, .y = stana::Points{top + quarter}},
			.color = stana::rendering::ExtendedColor{1, 0, 0}
		},
		stana::rendering::GPUVertex{
			.position =
				{.x = stana::Points{meshX + meshExtent + quarter},
					.y = stana::Points{top + quarter}},
			.color = stana::rendering::ExtendedColor{0, 1, 0}
		},
		stana::rendering::GPUVertex{
			.position =
				{.x = stana::Points{meshX + quarter},
					.y = stana::Points{top + meshExtent + quarter}},
			.color = stana::rendering::ExtendedColor{0, 0, 1}
		}
	};
	const std::array<std::uint32_t, 3> indices{0, 1, 2};
	const auto mesh = stana::rendering::GPUMesh::create(vertices, indices);
	if (!mesh) {
		throw std::runtime_error(mesh.error().message);
	}
	picture.drawMesh(*mesh);
	constexpr double lineStart = 400;
	constexpr double lineEnd = 560;
	constexpr double lineY = 30;
	constexpr double lineBottom = 90;
	constexpr double lineWidth = 10;
	const std::array line{
		stana::Point{.x = stana::Points{lineStart}, .y = stana::Points{lineY}},
		stana::Point{.x = stana::Points{lineStart}, .y = stana::Points{lineY}},
		stana::Point{.x = stana::Points{lineEnd}, .y = stana::Points{lineY}},
		stana::Point{.x = stana::Points{lineEnd}, .y = stana::Points{lineBottom}}
	};
	picture.strokePolyline(
		line,
		stana::rendering::ExtendedColor{1, 0, 1},
		stana::Points{lineWidth}
	);
	constexpr double dotX = 700;
	const std::array point{stana::Point{.x = stana::Points{dotX}, .y = stana::Points{lineY}}};
	picture.strokePolyline(point, stana::rendering::ExtendedColor{0, 1, 1}, stana::Points{radius});
	const stana::reactive::Signal model{picture};
	const stana::Frame fill{.width = stana::Length::fill(), .height = stana::Length::fill()};
	auto document = stana::document::detail::Document::create(
		{.width = stana::Points{width}, .height = stana::Points{height}}
	);
	stana::reactive::Transaction::perform([&] -> void {
		stana::ui::MountContext context{.document = document, .parent = document->root()};
		static_cast<void>(stana::ui::mount(
			stana::ui::GPUCanvas{.drawing = model, .layout = {.frame = fill}},
			context
		));
	});
	document->commit();
	stana::application::detail::SceneConversion conversion;
	std::vector<std::shared_ptr<const stana::platform::detail::Scene>> scenes{
		conversion.convert(document->renderSnapshot())
	};
	constexpr double changeX = 490.25;
	constexpr double changeY = 110.5;
	constexpr double changeWidth = 70;
	constexpr double changeHeight = 30;
	picture.fillRect(
		{.origin = {.x = stana::Points{changeX}, .y = stana::Points{changeY}},
			.size = {.width = stana::Points{changeWidth}, .height = stana::Points{changeHeight}}},
		stana::rendering::ExtendedColor{1, 1, 0}
	);
	model.set(picture);
	document->commit();
	scenes.push_back(conversion.convert(document->renderSnapshot()));
	document->close();
	return scenes;
}

} // namespace stana::testing::fixtures::platform::macos
