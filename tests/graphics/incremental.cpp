#include <algorithm>
#include <array>
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
import stana.testing;
import stana.core;

namespace {
void require(bool value, const std::string &message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

void sameScene(const stana::SceneSnapshot &left, const stana::SceneSnapshot &right) {
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
	const std::vector<stana::NodeSnapshot> &left,
	const std::vector<stana::NodeSnapshot> &right
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

/// @brief C01-C03/C14/C18-C20: sparse work and lazy immutable compatibility views.
void sparseWork() {
	using namespace stana;
	const Signal text{std::string{"11"}};
	const Signal color{Color::black()};
	TestHost host;
	host.mount(
		VStack{
			.children = children(
				Text{.text = text, .textStyle = {.foreground = color}},
				Text{.text = "unrelated"},
				Button{.text = "focus"}
			)
		}
	);
	auto before = host.metrics();
	color.set(Color::accent());
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
	color.set(Color::accent());
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
	const SceneSnapshot copy{
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
	host.setEnvironment({.theme = Theme::dark});
	require(host.metrics().measurements == before.measurements, "C18 theme bypasses measurement");
	sameScene(*old, copy);
}

/// @brief C04-C12/C15/C17: fixed-seed differential production recomputation oracle.
void differential() {
	using namespace stana;
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
		const Signal text{std::string{"label"}};
		const Signal padding{Insets::all(0)};
		const Signal spacing{0.0};
		const Signal frame{Frame{}};
		const Signal visible{Visibility::visible};
		const Signal enabled{true};
		const Signal opacity{1.0};
		const Signal present{true};
		TestHost host{{.width = fixtureWidth, .height = fixtureHeight}};
		auto content = VStack{
			.spacing = spacing,
			.layout = {.padding = padding, .frame = frame},
			.appearance = {.opacity = opacity},
			.interaction = {.isEnabled = enabled},
			.children = children(
				Text{.text = text, .appearance = {.visibility = visible}},
				HStack{
					.spacing = spacing,
					.children = children(Text{.text = "sibling"}, Text{.text = "other"})
				},
				If{.condition = present, .thenContent = Button{.text = "conditional"}}
			)
		};
		if (fixture == 0) {
			host.mount(content);
		} else if (fixture == 1) {
			host.mount(ScrollView{.content = content});
		} else {
			host.mount(Box{.children = children(content)});
		}
		for (unsigned iteration = 0; iteration < operations; ++iteration) {
			constexpr unsigned editKinds = 10;
			constexpr unsigned scalarRange = 30;
			const auto choice = random() % editKinds;
			const auto value = static_cast<double>(random() % scalarRange);
			Transaction::perform([&] -> void {
				switch (static_cast<Edit>(choice)) {
				case Edit::text:
					text.set(std::string(static_cast<std::size_t>(value), 'x'));
					break;
				case Edit::padding:
					padding.set(Insets::all(value));
					break;
				case Edit::spacing:
					spacing.set(value);
					break;
				case Edit::frame:
					frame.set(Frame{.width = Length::points(value), .height = Length::fill()});
					break;
				case Edit::visible:
					visible.set(static_cast<Visibility>(random() % 3));
					break;
				case Edit::enabled:
					enabled.set(!enabled.get());
					break;
				case Edit::opacity:
					opacity.set(value / static_cast<double>(scalarRange));
					break;
				case Edit::present:
					present.set(!present.get());
					break;
				case Edit::environment:
					host.setEnvironment(
						{.direction =
								random() % 2 == 0 ? Direction::leftToRight : Direction::rightToLeft,
							.textScale = 1 + value / static_cast<double>(scalarRange)}
					);
					break;
				case Edit::resize:
					host.resize({.width = fixtureWidth + value, .height = fixtureHeight - value});
					break;
				}
			});
			if (fixture == 1) {
				host.scroll({.x = 1, .y = 1}, value);
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
	using namespace stana;
	constexpr double viewport = 100;
	constexpr double contentHeight = 2000;
	constexpr double delta = 50;
	TestHost scroll{{.width = viewport, .height = viewport}};
	scroll.mount(
		ScrollView{
			.content = VStack{
				.layout = {.frame = Frame{.height = Length::points(contentHeight)}},
				.children =
					children(Text{.text = "scrolling"}, Text{.text = "unchanged measurement"})
			}
		}
	);
	const auto beforeNodes = scroll.nodes();
	const auto before = scroll.metrics();
	scroll.scroll({.x = 1, .y = 1}, delta);
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
	TestHost focus;
	focus.mount(VStack{.children = children(Button{.text = "one"}, Button{.text = "two"})});
	std::vector<NodeId> buttons;
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
	using namespace stana;
	using namespace stana::detail;
	constexpr double viewport = 100;
	auto document = Document::create({.width = viewport, .height = viewport});
	const auto identity = document->createNode(NodeKind::text, document->root());
	const Signal text{std::string{"old"}};
	document->bind(
		identity,
		Property<std::string>{text},
		[](Node &node, const std::string &value) -> Dirty {
			return assignRetained(
				node.text,
				value,
				Dirty::measure | Dirty::paint | Dirty::snapshot
			);
		}
	);
	const auto weak = std::weak_ptr{document};
	bool effectSawFinal = false;
	document->setTextMeasure(
		[text, weak, &effectSawFinal](const std::string &value, Font, double) -> Size {
			if (value == "middle") {
				ReactiveRuntime::current().effects.emplace_back([weak, &effectSawFinal] -> void {
					if (const auto owner = weak.lock()) {
						effectSawFinal =
							owner->renderSnapshot()->root->children.front()->snapshot->text ==
							"new";
					}
				});
				text.set("new");
			}
			return {.width = static_cast<double>(value.size()), .height = 1};
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
	using namespace stana;
	using namespace stana::detail;
	constexpr double viewport = 100;
	auto document = Document::create({.width = viewport, .height = viewport});
	const auto group = document->createNode(NodeKind::group, document->root());
	const auto first = document->createNode(NodeKind::text, group);
	const auto second = document->createNode(NodeKind::text, group);
	bool staleDuringCleanup = false;
	static_cast<void>(document->find(first)->scope.own([&] -> void {
		staleDuringCleanup = !document->handle(first).isAlive();
		document->remove(second);
	}));
	document->commit();
	require(
		geometryFind(document->renderSnapshot()->geometry, first).has_value(),
		"published child is indexed"
	);
	document->remove(first);
	document->commit();
	require(
		staleDuringCleanup && document->find(second) == nullptr &&
			!geometryFind(document->renderSnapshot()->geometry, first) &&
			!geometryFind(document->renderSnapshot()->geometry, second),
		"C16 retire before reentrant cleanup"
	);
	const auto text = document->createNode(NodeKind::text, group);
	document->find(text)->text = "provider";
	document->commit();
	const auto old = document->renderSnapshot();
	document->setTextMeasure([] [[noreturn]] (const std::string &, Font, double) -> Size {
		throw std::runtime_error("provider failure");
	});
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
	document->setTextMeasure([](const std::string &, Font, double) -> Size {
		constexpr double extent = 20;
		return {.width = extent, .height = extent};
	});
	document->commit();
	require(document->renderSnapshot()->revision > old->revision, "C17 new provider recovers");
	const auto before = document->renderSnapshot();
	document->find(text)->inert = true;
	document->invalidate(text, Dirty::snapshot);
	document->commit();
	require(
		document->renderSnapshot()->root->paint == before->root->paint,
		"C13 inert does not invent paint"
	);
	const auto geometry = document->renderSnapshot()->geometry;
	document->find(text)->foreground = Color::accent();
	document->find(text)->isForegroundExplicit = true;
	document->invalidate(text, Dirty::paint);
	document->commit();
	require(
		document->renderSnapshot()->geometry == geometry,
		"paint-only publication shares the exact geometry root"
	);
	const auto weak = std::weak_ptr{document};
	document->setTextMeasure([weak, text](const std::string &, Font, double) -> Size {
		if (const auto owner = weak.lock()) {
			owner->remove(text);
		}
		return {.width = 1, .height = 1};
	});
	document->commit();
	require(
		!document->handle(text).isAlive(),
		"reentrant provider removal never resumes borrowed storage"
	);

	// An overlap reorder has identical rectangles but changes which control receives a hit.
	auto ordered = Document::create({.width = viewport, .height = viewport});
	const auto overlay = ordered->createNode(NodeKind::zstack, ordered->root());
	const auto back = ordered->createNode(NodeKind::button, overlay);
	const auto front = ordered->createNode(NodeKind::button, overlay);
	ordered->commit();
	const auto initialAttempt = ordered->beginPresentation(1, ordered->renderSnapshot()->revision);
	require(initialAttempt.has_value(), "ordered initial admission");
	static_cast<void>(ordered->reportGPUCompletion(*initialAttempt, true));
	require(ordered->reportPresentation(*initialAttempt, 1), "ordered initial confirmation");
	const Point overlap{.x = 1, .y = viewport / 2};
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
		ordered->reportPresentation(*reordered, 1) && ordered->hitTest(overlap) == back,
		"confirmed order installs new hit route"
	);

	GeometryRoot root;
	constexpr std::uint64_t entries = 128;
	for (std::uint64_t serial = 1; serial <= entries; ++serial) {
		root = geometrySet(
			root,
			{.document = 1, .serial = serial},
			{.bounds = {.size = {.width = static_cast<double>(serial), .height = 1}}}
		);
	}
	const auto immutable = root;
	for (std::uint64_t serial = 1; serial <= entries; serial += 2) {
		root = geometryErase(root, {.document = 1, .serial = serial});
	}
	require(
		geometryFind(immutable, {.document = 1, .serial = 1}).has_value() &&
			!geometryFind(root, {.document = 1, .serial = 1}),
		"C22 immutable erase"
	);
	const NodeId retained{.document = 1, .serial = 2};
	const auto entry = geometryFind(root, retained);
	if (!entry) {
		throw std::runtime_error("missing AVL entry");
	}
	require(geometrySet(root, retained, *entry) == root, "equal geometry shares root");
	std::vector<Rect> changes;
	geometryChanges(immutable, root, changes);
	require(changes.size() == entries / 2, "rotations do not invent changes");
	const auto canvas = document->createNode(NodeKind::canvas, group);
	document->find(canvas)->drawing.commands.push_back({.kind = PaintKind::popClip});
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
