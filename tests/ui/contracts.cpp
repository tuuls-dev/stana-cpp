#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

import stana;

namespace {
void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] auto findKind(const stana::testing::TestHost &host, const std::string &kind)
	-> stana::document::NodeSnapshot {
	const auto nodes = host.nodes();
	const auto found = std::ranges::find(nodes, kind, &stana::document::NodeSnapshot::kind);
	if (found == nodes.end()) {
		throw std::runtime_error("Missing node: " + kind);
	}
	return *found;
}

/// @brief Verifies the native Counter's public authoring pattern without creating a window.
void counterRetainsIdentity() {

	const stana::reactive::Signal count{0};
	constexpr double gap = 16;
	constexpr double padding = 28;
	constexpr double headingSize = 20;
	stana::document::NodeHandle button;
	stana::testing::TestHost host;
	host.mount(
		stana::ui::VStack{
			.spacing = gap,
			.layout = {.padding = stana::Insets::all(padding)},
			.children = stana::ui::children(
				stana::ui::Text{
					.text = count.map([](int value) -> std::string {
						return "Count: " + std::to_string(value);
					}),
					.textStyle = {.font = stana::Font::system(headingSize)}
				},
				stana::ui::Button{
					.text = "Increment",
					.lifecycle = {.onMount = [&](stana::document::NodeHandle handle) -> void {
						button = std::move(handle);
					}},
					.events = {stana::ui::on(stana::ui::Event::click, [count] -> void {
						++count;
					})}
				}
			)
		}
	);
	bool publishedBeforeEffect = false;
	const auto postPublication = stana::reactive::effect(count, [&](int value) -> void {
		publishedBeforeEffect = findKind(host, "text").text == "Count: " + std::to_string(value);
	});
	const auto initial = host.metrics();
	const auto label = findKind(host, "text");
	require(label.text == "Count: 0", "initial label");
	require(button.isAlive(), "post-publication onMount");
	require(button.activate().has_value(), "button activation");
	const auto updated = findKind(host, "text");
	require(updated.id == label.id && updated.text == "Count: 1", "reactive leaf identity");
	require(host.metrics().mounts == initial.mounts, "no remount for ordinary write");
	stana::reactive::Transaction::perform([&] -> void {
		count.set(2);
		require(
			findKind(host, "text").text == "Count: 1",
			"read last complete snapshot inside batch"
		);
		count.set(0);
	});
	require(findKind(host, "text").text == "Count: 0", "commit final batch value");
	const auto hitBounds = findKind(host, "button").bounds;
	host.click({.x = hitBounds.origin.x + 1, .y = hitBounds.origin.y + 1});
	require(count.get() == 1, "pointer click shares activation path");
	require(publishedBeforeEffect, "effects observe published retained state");
	host.close();
	require(!button.isAlive() && !button.activate().has_value(), "close invalidates handles");
}

struct LocalCounter {
	int *mounts;
	int *disposals;

	[[nodiscard]] auto render(stana::ui::ComponentContext &context) const {
		++*mounts;
		const auto state = context.state(0);
		context.onDispose([counter = disposals] -> void {
			++*counter;
		});
		return stana::ui::Button{
			.text = state.map([](int value) -> std::string {
				return std::to_string(value);
			}),
			.events = {stana::ui::on(stana::ui::Event::click, [state] -> void {
				++state;
			})}
		};
	}
};

/// @brief Proves reusable component state belongs to each mounted branch occurrence.
void branchesDisposeLocalState() {

	int mounts = 0;
	int disposals = 0;
	const stana::reactive::Signal visible{true};
	stana::testing::TestHost host;
	host.mount(
		stana::ui::If{
			.condition = visible,
			.thenContent = LocalCounter{.mounts = &mounts, .disposals = &disposals},
			.elseContent = stana::ui::Text{.text = "Hidden"}
		}
	);
	const auto first = findKind(host, "button");
	require(host.activate(first.id).has_value(), "local state action");
	require(findKind(host, "button").text == "1" && mounts == 1, "render once");
	visible.set(false);
	require(disposals == 1 && !host.handle(first.id).isAlive(), "conditional removal disposes");
	visible.set(true);
	require(mounts == 2 && findKind(host, "button").text == "0", "remount has fresh local state");
}

struct Item {
	int id = 0;
	std::string name{};
	auto operator==(const Item &) const -> bool = default;
};

/// @brief Guards row identity across moves and rejection before invalid keyed mutations.
void keyedRowsAreAtomic() {

	const stana::reactive::Signal items{
		std::vector{Item{.id = 1, .name = "A"}, Item{.id = 2, .name = "B"}}
	};
	stana::testing::TestHost host;
	host.mount(
		stana::ui::VStack{
			.children = stana::ui::children(
				stana::ui::ForEach{
					.items = items,
					.key = [](const Item &item) -> int {
						return item.id;
					},
					.content = [](const Item &item) -> stana::ui::Text {
						return {.text = item.name};
					}
				}
			)
		}
	);
	const auto first = findKind(host, "text");
	items.set({{.id = 2, .name = "B"}, {.id = 1, .name = "A"}});
	const auto snapshots = host.nodes();
	const auto old = std::ranges::find(snapshots, first.id, &stana::document::NodeSnapshot::id);
	require(old != snapshots.end() && old->text == "A", "moves retain descendants");
	const auto mounts = host.metrics().mounts;
	items.set({{.id = 1, .name = "A"}, {.id = 1, .name = "duplicate"}});
	require(host.errors() == std::vector{stana::Error::duplicateKey}, "duplicate diagnosed");
	require(
		host.metrics().mounts == mounts && host.handle(first.id).isAlive(),
		"invalid update preserves mounted rows"
	);
	items.set({{.id = 1, .name = "Changed"}});
	require(
		!host.handle(first.id).isAlive() && findKind(host, "text").text == "Changed",
		"unequal item replaces content"
	);
}

[[nodiscard]] auto findText(const stana::rendering::SceneSnapshot &scene, const std::string &text)
	-> const stana::rendering::PaintCommand & {
	const auto found = std::ranges::find_if(
		scene.commands,
		[&](const stana::rendering::PaintCommand &command) -> bool {
			return command.kind == stana::rendering::PaintKind::text && command.text == text;
		}
	);
	if (found == scene.commands.end()) {
		throw std::runtime_error("Missing text command: " + text);
	}
	return *found;
}

[[nodiscard]] auto findFill(const stana::rendering::SceneSnapshot &scene)
	-> const stana::rendering::PaintCommand & {
	const auto found = std::ranges::find(
		scene.commands,
		stana::rendering::PaintKind::fillRect,
		&stana::rendering::PaintCommand::kind
	);
	if (found == scene.commands.end()) {
		throw std::runtime_error("Missing fill command");
	}
	return *found;
}

/// @brief Default labels follow the document theme; explicit colors and buttons stay put.
void themeDefaultsFollowEnvironment() {

	constexpr stana::Color marked = stana::Color::rgb(0.2, 0.4, 0.6);
	stana::testing::TestHost host;
	host.mount(
		stana::ui::VStack{
			.children = stana::ui::children(
				stana::ui::Text{.text = "Count"},
				stana::ui::Text{.text = "Marked", .textStyle = {.foreground = marked}},
				stana::ui::Button{
					.text = "Increment",
					.events = {stana::ui::on(stana::ui::Event::click, [] -> void {})}
				}
			)
		}
	);
	const auto mounts = host.metrics().mounts;
	const auto light = host.scene();
	require(static_cast<bool>(light), "light scene published");
	require(light->background == stana::Color::white(), "light viewport");
	require(findText(*light, "Count").color == stana::Color::black(), "light label");
	require(findText(*light, "Marked").color == marked, "explicit foreground");
	require(findText(*light, "Increment").color == stana::Color::white(), "button label");
	require(
		findFill(*light).color == stana::Color::accent().resolved(stana::Theme::light),
		"button fill"
	);

	auto environment = stana::Environment{};
	environment.theme = stana::Theme::dark;
	host.setEnvironment(environment);
	const auto dark = host.scene();
	require(static_cast<bool>(dark) && host.metrics().mounts == mounts, "theme does not remount");
	require(dark->background == stana::Color::black(), "dark viewport");
	require(findText(*dark, "Count").color == stana::Color::white(), "dark label");
	require(findText(*dark, "Marked").color == marked, "explicit foreground survives theme");
	require(
		findText(*dark, "Increment").color == stana::Color::white(),
		"button label survives theme"
	);
	require(
		findFill(*dark).color == stana::Color::accent().resolved(stana::Theme::dark),
		"button fill survives theme"
	);
}

/// @brief Removal marks handles dead before cleanup can reenter document closure.
void disposalMayCloseTheDocument() {

	stana::testing::TestHost host;
	const stana::reactive::Signal visible{true};
	stana::document::NodeHandle handle;
	bool wasAlreadyDead = false;
	host.mount(
		stana::ui::If{
			.condition = visible,
			.thenContent = stana::ui::Text{
				.text = "Disposable",
				.lifecycle = {
					.onMount = [&](stana::document::NodeHandle mounted) -> void {
						handle = std::move(mounted);
					},
					.onDispose = [&] -> void {
						wasAlreadyDead = !handle.isAlive();
						host.close();
					}
				}
			}
		}
	);
	visible.set(false);
	require(
		wasAlreadyDead && host.nodes().empty(),
		"reentrant close preserves disposal invariants"
	);
}
} // namespace

auto main() -> int {
	try {
		counterRetainsIdentity();
		branchesDisposeLocalState();
		keyedRowsAreAtomic();
		themeDefaultsFollowEnvironment();
		disposalMayCloseTheDocument();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
