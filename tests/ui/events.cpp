#include <algorithm>
#include <compare>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import stana;

namespace {
void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void requireOrder(
	const std::vector<std::string> &actual,
	const std::vector<std::string> &expected,
	const std::string &message
) {
	if (actual == expected) {
		return;
	}
	std::string rendered = message;
	rendered.append("\nactual:");
	for (const auto &item : actual) {
		rendered.append("\n  ");
		rendered.append(item);
	}
	rendered.append("\nexpected:");
	for (const auto &item : expected) {
		rendered.append("\n  ");
		rendered.append(item);
	}
	throw std::runtime_error(rendered);
}

void requirePoint(stana::Point actual, stana::Point expected, const std::string &message) {
	if (std::is_eq(actual.x <=> expected.x) && std::is_eq(actual.y <=> expected.y)) {
		return;
	}
	throw std::runtime_error(message);
}

[[nodiscard]] auto centerOf(stana::Rect bounds) -> stana::Point {
	constexpr double half = 2;
	return {
		.x = bounds.origin.x + bounds.size.width / half,
		.y = bounds.origin.y + bounds.size.height / half
	};
}

[[nodiscard]] auto square(double side) -> stana::Frame {
	return {
		.width = stana::Length::points(stana::Points{side}),
		.height = stana::Length::points(stana::Points{side})
	};
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

[[nodiscard]] auto findText(const stana::testing::TestHost &host, const std::string &text)
	-> stana::document::NodeSnapshot {
	const auto nodes = host.nodes();
	const auto found = std::ranges::find(nodes, text, &stana::document::NodeSnapshot::text);
	if (found == nodes.end()) {
		throw std::runtime_error("Missing text: " + text);
	}
	return *found;
}

[[nodiscard]] auto mark() -> stana::symbol::Asset {
	constexpr std::uint32_t bridgeVersion = 1;
	constexpr double extent = 12;
	using stana::symbol::Element;
	Element::Path path;
	path.commands = {
		stana::symbol::MoveTo{},
		stana::symbol::LineTo{
			.x = stana::symbol::AssetUnits{extent},
			.y = stana::symbol::AssetUnits{extent}
		}
	};
	path.fill = stana::symbol::NoPaint{};
	path.stroke = stana::symbol::CurrentColor{};
	return stana::symbol::Asset::compiled(
		bridgeVersion,
		{.size =
				{.width = stana::symbol::AssetUnits{extent},
					.height = stana::symbol::AssetUnits{extent}}},
		false,
		{Element{.value = std::move(path)}}
	);
}

/// @brief Custom visual component. Events attach to the mounted group, not to render().
struct Panel {
	stana::ui::Events events{};

	[[nodiscard]] auto render(stana::ui::ComponentContext &) const -> stana::ui::Text {
		return {.text = "panel"};
	}
};

/// @brief Click reaches no-argument and typed handlers, in capture then bubble order, for
/// programmatic, pointer, and keyboard activation. preventDefault leaves later handlers running.
void clickRoutesThroughEveryActivation() {
	std::vector<std::string> order;
	int clicks = 0;
	stana::NodeId buttonId{};
	stana::testing::TestHost host;
	host.mount(
		stana::ui::VStack{
			.events =
				{stana::ui::on(
					 stana::ui::Event::click,
					 [&](stana::document::ClickEvent &event) -> void {
						 order.emplace_back("parent-capture");
						 require(
							 event.phase == stana::ui::Phase::capture,
							 "parent click capture phase"
						 );
					 },
					 stana::ui::Phase::capture
				 ),
					stana::ui::on(
						stana::ui::Event::click,
						[&](stana::document::ClickEvent &) -> void {
							order.emplace_back("parent-bubble");
						}
					)},
			.children = stana::ui::children(
				stana::ui::Button{
					.text = "Save",
					.lifecycle =
						{.onMount = [&](const stana::document::NodeHandle &handle) -> void {
							buttonId = handle.id();
						}},
					.events = {
						stana::ui::on(
							stana::ui::Event::click,
							[&]() -> void {
								order.emplace_back("child");
								++clicks;
							}
						),
						stana::ui::on(
							stana::ui::Event::click,
							[&](stana::document::ClickEvent &event) -> void {
								order.emplace_back("child-typed");
								require(
									event.phase == stana::ui::Phase::bubble,
									"typed click bubbles"
								);
								require(
									event.target == buttonId && event.current == buttonId,
									"click target is the button"
								);
								event.preventDefault();
							}
						),
						stana::ui::on(stana::ui::Event::click, [&]() -> void {
							order.emplace_back("after-prevent");
						})
					}
				}
			)
		}
	);
	const std::vector<std::string>
		expected{"parent-capture", "child", "child-typed", "after-prevent", "parent-bubble"};
	const auto expect = [&]() -> void {
		requireOrder(order, expected, "click route");
		order.clear();
	};
	require(host.activate(buttonId).has_value(), "programmatic activation");
	expect();
	host.click(centerOf(findText(host, "Save").bounds));
	expect();
	require(findText(host, "Save").isFocused, "pointer press focuses the button");
	host.keyDown(stana::document::Key::enter);
	expect();
	host.keyDown(stana::document::Key::space);
	expect();
	const int activated = clicks;
	host.keyUp(stana::document::Key::enter);
	host.keyUp(stana::document::Key::space);
	require(clicks == activated && order.empty(), "key release does not activate");
}

/// @brief Capture may prevent the press default. Bubble handlers on the target still run,
/// and the control is neither focused nor activated.
void pointerPressPreventionSkipsFocus() {
	int childPresses = 0;
	int parentPresses = 0;
	int clicks = 0;
	stana::testing::TestHost host;
	host.mount(
		stana::ui::VStack{
			.events = {stana::ui::on(
				stana::ui::Event::pointerDown,
				[&](auto &event) -> void {
					++parentPresses;
					event.preventDefault();
				},
				stana::ui::Phase::capture
			)},
			.children = stana::ui::children(
				stana::ui::Button{
					.text = "Arm",
					.events = {
						stana::ui::on(
							stana::ui::Event::pointerDown,
							[&](stana::document::PointerEvent &event) -> void {
								++childPresses;
								require(event.defaultPrevented, "child sees capture prevention");
							}
						),
						stana::ui::on(
							stana::ui::Event::pointerDown,
							[&](stana::document::PointerEvent &event) -> void {
								++childPresses;
								require(event.defaultPrevented, "a later press handler still runs");
							}
						),
						stana::ui::on(stana::ui::Event::click, [&]() -> void {
							++clicks;
						})
					}
				}
			)
		}
	);
	const auto center = centerOf(findText(host, "Arm").bounds);
	host.pointerDown(center);
	require(childPresses == 2 && parentPresses == 1, "press delivery survives prevention");
	require(!findText(host, "Arm").isFocused, "prevented press does not focus");
	host.pointerUp(center);
	require(clicks == 0, "prevented press does not activate");
}

/// @brief Release prevention does not cancel activation. The press already armed the control.
void pointerReleasePreventionStillActivates() {
	int clicks = 0;
	stana::testing::TestHost host;
	host.mount(
		stana::ui::Button{
			.text = "Release",
			.events = {
				stana::ui::on(
					stana::ui::Event::pointerUp,
					[&](stana::document::PointerEvent &event) -> void {
						event.preventDefault();
					}
				),
				stana::ui::on(stana::ui::Event::click, [&]() -> void {
					++clicks;
				})
			}
		}
	);
	host.click(centerOf(findText(host, "Release").bounds));
	require(clicks == 1, "pointer release prevention does not cancel activation");
}

/// @brief A toggle flips from pointer, keyboard, and NodeHandle activation. Click prevention
/// on the control or a parent skips the flip after those handlers have run.
void toggleDefaultFollowsClickPrevention() {
	const stana::reactive::Signal<bool> value{false};
	bool blockChild = false;
	bool blockParent = false;
	int childClicks = 0;
	int parentClicks = 0;
	stana::document::NodeHandle toggle;
	stana::testing::TestHost host;
	host.mount(
		stana::ui::VStack{
			.events = {stana::ui::on(
				stana::ui::Event::click,
				[&](stana::document::ClickEvent &event) -> void {
					++parentClicks;
					if (blockParent) {
						event.preventDefault();
					}
				}
			)},
			.children = stana::ui::children(
				stana::ui::Toggle{
					.text = "Flag",
					.value = value.binding(),
					.lifecycle =
						{.onMount = [&](const stana::document::NodeHandle &handle) -> void {
							toggle = handle;
						}},
					.events = {stana::ui::on(
						stana::ui::Event::click,
						[&](stana::document::ClickEvent &event) -> void {
							++childClicks;
							if (blockChild) {
								event.preventDefault();
							}
						}
					)}
				}
			)
		}
	);
	require(host.activate(toggle.id()).has_value() && value.get(), "activate flips the toggle");
	value.set(false);
	host.click(centerOf(findText(host, "Flag").bounds));
	require(value.get(), "pointer release flips the toggle");
	value.set(false);
	require(toggle.requestFocus().has_value(), "toggle accepts focus");
	host.keyDown(stana::document::Key::space);
	require(value.get(), "space flips the focused toggle");
	value.set(false);
	host.keyDown(stana::document::Key::enter);
	require(value.get(), "return flips the focused toggle");
	value.set(false);
	const int flipsBeforeBlock = childClicks;
	blockChild = true;
	require(host.activate(toggle.id()).has_value() && !value.get(), "child click prevention");
	require(childClicks > flipsBeforeBlock && parentClicks > flipsBeforeBlock, "handlers ran");
	blockChild = false;
	blockParent = true;
	host.click(centerOf(findText(host, "Flag").bounds));
	require(!value.get(), "parent click prevention skips the flip");
}

/// @brief Scroll handlers observe the delta before the offset changes. Prevention and a zero
/// delta both leave content where it is.
void scrollPreventionAndZeroDelta() {
	constexpr double viewport = 160;
	constexpr double contentHeight = 640;
	constexpr double delta = 24;
	int scrolls = 0;
	stana::Points reported{};
	bool prevent = true;
	// The root stretches its child to the host, so the viewport is the host size.
	stana::testing::TestHost host{
		{.width = stana::Points{viewport}, .height = stana::Points{viewport}}
	};
	host.mount(
		stana::ui::ScrollView{
			.events = {stana::ui::on(
				stana::ui::Event::scroll,
				[&](stana::document::ScrollEvent &event) -> void {
					++scrolls;
					reported = event.delta;
					if (prevent) {
						event.preventDefault();
					}
				}
			)},
			.content = stana::ui::VStack{
				.layout =
					{.frame =
							stana::Frame{
								.height = stana::Length::points(stana::Points{contentHeight})
							}},
				.children = stana::ui::children(stana::ui::Text{.text = "scrolled"})
			}
		}
	);
	const auto point = centerOf(findKind(host, "scroll").bounds);
	const stana::Points initial = findText(host, "scrolled").bounds.origin.y;
	host.scroll(point, stana::Points{delta});
	require(scrolls == 1 && std::is_eq(reported <=> stana::Points{delta}), "scroll payload");
	require(
		std::is_eq(findText(host, "scrolled").bounds.origin.y <=> initial),
		"prevented scroll does not move"
	);
	prevent = false;
	host.scroll(point, stana::Points{delta});
	require(
		findText(host, "scrolled").bounds.origin.y < initial,
		"scroll moves content after the handler"
	);
	const stana::Points moved = findText(host, "scrolled").bounds.origin.y;
	constexpr double zeroDelta = 0;
	host.scroll(point, stana::Points{zeroDelta});
	require(scrolls == 3, "a zero delta still dispatches");
	require(
		std::is_eq(findText(host, "scrolled").bounds.origin.y <=> moved),
		"a zero delta does not move"
	);
}

/// @brief Tab, shift-tab, focus, and blur follow the focused control. Prevention skips the
/// corresponding default and does not roll focus back.
void keyboardFocusAndDefaults() {
	std::vector<std::string> trace;
	int clicks = 0;
	int releases = 0;
	bool blockTab = false;
	bool blockEnter = false;
	bool sawShiftTab = false;
	stana::NodeId firstId{};
	stana::NodeId secondId{};
	stana::NodeId stackId{};
	stana::testing::TestHost host;
	const auto name = [&](stana::NodeId identity) -> std::string {
		if (identity == firstId) {
			return "first";
		}
		if (identity == secondId) {
			return "second";
		}
		if (identity == stackId) {
			return "stack";
		}
		return "other";
	};
	const auto note = [&](const std::string &kind, const auto &event) -> void {
		const char *const phase = event.phase == stana::ui::Phase::capture ? "capture" : "bubble";
		trace.push_back(kind + " " + phase + " " + name(event.target) + " " + name(event.current));
	};
	const auto onKey = [&](stana::document::KeyEvent &event) -> void {
		require(event.current == event.target, "key current is the focused control");
		if (event.key == stana::document::Key::tab && event.backwards) {
			sawShiftTab = true;
		}
		if (blockTab && event.key == stana::document::Key::tab) {
			event.preventDefault();
		}
		if (blockEnter && event.key == stana::document::Key::enter) {
			event.preventDefault();
		}
	};
	const auto onRelease = [&](stana::document::KeyEvent &event) -> void {
		++releases;
		require(event.phase == stana::ui::Phase::bubble, "key release bubbles");
	};
	host
		.mount(
			stana::ui::VStack{
				.lifecycle = {.onMount = [&](const stana::document::NodeHandle &handle) -> void {
					stackId = handle.id();
				}},
				.events =
					{stana::ui::on(
						 stana::ui::Event::focus,
						 [&](stana::document::FocusEvent &event) -> void {
							 note("focus", event);
						 },
						 stana::ui::Phase::capture
					 ),
						stana::ui::on(
							stana::ui::Event::focus,
							[&](stana::document::FocusEvent &event) -> void {
								note("focus", event);
								event.preventDefault();
							}
						),
						stana::ui::on(
							stana::ui::Event::blur,
							[&](stana::document::FocusEvent &event) -> void {
								note("blur", event);
							},
							stana::ui::Phase::capture
						),
						stana::ui::on(
							stana::ui::Event::blur,
							[&](stana::document::FocusEvent &event) -> void {
								note("blur", event);
							}
						)},
				.children =
					stana::ui::
						children(
							stana::ui::Button{
								.text = "First",
								.lifecycle =
									{.onMount =
											[&](const stana::document::NodeHandle &handle) -> void {
										firstId = handle.id();
									}},
								.events =
									{stana::ui::on(stana::ui::Event::keyDown, onKey),
										stana::ui::on(stana::ui::Event::keyUp, onRelease),
										stana::ui::on(
											stana::ui::Event::focus,
											[&](stana::document::FocusEvent &event) -> void {
												note("focus", event);
											}
										),
										stana::ui::on(
											stana::ui::Event::blur,
											[&](stana::document::FocusEvent &event) -> void {
												note("blur", event);
											}
										),
										stana::ui::on(
											stana::ui::Event::click,
											[&]() -> void {
												++clicks;
											}
										)}
							},
							stana::ui::Button{
								.text = "Second",
								.lifecycle =
									{
										.onMount = [&](
													   const stana::document::NodeHandle &handle
												   ) -> void {
											secondId = handle.id();
										}
									},
								.events = {
									stana::ui::on(stana::ui::Event::keyDown, onKey),
									stana::ui::on(stana::ui::Event::keyUp, onRelease),
									stana::ui::on(
										stana::ui::Event::focus,
										[&](stana::document::FocusEvent &event) -> void {
											note("focus", event);
										}
									),
									stana::ui::on(
										stana::ui::Event::blur,
										[&](stana::document::FocusEvent &event) -> void {
											note("blur", event);
										}
									),
									stana::ui::on(stana::ui::Event::click, [&]() -> void {
										++clicks;
									})
								}
							}
						)
			}
		);
	host.keyDown(stana::document::Key::tab);
	requireOrder(
		trace,
		{"focus capture first stack", "focus bubble first first", "focus bubble first stack"},
		"the first focus has no blur"
	);
	require(findText(host, "First").isFocused, "prevented focus notification stays published");
	trace.clear();
	host.keyDown(stana::document::Key::tab);
	requireOrder(
		trace,
		{"blur capture first stack",
			"blur bubble first first",
			"blur bubble first stack",
			"focus capture second stack",
			"focus bubble second second",
			"focus bubble second stack"},
		"blur then focus"
	);
	require(findText(host, "Second").isFocused, "tab moves to the next control");
	trace.clear();
	host.keyDown(stana::document::Key::tab, true);
	require(sawShiftTab, "shift-tab delivers the backwards flag");
	require(findText(host, "First").isFocused, "shift-tab moves to the previous control");
	requireOrder(
		trace,
		{"blur capture second stack",
			"blur bubble second second",
			"blur bubble second stack",
			"focus capture first stack",
			"focus bubble first first",
			"focus bubble first stack"},
		"reverse focus order"
	);
	blockEnter = true;
	host.keyDown(stana::document::Key::enter);
	require(clicks == 0 && findText(host, "First").isFocused, "prevented return does not click");
	blockEnter = false;
	host.keyDown(stana::document::Key::space);
	require(clicks == 1, "space activates the focused control");
	host.keyUp(stana::document::Key::space);
	require(clicks == 1 && releases == 1, "key release has no activation default");
	blockTab = true;
	host.keyDown(stana::document::Key::tab);
	require(findText(host, "First").isFocused, "prevented tab keeps focus");
}

/// @brief Disabled and inert subtrees are not hit targets. An eligible ancestor can still
/// receive the press when the point lies inside it.
void disabledAndInertSkipTargets() {
	int disabledPresses = 0;
	int parentPresses = 0;
	int inertBoxPresses = 0;
	int inertButtonPresses = 0;
	stana::NodeId disabledId{};
	stana::testing::TestHost host;
	host.mount(
		stana::ui::VStack{
			.events = {stana::ui::on(
				stana::ui::Event::pointerDown,
				[&](stana::document::PointerEvent &) -> void {
					++parentPresses;
				}
			)},
			.children = stana::ui::children(
				stana::ui::Button{
					.text = "Disabled",
					.interaction = {.isEnabled = false},
					.lifecycle =
						{.onMount = [&](const stana::document::NodeHandle &handle) -> void {
							disabledId = handle.id();
						}},
					.events = {stana::ui::on(
						stana::ui::Event::pointerDown,
						[&](stana::document::PointerEvent &) -> void {
							++disabledPresses;
						}
					)}
				},
				stana::ui::Box{
					.interaction = {.inert = true},
					.events = {stana::ui::on(
						stana::ui::Event::pointerDown,
						[&](stana::document::PointerEvent &) -> void {
							++inertBoxPresses;
						}
					)},
					.children = stana::ui::children(
						stana::ui::Button{
							.text = "Inside",
							.events = {stana::ui::on(
								stana::ui::Event::pointerDown,
								[&](stana::document::PointerEvent &) -> void {
									++inertButtonPresses;
								}
							)}
						}
					)
				}
			)
		}
	);
	host.pointerDown(centerOf(findText(host, "Disabled").bounds));
	require(disabledPresses == 0 && parentPresses == 1, "disabled child defers to its parent");
	require(!findText(host, "Disabled").isEnabled, "disabled snapshot is ineligible");
	const auto rejected = host.activate(disabledId);
	require(
		!rejected && rejected.error() == stana::Error::notFocusable,
		"disabled activation is rejected"
	);
	const int pressesBeforeInert = parentPresses;
	host.pointerDown(centerOf(findText(host, "Inside").bounds));
	require(
		inertBoxPresses == 0 && inertButtonPresses == 0,
		"an inert subtree receives no pointer events"
	);
	require(!findText(host, "Inside").isFocused, "an inert control is not focused");
	require(parentPresses > pressesBeforeInert, "the eligible ancestor remains the target");
}

/// @brief A copied handler can dispose its occurrence. Later handlers on that occurrence are
/// skipped, ancestors already on the route still run, and the occurrence releases the handler.
void disposalDuringDispatchReleasesHandlers() {
	int first = 0;
	int second = 0;
	int parentPresses = 0;
	constexpr double rowSide = 48;
	const auto token = std::make_shared<int>(0);
	const std::weak_ptr<int> weak = token;
	const stana::reactive::Signal<std::vector<int>> items{std::vector<int>{1}};
	stana::NodeId boxId{};
	stana::testing::TestHost host;
	host.mount(
		stana::ui::VStack{
			.events = {stana::ui::on(
				stana::ui::Event::pointerDown,
				[&](stana::document::PointerEvent &) -> void {
					++parentPresses;
				}
			)},
			.children = stana::ui::children(
				stana::ui::ForEach{
					.items = items,
					.key = [](int value) -> int {
						return value;
					},
					.content = [&](int) -> auto {
						const auto held = weak.lock();
						require(held != nullptr, "row token");
						return stana::ui::Box{
							.layout = {.frame = square(rowSide)},
							.lifecycle =
								{.onMount = [&](const stana::document::NodeHandle &handle) -> void {
									boxId = handle.id();
								}},
							.events =
								{stana::ui::on(
									 stana::ui::Event::pointerDown,
									 [held, items, &first](stana::document::PointerEvent &)
										 -> void {
										 ++first;
										 require(
											 held.use_count() == 3,
											 "dispatch copies the handler"
										 );
										 items.set(std::vector<int>{});
										 require(
											 held.use_count() == 2,
											 "the occurrence released its handler during dispatch"
										 );
									 }
								 ),
									stana::ui::on(
										stana::ui::Event::pointerDown,
										[&second](stana::document::PointerEvent &) -> void {
											++second;
										}
									)},
							.children = stana::ui::children(stana::ui::Text{.text = "gone"})
						};
					}
				}
			)
		}
	);
	require(token.use_count() == 2, "the mounted occurrence retains the handler");
	require(host.handle(boxId).isAlive(), "row mounted");
	const auto point = centerOf(findText(host, "gone").bounds);
	host.pointerDown(point);
	require(first == 1 && second == 0, "disposal skips the rest of that occurrence");
	require(parentPresses == 1, "ancestors already on the route still run");
	require(!host.handle(boxId).isAlive(), "disposed row");
	require(token.use_count() == 1, "disposal releases the handler");
	host.pointerDown(point);
	require(first == 1 && second == 0, "a removed occurrence does not receive a later press");
}

/// @brief Enter and leave fire only on occurrences whose containment changed, capture before
/// bubble on that occurrence. Moving between children does not re-enter their parent.
void pointerCrossingStaysOnTheBoundary() {
	constexpr double side = 40;
	std::vector<std::string> topTrace;
	std::vector<std::string> leaves;
	int stackEnters = 0;
	int stackLeaves = 0;
	stana::Point topCenter{};
	stana::Point bottomCenter{};
	stana::NodeId topId{};
	stana::NodeId bottomId{};
	stana::NodeId stackId{};
	stana::testing::TestHost host;
	host.mount(
		stana::ui::VStack{
			.lifecycle = {.onMount = [&](const stana::document::NodeHandle &handle) -> void {
				stackId = handle.id();
			}},
			.events =
				{stana::ui::on(
					 stana::ui::Event::pointerEnter,
					 [&](stana::document::PointerEvent &event) -> void {
						 ++stackEnters;
						 require(
							 event.target == stackId && event.current == stackId,
							 "parent enter does not retarget to the child"
						 );
					 }
				 ),
					stana::ui::on(
						stana::ui::Event::pointerLeave,
						[&](stana::document::PointerEvent &event) -> void {
							++stackLeaves;
							leaves.emplace_back("stack");
							requirePoint(event.position, bottomCenter, "parent leave position");
						}
					)},
			.children =
				stana::ui::children(
					stana::ui::Box{
						.layout = {.frame = square(side)},
						.lifecycle =
							{.onMount = [&](const stana::document::NodeHandle &handle) -> void {
								topId = handle.id();
							}},
						.events =
							{stana::ui::on(
								 stana::ui::Event::pointerEnter,
								 [&](stana::document::PointerEvent &event) -> void {
									 topTrace.emplace_back("bubble");
									 require(
										 event.phase == stana::ui::Phase::bubble &&
											 event.target == topId && event.current == topId,
										 "enter bubble stays on the crossed box"
									 );
									 requirePoint(event.position, topCenter, "enter position");
								 }
							 ),
								stana::ui::on(
									stana::ui::Event::pointerEnter,
									[&](stana::document::PointerEvent &event) -> void {
										topTrace.emplace_back("capture");
										require(
											event.phase == stana::ui::Phase::capture,
											"enter capture phase"
										);
									},
									stana::ui::Phase::capture
								),
								stana::ui::on(
									stana::ui::Event::pointerMove,
									[&](stana::document::PointerEvent &event) -> void {
										topTrace.emplace_back("move");
										require(
											event.target == topId && event.current == topId,
											"move target is the hit box"
										);
										requirePoint(event.position, topCenter, "move position");
									}
								),
								stana::ui::on(
									stana::ui::Event::pointerLeave,
									[&](stana::document::PointerEvent &event) -> void {
										leaves.emplace_back("top");
										requirePoint(
											event.position,
											bottomCenter,
											"leave position"
										);
									}
								)},
						.children = stana::ui::children(stana::ui::Empty{})
					},
					stana::ui::Box{
						.layout = {.frame = square(side)},
						.lifecycle =
							{
								.onMount = [&](const stana::document::NodeHandle &handle) -> void {
									bottomId = handle.id();
								}
							},
						.events =
							{stana::ui::on(
								 stana::ui::Event::pointerEnter,
								 [&](stana::document::PointerEvent &event) -> void {
									 require(
										 event.target == bottomId && event.current == bottomId,
										 "bottom enter"
									 );
								 }
							 ),
								stana::ui::on(
									stana::ui::Event::pointerLeave,
									[&](stana::document::PointerEvent &) -> void {
										leaves.emplace_back("bottom");
									}
								)},
						.children = stana::ui::children(stana::ui::Empty{})
					}
				)
		}
	);
	const auto boxes = host.nodes();
	stana::document::NodeSnapshot top{};
	stana::document::NodeSnapshot bottom{};
	for (const auto &node : boxes) {
		if (node.id == topId) {
			top = node;
		} else if (node.id == bottomId) {
			bottom = node;
		}
	}
	require(top.id == topId && bottom.id == bottomId, "crossed boxes are mounted");
	topCenter = centerOf(top.bounds);
	bottomCenter = centerOf(bottom.bounds);
	host.pointerMove(topCenter);
	requireOrder(topTrace, {"capture", "bubble", "move"}, "capture runs before bubble");
	require(stackEnters == 1, "the parent enters once with the pointer");
	host.pointerMove(bottomCenter);
	require(stackEnters == 1, "moving between children does not re-enter the parent");
	requireOrder(leaves, {"top"}, "the previous child leaves");
	host.pointerLeave();
	require(stackLeaves == 1, "leaving the window leaves the parent");
	requireOrder(leaves, {"top", "bottom", "stack"}, "leave walks deepest first");
}

/// @brief Controls, text, containers, canvases, symbols, and custom components share .events.
void visualComponentsShareTheRegistry() {
	constexpr double tile = 36;
	int stackPresses = 0;
	int textPresses = 0;
	int buttonPresses = 0;
	int boxPresses = 0;
	int cpuPresses = 0;
	int gpuPresses = 0;
	int symbolPresses = 0;
	int panelPresses = 0;
	int rowPresses = 0;
	int overlayPresses = 0;
	stana::testing::TestHost host;
	host.mount(
		stana::ui::VStack{
			.events = {stana::ui::on(
				stana::ui::Event::pointerDown,
				[&](stana::document::PointerEvent &) -> void {
					++stackPresses;
				}
			)},
			.children = stana::ui::children(
				stana::ui::Text{
					.text = "label",
					.events = {stana::ui::on(
						stana::ui::Event::pointerDown,
						[&](stana::document::PointerEvent &) -> void {
							++textPresses;
						}
					)}
				},
				stana::ui::Button{
					.text = "Go",
					.events = {stana::ui::on(
						stana::ui::Event::pointerDown,
						[&](stana::document::PointerEvent &) -> void {
							++buttonPresses;
						}
					)}
				},
				stana::ui::Box{
					.layout = {.frame = square(tile)},
					.events = {stana::ui::on(
						stana::ui::Event::pointerDown,
						[&](stana::document::PointerEvent &) -> void {
							++boxPresses;
						}
					)},
					.children = stana::ui::children(stana::ui::Empty{})
				},
				stana::ui::CPUCanvas{
					.drawing = stana::rendering::Drawing{},
					.layout = {.frame = square(tile)},
					.events = {stana::ui::on(
						stana::ui::Event::pointerDown,
						[&](stana::document::PointerEvent &) -> void {
							++cpuPresses;
						}
					)}
				},
				stana::ui::GPUCanvas{
					.drawing = stana::rendering::GPUDrawing{},
					.layout = {.frame = square(tile)},
					.events = {stana::ui::on(
						stana::ui::Event::pointerDown,
						[&](stana::document::PointerEvent &) -> void {
							++gpuPresses;
						}
					)}
				},
				stana::symbol::Symbol{
					.asset = mark(),
					.events = {stana::ui::on(
						stana::ui::Event::pointerDown,
						[&](stana::document::PointerEvent &) -> void {
							++symbolPresses;
						}
					)}
				},
				Panel{
					.events = {stana::ui::on(
						stana::ui::Event::pointerDown,
						[&](stana::document::PointerEvent &event) -> void {
							++panelPresses;
							require(
								event.target == findText(host, "panel").id &&
									event.current != event.target &&
									event.phase == stana::ui::Phase::bubble,
								"custom events attach to the wrapper and bubble from render()"
							);
						}
					)}
				},
				stana::ui::HStack{
					.layout = {.frame = square(tile)},
					.events = {stana::ui::on(
						stana::ui::Event::pointerDown,
						[&](stana::document::PointerEvent &) -> void {
							++rowPresses;
						}
					)},
					.children = stana::ui::children(stana::ui::Empty{})
				},
				stana::ui::ZStack{
					.layout = {.frame = square(tile)},
					.events = {stana::ui::on(
						stana::ui::Event::pointerDown,
						[&](stana::document::PointerEvent &) -> void {
							++overlayPresses;
						}
					)},
					.children = stana::ui::children(stana::ui::Empty{})
				}
			)
		}
	);
	int issued = 0;
	const auto tap = [&](const stana::document::NodeSnapshot &node) -> void {
		host.pointerDown(centerOf(node.bounds));
		++issued;
	};
	tap(findText(host, "label"));
	tap(findText(host, "Go"));
	tap(findKind(host, "box"));
	tap(findKind(host, "cpuCanvas"));
	tap(findKind(host, "gpuCanvas"));
	tap(findKind(host, "symbol"));
	tap(findText(host, "panel"));
	tap(findKind(host, "hstack"));
	tap(findKind(host, "zstack"));
	require(textPresses == 1, "text events");
	require(buttonPresses == 1, "button events");
	require(boxPresses == 1, "box events");
	require(cpuPresses == 1, "cpu canvas events");
	require(gpuPresses == 1, "gpu canvas events");
	require(symbolPresses == 1, "symbol events");
	require(panelPresses == 1, "custom component events");
	require(rowPresses == 1, "row events");
	require(overlayPresses == 1, "overlay events");
	require(stackPresses == issued, "the container observes every descendant press");
}
} // namespace

auto main() -> int {
	try {
		clickRoutesThroughEveryActivation();
		pointerPressPreventionSkipsFocus();
		pointerReleasePreventionStillActivates();
		toggleDefaultFollowsClickPrevention();
		scrollPreventionAndZeroDelta();
		keyboardFocusAndDefaults();
		disabledAndInertSkipTargets();
		disposalDuringDispatchReleasesHandlers();
		pointerCrossingStaysOnTheBoundary();
		visualComponentsShareTheRegistry();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
