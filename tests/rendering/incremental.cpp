#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
import stana;

namespace {
void require(bool value, const std::string &message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

void sameScene(
	const stana::rendering::SceneSnapshot &left,
	const stana::rendering::SceneSnapshot &right
) {
	require(
		left.size == right.size && left.background == right.background &&
			left.commands.size() == right.commands.size(),
		"complete scene shape differs"
	);
	for (std::size_t index = 0; index < left.commands.size(); ++index) {
		const auto &a = left.commands.at(index);
		const auto &b = right.commands.at(index);
		require(
			a.kind == b.kind && a.bounds == b.bounds && a.color == b.color &&
				std::is_eq(a.cornerRadius <=> b.cornerRadius) &&
				std::is_eq(a.lineWidth <=> b.lineWidth) && a.text == b.text && a.font == b.font,
			"semantic paint differs at " + std::to_string(index)
		);
	}
}

void sameNodes(
	const std::vector<stana::document::NodeSnapshot> &left,
	const std::vector<stana::document::NodeSnapshot> &right
) {
	require(left.size() == right.size(), "visible node count differs");
	for (std::size_t index = 0; index < left.size(); ++index) {
		const auto &a = left.at(index);
		const auto &b = right.at(index);
		require(
			a.id == b.id && a.kind == b.kind && a.text == b.text && a.bounds == b.bounds &&
				a.isEnabled == b.isEnabled && a.isFocused == b.isFocused &&
				a.accessibilityLabel == b.accessibilityLabel,
			"node projection differs"
		);
	}
}

[[nodiscard]] auto presentationAt(double seconds) -> stana::PresentationTimePoint {
	return stana::PresentationTimePoint{stana::PresentationDuration{seconds}};
}

/// @brief C01-C03/C14/C18-C20: sparse work and lazy immutable compatibility views.
void sparseWork() {

	const stana::reactive::Signal text{std::string{"11"}};
	const stana::reactive::Signal color{stana::Color::black()};
	stana::testing::TestHost host;
	host.mount(
		stana::ui::VStack{
			.children = stana::ui::children(
				stana::ui::Text{.text = text, .textStyle = {.foreground = color}},
				stana::ui::Text{.text = "unrelated"},
				stana::ui::Button{.text = "focus"}
			)
		}
	);
	auto before = host.metrics();
	color.set(stana::Color::accent());
	auto after = host.metrics();
	require(
		after.measurements == before.measurements && after.arrangements == before.arrangements &&
			after.paintBuilds == before.paintBuilds + 1,
		"C02 paint-only sparse work"
	);
	require(
		after.sceneMaterializations == 0 && after.nodeMaterializations == 0,
		"C20 production commit never flattens"
	);
	before = after;
	color.set(stana::Color::accent());
	text.set("11");
	require(
		host.metrics().sceneRevisions == before.sceneRevisions,
		"C01 resolved equality does not publish"
	);
	text.set("22");
	after = host.metrics();
	require(
		after.measurements == before.measurements + 3 && after.arrangements == before.arrangements,
		"C03 changed text plus aggregates, no placement"
	);
	const auto old = host.scene();
	const stana::rendering::SceneSnapshot copy{
		.revision = old->revision,
		.size = old->size,
		.background = old->background,
		.commands = old->commands
	};
	const auto nodes = host.nodes();
	static_cast<void>(host.nodes());
	static_cast<void>(host.scene());
	require(
		host.metrics().sceneMaterializations == 1 && host.metrics().nodeMaterializations == 1,
		"C19 one lazy view per revision"
	);
	const auto button = std::ranges::find_if(nodes, [](const auto &node) -> bool {
		return node.kind == "button";
	});
	require(button != nodes.end() && host.handle(button->id).requestFocus().has_value(), "focus");
	before = host.metrics();
	require(host.handle(button->id).requestFocus().has_value(), "repeat focus");
	require(host.metrics().sceneRevisions == before.sceneRevisions, "C14 repeat focus is no-op");
	host.setEnvironment({.theme = stana::Theme::dark});
	require(host.metrics().measurements == before.measurements, "C18 theme bypasses measurement");
	sameScene(*old, copy);
}

/// @brief C04-C12/C15/C17: fixed-seed differential production recomputation oracle.
void differential() {

	constexpr std::uint32_t seed = 0x51a7a;
	constexpr unsigned operations = 1000;
	constexpr unsigned fixtures = 3;
	constexpr double fixtureWidth = 401;
	constexpr double fixtureHeight = 303;
	constexpr unsigned leftShift = 13;
	constexpr unsigned rightShift = 17;
	constexpr unsigned finalShift = 5;
	auto random = [state = seed]() mutable -> std::uint32_t {
		state ^= state << leftShift;
		state ^= state >> rightShift;
		state ^= state << finalShift;
		return state;
	};
	enum class Edit : std::uint8_t {
		text,
		padding,
		spacing,
		frame,
		visible,
		enabled,
		opacity,
		present,
		environment,
		resize
	};
	for (unsigned fixture = 0; fixture < fixtures; ++fixture) {
		const stana::reactive::Signal text{std::string{"label"}};
		const stana::reactive::Signal padding{stana::Insets::all(stana::Points{0})};
		const stana::reactive::Signal spacing{stana::Points{0}};
		const stana::reactive::Signal frame{stana::Frame{}};
		const stana::reactive::Signal visible{stana::Visibility::visible};
		const stana::reactive::Signal enabled{true};
		const stana::reactive::Signal opacity{stana::Opacity{1}};
		const stana::reactive::Signal present{true};
		stana::testing::TestHost host{
			{.width = stana::Points{fixtureWidth}, .height = stana::Points{fixtureHeight}}
		};
		auto content = stana::ui::VStack{
			.spacing = spacing,
			.layout = {.padding = padding, .frame = frame},
			.appearance = {.opacity = opacity},
			.interaction = {.isEnabled = enabled},
			.children = stana::ui::children(
				stana::ui::Text{.text = text, .appearance = {.visibility = visible}},
				stana::ui::HStack{
					.spacing = spacing,
					.children = stana::ui::children(
						stana::ui::Text{.text = "sibling"},
						stana::ui::Text{.text = "other"}
					)
				},
				stana::ui::If{
					.condition = present,
					.thenContent = stana::ui::Button{.text = "conditional"}
				}
			)
		};
		if (fixture == 0) {
			host.mount(content);
		} else if (fixture == 1) {
			host.mount(stana::ui::ScrollView{.content = content});
		} else {
			host.mount(stana::ui::Box{.children = stana::ui::children(content)});
		}
		for (unsigned iteration = 0; iteration < operations; ++iteration) {
			constexpr unsigned editKinds = 10;
			constexpr unsigned scalarRange = 30;
			const auto choice = random() % editKinds;
			const auto value = static_cast<double>(random() % scalarRange);
			stana::reactive::Transaction::perform([&] -> void {
				switch (static_cast<Edit>(choice)) {
				case Edit::text:
					text.set(std::string(static_cast<std::size_t>(value), 'x'));
					break;
				case Edit::padding:
					padding.set(stana::Insets::all(stana::Points{value}));
					break;
				case Edit::spacing:
					spacing.set(stana::Points{value});
					break;
				case Edit::frame:
					frame.set(
						stana::Frame{
							.width = stana::Length::points(stana::Points{value}),
							.height = stana::Length::fill()
						}
					);
					break;
				case Edit::visible:
					visible.set(static_cast<stana::Visibility>(random() % 3));
					break;
				case Edit::enabled:
					enabled.set(!enabled.get());
					break;
				case Edit::opacity:
					opacity.set(stana::Opacity{value / static_cast<double>(scalarRange)});
					break;
				case Edit::present:
					present.set(!present.get());
					break;
				case Edit::environment:
					host.setEnvironment(
						{.direction = random() % 2 == 0 ? stana::Direction::leftToRight
														: stana::Direction::rightToLeft,
							.textScale =
								stana::ScaleFactor{1 + value / static_cast<double>(scalarRange)}}
					);
					break;
				case Edit::resize:
					host.resize(
						{.width = stana::Points{fixtureWidth + value},
							.height = stana::Points{fixtureHeight - value}}
					);
					break;
				}
			});
			if (fixture == 1) {
				host.scroll({.x = stana::Points{1}, .y = stana::Points{1}}, stana::Points{value});
			}
			const auto incremental = host.scene();
			const auto nodes = host.nodes();
			host.forceFullRecompute();
			sameScene(*incremental, *host.scene());
			sameNodes(nodes, host.nodes());
		}
	}
	std::cout << "Differential seed=" << seed << " fixtures=" << fixtures
			  << " operations/fixture=" << operations << '\n';
}

/// @brief C09/C14: motion and focus invalidate placement/paint, never text measurement.
void motionAndFocus() {

	constexpr double viewport = 100;
	constexpr double contentHeight = 2000;
	constexpr double delta = 50;
	stana::testing::TestHost scroll{
		{.width = stana::Points{viewport}, .height = stana::Points{viewport}}
	};
	scroll.mount(
		stana::ui::ScrollView{
			.content = stana::ui::VStack{
				.layout =
					{.frame =
							stana::Frame{
								.height = stana::Length::points(stana::Points{contentHeight})
							}},
				.children = stana::ui::children(
					stana::ui::Text{.text = "scrolling"},
					stana::ui::Text{.text = "unchanged measurement"}
				)
			}
		}
	);
	const auto beforeNodes = scroll.nodes();
	const auto before = scroll.metrics();
	scroll.scroll({.x = stana::Points{1}, .y = stana::Points{1}}, stana::Points{delta});
	require(
		scroll.metrics().measurements == before.measurements &&
			scroll.metrics().arrangements > before.arrangements,
		"C09 scroll is placement-only"
	);
	const auto afterNodes = scroll.nodes();
	require(
		afterNodes.back().bounds.origin.y < beforeNodes.back().bounds.origin.y,
		"scroll actually moved document coordinates"
	);
	const auto incremental = scroll.scene();
	scroll.forceFullRecompute();
	sameScene(*incremental, *scroll.scene());
	stana::testing::TestHost focus;
	focus.mount(
		stana::ui::VStack{
			.children = stana::ui::children(
				stana::ui::Button{.text = "one"},
				stana::ui::Button{.text = "two"}
			)
		}
	);
	std::vector<stana::NodeId> buttons;
	for (const auto &node : focus.nodes()) {
		if (node.kind == "button") {
			buttons.push_back(node.id);
		}
	}
	require(focus.handle(buttons.front()).requestFocus().has_value(), "first focus");
	const auto oldFocus = focus.metrics();
	require(focus.handle(buttons.back()).requestFocus().has_value(), "second focus");
	require(
		focus.metrics().paintBuilds == oldFocus.paintBuilds + 2 &&
			focus.metrics().measurements == oldFocus.measurements &&
			focus.metrics().arrangements == oldFocus.arrangements,
		"C14 only old/new focus paint changes"
	);
}

/// @brief C21: provider-enqueued bindings cannot publish an intermediate scene or run effects
/// early.
void providerSettlement() {

	constexpr double viewport = 100;
	auto document = stana::document::detail::Document::create(
		{.width = stana::Points{viewport}, .height = stana::Points{viewport}}
	);
	const auto identity =
		document->createNode(stana::document::detail::NodeKind::text, document->root());
	const stana::reactive::Signal text{std::string{"old"}};
	document->bind(
		identity,
		stana::reactive::Property<std::string>{text},
		[](stana::document::detail::Node &node,
			const std::string &value) -> stana::document::detail::Dirty {
			return stana::ui::detail::assignRetained(
				node.text,
				value,
				stana::document::detail::Dirty::measure | stana::document::detail::Dirty::paint |
					stana::document::detail::Dirty::snapshot
			);
		}
	);
	const auto weak = std::weak_ptr{document};
	bool effectSawFinal = false;
	document->setTextMeasure(
		[text,
			weak,
			&effectSawFinal](const std::string &value, stana::Font, stana::Points) -> stana::Size {
			if (value == "middle") {
				stana::reactive::detail::ReactiveRuntime::current().effects.emplace_back(
					[weak, &effectSawFinal] -> void {
						if (const auto owner = weak.lock()) {
							effectSawFinal =
								owner->renderSnapshot()->root->children.front()->snapshot->text ==
								"new";
						}
					}
				);
				text.set("new");
			}
			return {
				.width = stana::Points{static_cast<double>(value.size())},
				.height = stana::Points{1}
			};
		}
	);
	document->commit();
	const auto before = document->metrics().sceneRevisions;
	unsigned publications = 0;
	document->setOnPublish([&] -> void {
		++publications;
	});
	text.set("middle");
	require(
		effectSawFinal && publications == 1 && document->metrics().sceneRevisions == before + 1 &&
			document->renderSnapshot()->root->children.front()->snapshot->text == "new",
		"reentrant binding settlement publishes only final inputs before effects"
	);
	document->close();
}

/// @brief C16/C21/C22: lifecycle, failed publication, provider changes and immutable AVL semantics.
void owningCore() {

	constexpr double viewport = 100;
	auto document = stana::document::detail::Document::create(
		{.width = stana::Points{viewport}, .height = stana::Points{viewport}}
	);
	const auto group =
		document->createNode(stana::document::detail::NodeKind::group, document->root());
	const auto first = document->createNode(stana::document::detail::NodeKind::text, group);
	const auto second = document->createNode(stana::document::detail::NodeKind::text, group);
	bool staleDuringCleanup = false;
	static_cast<void>(document->find(first)->scope.own([&] -> void {
		staleDuringCleanup = !document->handle(first).isAlive();
		document->remove(second);
	}));
	document->commit();
	require(
		stana::rendering::detail::geometryFind(document->renderSnapshot()->geometry, first)
			.has_value(),
		"published child is indexed"
	);
	document->remove(first);
	document->commit();
	require(
		staleDuringCleanup && document->find(second) == nullptr &&
			!stana::rendering::detail::geometryFind(document->renderSnapshot()->geometry, first) &&
			!stana::rendering::detail::geometryFind(document->renderSnapshot()->geometry, second),
		"C16 retire before reentrant cleanup"
	);
	const auto text = document->createNode(stana::document::detail::NodeKind::text, group);
	document->find(text)->text = "provider";
	document->commit();
	const auto old = document->renderSnapshot();
	document->setTextMeasure(
		[] [[noreturn]] (const std::string &, stana::Font, stana::Points) -> stana::Size {
			throw std::runtime_error("provider failure");
		}
	);
	bool rejected = false;
	try {
		document->commit();
	} catch (const std::runtime_error &) {
		rejected = true;
	}
	require(
		rejected && document->renderSnapshot() == old &&
			document->metrics().sceneRevisions == old->revision,
		"C21 no half-publication"
	);
	document->setTextMeasure([](const std::string &, stana::Font, stana::Points) -> stana::Size {
		constexpr double extent = 20;
		return {.width = stana::Points{extent}, .height = stana::Points{extent}};
	});
	document->commit();
	require(document->renderSnapshot()->revision > old->revision, "C17 new provider recovers");
	const auto before = document->renderSnapshot();
	document->find(text)->inert = true;
	document->invalidate(text, stana::document::detail::Dirty::snapshot);
	document->commit();
	require(
		document->renderSnapshot()->root->paint == before->root->paint,
		"C13 inert does not invent paint"
	);
	const auto geometry = document->renderSnapshot()->geometry;
	document->find(text)->foreground = stana::Color::accent();
	document->find(text)->isForegroundExplicit = true;
	document->invalidate(text, stana::document::detail::Dirty::paint);
	document->commit();
	require(
		document->renderSnapshot()->geometry == geometry,
		"paint-only publication shares the exact geometry root"
	);
	const auto weak = std::weak_ptr{document};
	document->setTextMeasure(
		[weak, text](const std::string &, stana::Font, stana::Points) -> stana::Size {
			if (const auto owner = weak.lock()) {
				owner->remove(text);
			}
			return {.width = stana::Points{1}, .height = stana::Points{1}};
		}
	);
	document->commit();
	require(
		!document->handle(text).isAlive(),
		"reentrant provider removal never resumes borrowed storage"
	);

	// An overlap reorder has identical rectangles but changes which control receives a hit.
	auto ordered = stana::document::detail::Document::create(
		{.width = stana::Points{viewport}, .height = stana::Points{viewport}}
	);
	const auto overlay =
		ordered->createNode(stana::document::detail::NodeKind::zstack, ordered->root());
	const auto back = ordered->createNode(stana::document::detail::NodeKind::button, overlay);
	const auto front = ordered->createNode(stana::document::detail::NodeKind::button, overlay);
	ordered->commit();
	const auto initialAttempt = ordered->beginPresentation(1, ordered->renderSnapshot()->revision);
	require(initialAttempt.has_value(), "ordered initial admission");
	static_cast<void>(ordered->reportGPUCompletion(*initialAttempt, true));
	require(
		ordered->reportPresentation(*initialAttempt, presentationAt(1)),
		"ordered initial confirmation"
	);
	const stana::Point overlap{.x = stana::Points{1}, .y = stana::Points{viewport / 2}};
	require(ordered->hitTest(overlap) == front, "initial topmost hit");
	require(ordered->reorder(overlay, {front, back}).has_value(), "overlap reorder");
	ordered->commit();
	require(
		ordered->hitTest(overlap) == front,
		"unsubmitted reorder retains acknowledged hit order"
	);
	const auto reordered = ordered->beginPresentation(1, ordered->renderSnapshot()->revision);
	require(reordered.has_value(), "ordered composite admission");
	ordered->gatePendingPresentation(*reordered);
	require(!ordered->hitTest(overlap), "order-only geometry gates the changed hit route");
	static_cast<void>(ordered->reportGPUCompletion(*reordered, true));
	require(
		ordered->reportPresentation(*reordered, presentationAt(1)) &&
			ordered->hitTest(overlap) == back,
		"confirmed order installs new hit route"
	);

	stana::rendering::detail::GeometryRoot root;
	constexpr std::uint64_t entries = 128;
	for (std::uint64_t serial = 1; serial <= entries; ++serial) {
		root = stana::rendering::detail::geometrySet(
			root,
			{.document = 1, .serial = serial},
			{.bounds = {
				 .size = {
					 .width = stana::Points{static_cast<double>(serial)},
					 .height = stana::Points{1}
				 }
			 }}
		);
	}
	const auto immutable = root;
	for (std::uint64_t serial = 1; serial <= entries; serial += 2) {
		root = stana::rendering::detail::geometryErase(root, {.document = 1, .serial = serial});
	}
	require(
		stana::rendering::detail::geometryFind(immutable, {.document = 1, .serial = 1})
				.has_value() &&
			!stana::rendering::detail::geometryFind(root, {.document = 1, .serial = 1}),
		"C22 immutable erase"
	);
	const stana::NodeId retained{.document = 1, .serial = 2};
	const auto entry = stana::rendering::detail::geometryFind(root, retained);
	if (!entry) {
		throw std::runtime_error("missing AVL entry");
	}
	require(
		stana::rendering::detail::geometrySet(root, retained, *entry) == root,
		"equal geometry shares root"
	);
	std::vector<stana::Rect> changes;
	stana::rendering::detail::geometryChanges(immutable, root, changes);
	require(changes.size() == entries / 2, "rotations do not invent changes");
	const auto canvas = document->createNode(stana::document::detail::NodeKind::cpuCanvas, group);
	document->find(canvas)->drawing.commands.push_back(
		{.kind = stana::rendering::PaintKind::popClip}
	);
	const auto previous = document->renderSnapshot();
	rejected = false;
	try {
		document->commit();
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	require(
		rejected && document->renderSnapshot() == previous,
		"R10 malformed clips do not publish"
	);
}
} // namespace

auto main() -> int {
	try {
		sparseWork();
		motionAndFocus();
		providerSettlement();
		differential();
		owningCore();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
