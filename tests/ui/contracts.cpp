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
import stana.testing;

namespace {
void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] auto findKind(const stana::TestHost &host, const std::string &kind)
	-> stana::NodeSnapshot {
	const auto nodes = host.nodes();
	const auto found = std::ranges::find(nodes, kind, &stana::NodeSnapshot::kind);
	if (found == nodes.end()) {
		throw std::runtime_error("Missing node: " + kind);
	}
	return *found;
}

/// @brief Verifies the native Counter's public authoring pattern without creating a window.
void counterRetainsIdentity() {
	using namespace stana;
	const Signal count{0};
	constexpr double gap = 16;
	constexpr double padding = 28;
	constexpr double headingSize = 20;
	NodeHandle button;
	TestHost host;
	host.mount(
		VStack{
			.spacing = gap,
			.layout = {.padding = Insets::all(padding)},
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
					.lifecycle = {.onMount = [&](NodeHandle handle) -> void {
						button = std::move(handle);
					}}
				}
			)
		}
	);
	bool publishedBeforeEffect = false;
	const auto postPublication = effect(count, [&](int value) -> void {
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
	Transaction::perform([&] -> void {
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

	[[nodiscard]] auto render(stana::ComponentContext &context) const {
		++*mounts;
		const auto state = context.state(0);
		context.onDispose([counter = disposals] -> void {
			++*counter;
		});
		return stana::Button{
			.text = state.map([](int value) -> std::string {
				return std::to_string(value);
			}),
			.onClick = [state] -> void {
				++state;
			}
		};
	}
};

/// @brief Proves reusable component state belongs to each mounted branch occurrence.
void branchesDisposeLocalState() {
	using namespace stana;
	int mounts = 0;
	int disposals = 0;
	const Signal visible{true};
	TestHost host;
	host.mount(
		If{.condition = visible,
			.thenContent = LocalCounter{.mounts = &mounts, .disposals = &disposals},
			.elseContent = Text{.text = "Hidden"}}
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
	using namespace stana;
	const Signal items{std::vector{Item{.id = 1, .name = "A"}, Item{.id = 2, .name = "B"}}};
	TestHost host;
	host.mount(
		VStack{
			.children = children(
				ForEach{
					.items = items,
					.key = [](const Item &item) -> int {
						return item.id;
					},
					.content = [](const Item &item) -> Text {
						return {.text = item.name};
					}
				}
			)
		}
	);
	const auto first = findKind(host, "text");
	items.set({{.id = 2, .name = "B"}, {.id = 1, .name = "A"}});
	const auto snapshots = host.nodes();
	const auto old = std::ranges::find(snapshots, first.id, &NodeSnapshot::id);
	require(old != snapshots.end() && old->text == "A", "moves retain descendants");
	const auto mounts = host.metrics().mounts;
	items.set({{.id = 1, .name = "A"}, {.id = 1, .name = "duplicate"}});
	require(host.errors() == std::vector{Error::duplicateKey}, "duplicate diagnosed");
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

[[nodiscard]] auto findText(const stana::SceneSnapshot &scene, const std::string &text)
	-> const stana::PaintCommand & {
	const auto found =
		std::ranges::find_if(scene.commands, [&](const stana::PaintCommand &command) -> bool {
			return command.kind == stana::PaintKind::text && command.text == text;
		});
	if (found == scene.commands.end()) {
		throw std::runtime_error("Missing text command: " + text);
	}
	return *found;
}

[[nodiscard]] auto findFill(const stana::SceneSnapshot &scene) -> const stana::PaintCommand & {
	const auto found =
		std::ranges::find(scene.commands, stana::PaintKind::fillRect, &stana::PaintCommand::kind);
	if (found == scene.commands.end()) {
		throw std::runtime_error("Missing fill command");
	}
	return *found;
}

/// @brief Default labels follow the document theme; explicit colors and buttons stay put.
void themeDefaultsFollowEnvironment() {
	using namespace stana;
	constexpr Color marked = Color::rgb(0.2, 0.4, 0.6);
	TestHost host;
	host.mount(
		VStack{
			.children = children(
				Text{.text = "Count"},
				Text{.text = "Marked", .textStyle = {.foreground = marked}},
				Button{.text = "Increment", .onClick = [] -> void {}}
			)
		}
	);
	const auto mounts = host.metrics().mounts;
	const auto light = host.scene();
	require(static_cast<bool>(light), "light scene published");
	require(light->background == Color::white(), "light viewport");
	require(findText(*light, "Count").color == Color::black(), "light label");
	require(findText(*light, "Marked").color == marked, "explicit foreground");
	require(findText(*light, "Increment").color == Color::white(), "button label");
	require(findFill(*light).color == Color::accent(), "button fill");

	auto environment = Environment{};
	environment.theme = Theme::dark;
	host.setEnvironment(environment);
	const auto dark = host.scene();
	require(static_cast<bool>(dark) && host.metrics().mounts == mounts, "theme does not remount");
	require(dark->background == Color::black(), "dark viewport");
	require(findText(*dark, "Count").color == Color::white(), "dark label");
	require(findText(*dark, "Marked").color == marked, "explicit foreground survives theme");
	require(findText(*dark, "Increment").color == Color::white(), "button label survives theme");
	require(findFill(*dark).color == Color::accent(), "button fill survives theme");
}

/// @brief Removal marks handles dead before cleanup can reenter document closure.
void disposalMayCloseTheDocument() {
	using namespace stana;
	TestHost host;
	const Signal visible{true};
	NodeHandle handle;
	bool wasAlreadyDead = false;
	host.mount(
		If{.condition = visible,
			.thenContent = Text{
				.text = "Disposable",
				.lifecycle = {
					.onMount = [&](NodeHandle mounted) -> void {
						handle = std::move(mounted);
					},
					.onDispose = [&] -> void {
						wasAlreadyDead = !handle.isAlive();
						host.close();
					}
				}
			}}
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
