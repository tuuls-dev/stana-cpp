module;

#include <concepts>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

export module stana:ui;
export import :primitives;
export import :reactive;
export import :tasks;
export import :rendering;
import :document;
export import :layout;
export import :symbol_asset;
export import :symbol_playback;

export namespace stana::ui {

/// @brief Shared typography and foreground overrides for text-bearing controls.
///
/// Text, Button and Toggle accept this as .textStyle. Font updates affect intrinsic
/// measurement; foreground changes affect paint. Omitting a field preserves the
/// receiving control's default, rather than resetting it to a universal style.
struct TextProperties {
	/// @brief Optional reactive system font; changes invalidate intrinsic text measurement.
	std::optional<::stana::reactive::Property<Font>> font{};
	/// @brief Optional reactive text color; omission keeps the receiving control default.
	std::optional<::stana::reactive::Property<Color>> foreground{};
};

/// @brief Shared retained paint and visibility configuration for visual components.
///
/// Use .appearance to set reactive colors, border geometry, opacity or visibility.
/// Updates mutate the existing node. clip confines descendant paint; hidden keeps
/// the layout footprint, while collapsed removes the footprint without disposing
/// its mounted scope. Use If when the branch and its resources should be removed.
struct AppearanceProperties {
	/// @brief Optional reactive fill behind this node and its children.
	std::optional<::stana::reactive::Property<Color>> background{};
	/// @brief Optional reactive subtree opacity in [0, 1]; zero opacity does not dispose state.
	std::optional<::stana::reactive::Property<double>> opacity{};
	/// @brief Optional reactive outline color used when borderWidth is positive.
	std::optional<::stana::reactive::Property<Color>> borderColor{};
	/// @brief Optional reactive outline thickness in finite, nonnegative logical points.
	std::optional<::stana::reactive::Property<double>> borderWidth{};
	/// @brief Optional reactive rounding radius used by background and clip geometry.
	std::optional<::stana::reactive::Property<double>> cornerRadius{};
	/// @brief Independent elliptical radii; when supplied, take precedence over cornerRadius.
	std::optional<::stana::reactive::Property<CornerRadii>> cornerRadii{};
	/// @brief Inward per-edge widths; when supplied, take precedence over borderWidth.
	/// Rich geometry uses an outer border contour and an inner padding contour for all paint.
	std::optional<::stana::reactive::Property<BorderWidths>> borderWidths{};
	/// @brief Paint-only ordered outer/inset shadows; an empty list removes all shadows.
	/// Missing colors follow this node's effective foreground. Shadows are not inherited.
	std::optional<::stana::reactive::Property<std::vector<::stana::rendering::BoxShadow>>>
		boxShadows{};
	/// @brief Optional reactive layout/paint participation; does not remove mounted resources.
	std::optional<::stana::reactive::Property<Visibility>> visibility{};
	/// @brief Whether this node clips descendant paint to its rounded outer bounds.
	std::optional<bool> clip{};
};

/// @brief Shared interaction policy and pointer-down handlers.
///
/// Visual components accept .interaction; ancestor disabled/inert state also
/// blocks descendants. Capture handlers run from root toward the target before
/// bubble handlers run back outward. Handlers may call event.preventDefault() to
/// prevent default press/focus behavior. These callbacks run on the UI executor.
struct InteractionProperties {
	/// @brief Reactive local enabled policy; disabled ancestors still block interaction.
	::stana::reactive::Property<bool> isEnabled = true;
	/// @brief Blocks this subtree from input and focus without changing its appearance or lifetime.
	bool inert = false;
	/// @brief Bubble-phase handler, invoked after targetward capture handlers on the UI executor.
	std::function<void(::stana::document::PointerEvent &)> onPointerDown{};
	/// @brief Capture-phase handler, invoked root-to-target before pointer-down bubbling.
	std::function<void(::stana::document::PointerEvent &)> onPointerDownCapture{};
};

/// @brief Application-authored semantic naming for a visual occurrence.
///
/// Supply .accessibility = {.label = "..."} when the visible content alone does not
/// provide the intended accessible name. Mounting carries this explicit value into
/// published node snapshots; diagnostic names do not become accessible labels.
struct AccessibilityProperties {
	/// @brief Explicit accessible name. Omit for no override; an empty string is an explicit name.
	std::optional<std::string> label{};
};

/// @brief Callbacks and diagnostic metadata for one mounted occurrence.
///
/// onMount runs after the mount batch publishes, with a non-owning NodeHandle;
/// onDispose runs when the occurrence's scope is disposed, not on every signal
/// update. Do not capture references to temporary component descriptions. Capture
/// shared state handles by value when callbacks need state after mounting.
struct LifecycleProperties {
	/// @brief Application-authored debug name for this occurrence; not an accessibility label.
	std::string diagnosticName{};
	/// @brief Post-publication callback with a non-owning handle; skipped if mounting was disposed.
	std::function<void(::stana::document::NodeHandle)> onMount{};
	/// @brief Cleanup invoked when the mounted scope is removed or its document closes.
	std::function<void()> onDispose{};
};

/// @brief Narrow mounting capability. Native objects and mutable retained records are not
/// part of the application-facing custom-component contract.
struct MountContext {
	std::shared_ptr<::stana::document::detail::Document> document;
	NodeId parent;
};

/// @brief One display-link sample delivered to ComponentContext::onFrame.
struct FrameTime {
	double timestamp = 0;		///< Host time of this sample, in seconds.
	double targetTimestamp = 0; ///< Expected time of the next sample, in seconds.
	double elapsed = 0; ///< Seconds since the previous delivered sample. Zero after a reset.
};

/// @brief Mount-local state and external resources live as long as this occurrence, not
/// as long as its temporary component description.
class ComponentContext {
  public:
	ComponentContext(
		::stana::reactive::Scope scope,
		Environment environment,
		std::weak_ptr<::stana::document::detail::Document> document = {}
	)
		: scope_(std::move(scope)),
		  environment_(environment),
		  document_(std::move(document)) {}

	template <typename T>
	[[nodiscard]] auto state(T initial) const -> ::stana::reactive::Signal<T> {
		return ::stana::reactive::Signal<T>{std::move(initial)};
	}

	void own(::stana::reactive::Subscription subscription) const {
		static_cast<void>(scope_.own(std::move(subscription)));
	}

	template <typename F> void onDispose(F cleanup) const {
		static_cast<void>(scope_.own(std::move(cleanup)));
	}

	/// @brief Starts a scope-owned coroutine after publication, with explicit error handling.
	/// Pass a named Task-producing function and capture state in its value parameters.
	void
	task(::stana::tasks::Task operation, std::function<void(std::exception_ptr)> onError) const {
		std::move(operation).start(scope_, std::move(onError));
	}

	[[nodiscard]] auto beginRequest() const -> ::stana::reactive::Scope::RequestToken {
		return scope_.beginRequest();
	}

	[[nodiscard]] auto isCurrent(::stana::reactive::Scope::RequestToken token) const -> bool {
		return scope_.isCurrent(token);
	}

	[[nodiscard]] auto environment() const noexcept -> Environment { return environment_; }

	/// @brief Registers a display-link callback owned by this occurrence.
	/// The scope keeps the registration alive when the returned subscription is discarded.
	/// The first elapsed sample after mount or after isActive becomes true is zero.
	template <typename F>
	[[nodiscard]] auto onFrame(F onSample, ::stana::reactive::Property<bool> isActive = true) const
		-> ::stana::reactive::Subscription {
		const auto document = document_.lock();
		if (!document) {
			return {};
		}
		const auto active = std::make_shared<bool>(isActive.get());
		const auto registration = std::make_shared<std::uint64_t>(0);
		const std::weak_ptr<::stana::document::detail::Document> weak = document_;
		*registration = document->addFrameRegistration(
			[weak, active, scope = scope_]() -> bool {
				return weak.lock() && scope.alive() && *active;
			},
			[notify = std::move(
				 onSample
			 )](::stana::document::detail::Document::FrameStamp stamp, double elapsed) -> void {
				notify(
					FrameTime{
						.timestamp = stamp.timestamp,
						.targetTimestamp = stamp.targetTimestamp,
						.elapsed = elapsed
					}
				);
			}
		);
		auto observed = ::stana::reactive::observe(
			std::move(isActive),
			[active, weak, registration](bool value) -> void {
				*active = value;
				if (const auto owner = weak.lock()) {
					owner->resetFrameTimestamp(*registration);
					owner->refreshFrameDemand();
				}
			}
		);
		static_cast<void>(scope_.own(std::move(observed)));
		static_cast<void>(scope_.own([weak, registration]() -> void {
			if (const auto owner = weak.lock()) {
				owner->cancelFrameRegistration(*registration);
			}
		}));
		return {};
	}

  private:
	::stana::reactive::Scope scope_;
	Environment environment_;
	std::weak_ptr<::stana::document::detail::Document> document_{};
};

/// @brief A component with no visible content, useful as an If branch default.
/// The mounting runtime still creates a structural scope; no native widget exists.
struct Empty {};

/// @brief A retained display-only label with a constant or reactive string input.
///
/// Set .text to a string, Signal<string> or Computed<string>. Passing signal.get()
/// captures a constant snapshot instead. Use .textStyle for typography and .layout
/// for sizing; the document updates the same node when its observed inputs change.
/// Text is not an editor and does not expose selection or native text-input state.
struct Text {
	/// @brief Display label supplied as a constant, Signal<string> or Computed<string>.
	::stana::reactive::Property<std::string> text{};
	/// @brief Shared box-model overrides applied to this component at mount and on reactive
	/// updates.
	::stana::layout::LayoutProperties layout{};
	/// @brief Shared typography/color overrides for this control; not inherited by arbitrary
	/// descendants.
	TextProperties textStyle{};
	/// @brief Shared fill, border, clipping, opacity and visibility configuration.
	AppearanceProperties appearance{};
	/// @brief Shared enabled/inert policy and pointer handlers for this subtree.
	InteractionProperties interaction{};
	/// @brief Explicit semantic naming for this occurrence, independent of diagnostic metadata.
	AccessibilityProperties accessibility{};
	/// @brief Mount/disposal callbacks and diagnostics owned by this mounted occurrence.
	LifecycleProperties lifecycle{};
};

/// @brief An actionable retained text control using Stana drawing, not NSButton.
///
/// Provide .text and .onClick; capture Signal handles by value in actions. Pointer
/// release inside the pressed button, keyboard activation and NodeHandle::activate
/// all use the same document action path. Disabled/inert ancestors block activation.
/// Shared groups customize layout, typography, appearance, semantics and lifetime.
struct Button {
	/// @brief Display label supplied as a constant, Signal<string> or Computed<string>.
	::stana::reactive::Property<std::string> text{};
	/// @brief Activation action shared by pointer, keyboard and programmatic activation paths.
	std::function<void()> onClick{};
	/// @brief Shared box-model overrides applied to this component at mount and on reactive
	/// updates.
	::stana::layout::LayoutProperties layout{};
	/// @brief Shared typography/color overrides for this control; not inherited by arbitrary
	/// descendants.
	TextProperties textStyle{};
	/// @brief Shared fill, border, clipping, opacity and visibility configuration.
	AppearanceProperties appearance{};
	/// @brief Shared enabled/inert policy and pointer handlers for this subtree.
	InteractionProperties interaction{};
	/// @brief Explicit semantic naming for this occurrence, independent of diagnostic metadata.
	AccessibilityProperties accessibility{};
	/// @brief Mount/disposal callbacks and diagnostics owned by this mounted occurrence.
	LifecycleProperties lifecycle{};
};

/// @brief A boolean control that reads and writes an explicit Binding<bool>.
///
/// Supply .value = signal.binding(), not a read-only Signal or Property. Activation
/// negates the binding through the document action path. External binding updates
/// keep the mounted occurrence and focus identity intact; .text supplies its label.
struct Toggle {
	/// @brief Display label supplied as a constant, Signal<string> or Computed<string>.
	::stana::reactive::Property<std::string> text{};
	/// @brief Writable model binding. Construct with Signal<bool>::binding(); read-only inputs are
	/// rejected.
	::stana::reactive::Binding<bool> value;
	/// @brief Shared box-model overrides applied to this component at mount and on reactive
	/// updates.
	::stana::layout::LayoutProperties layout{};
	/// @brief Shared typography/color overrides for this control; not inherited by arbitrary
	/// descendants.
	TextProperties textStyle{};
	/// @brief Shared fill, border, clipping, opacity and visibility configuration.
	AppearanceProperties appearance{};
	/// @brief Shared enabled/inert policy and pointer handlers for this subtree.
	InteractionProperties interaction{};
	/// @brief Explicit semantic naming for this occurrence, independent of diagnostic metadata.
	AccessibilityProperties accessibility{};
	/// @brief Mount/disposal callbacks and diagnostics owned by this mounted occurrence.
	LifecycleProperties lifecycle{};
};

/// @brief An empty layout component whose explicit frame can reserve space.
///
/// Use .layout.frame for a fixed or constrained gap. It paints no content and has
/// no default action. The parent layout, not a native spacer widget, arranges it.
struct Spacer {
	/// @brief Shared box-model overrides applied to this component at mount and on reactive
	/// updates.
	::stana::layout::LayoutProperties layout{};
};

/// @brief Decorative drawing recorded as portable commands in local logical points.
///
/// Construct Drawing, then pass it as .drawing and provide a frame through .layout.
/// Mounting copies commands into the retained node; scene projection translates
/// them to document coordinates. This is not a native drawing-context escape hatch.
/// Provide an accessible equivalent when the drawing conveys meaningful content.
struct CPUCanvas {
	/// @brief Portable local-coordinate paint commands copied into the retained CPUCanvas at mount.
	::stana::reactive::Property<::stana::rendering::Drawing> drawing{};
	/// @brief Shared box-model overrides applied to this component at mount and on reactive
	/// updates.
	::stana::layout::LayoutProperties layout{};
	/// @brief Shared fill, border, clipping, opacity and visibility configuration.
	AppearanceProperties appearance{};
	/// @brief Explicit semantic naming for this occurrence, independent of diagnostic metadata.
	InteractionProperties interaction{};
	AccessibilityProperties accessibility{};
	/// @brief Mount/disposal callbacks and diagnostics owned by this mounted occurrence.
	LifecycleProperties lifecycle{};
};

/// @brief Reactive ordered GPU drawing, cropped to its padded content output.
/// Default outer clipping is rounded; errors are delivered to this mounted occurrence.
struct GPUCanvas {
	::stana::reactive::Property<::stana::rendering::GPUDrawing> drawing{};
	::stana::layout::LayoutProperties layout{};
	AppearanceProperties appearance{};
	InteractionProperties interaction{};
	AccessibilityProperties accessibility{};
	LifecycleProperties lifecycle{};
	std::function<void(const ::stana::rendering::GPUDiagnostic &)> onError{};
};

/// @brief A statically composed column arranged in child order.
///
/// Use .children = children(...) so CTAD infers tuple storage. .spacing is reactive;
/// .alignment controls horizontal placement in the padded content box. Normal
/// property changes do not reconstruct this tuple or remount its descendants.
template <typename Children> struct VStack {
	/// @brief Reactive finite, nonnegative gap between participating children in logical points.
	::stana::reactive::Property<double> spacing = 0;
	/// @brief Logical horizontal alignment resolved against document direction during placement.
	Alignment alignment = Alignment::leading;
	/// @brief Shared box-model overrides applied to this component at mount and on reactive
	/// updates.
	::stana::layout::LayoutProperties layout{};
	/// @brief Shared fill, border, clipping, opacity and visibility configuration.
	AppearanceProperties appearance{};
	/// @brief Shared enabled/inert policy and pointer handlers for this subtree.
	InteractionProperties interaction{};
	/// @brief Explicit semantic naming for this occurrence, independent of diagnostic metadata.
	AccessibilityProperties accessibility{};
	/// @brief Mount/disposal callbacks and diagnostics owned by this mounted occurrence.
	LifecycleProperties lifecycle{};
	/// @brief Statically typed child tuple; construct with children(...) so CTAD infers its type.
	Children children;
};

/// @brief A statically composed row with reactive spacing.
///
/// Use .children = children(...). Document direction reverses the logical leading
/// edge, so RTL placement follows the same child order from right to left. Child
/// frames and shared layout properties determine the measured row dimensions.
template <typename Children> struct HStack {
	/// @brief Reactive finite, nonnegative gap between participating children in logical points.
	::stana::reactive::Property<double> spacing = 0;
	/// @brief Logical horizontal alignment resolved against document direction during placement.
	Alignment alignment = Alignment::leading;
	/// @brief Shared box-model overrides applied to this component at mount and on reactive
	/// updates.
	::stana::layout::LayoutProperties layout{};
	/// @brief Shared fill, border, clipping, opacity and visibility configuration.
	AppearanceProperties appearance{};
	/// @brief Shared enabled/inert policy and pointer handlers for this subtree.
	InteractionProperties interaction{};
	/// @brief Explicit semantic naming for this occurrence, independent of diagnostic metadata.
	AccessibilityProperties accessibility{};
	/// @brief Mount/disposal callbacks and diagnostics owned by this mounted occurrence.
	LifecycleProperties lifecycle{};
	/// @brief Statically typed child tuple; construct with children(...) so CTAD infers its type.
	Children children;
};

/// @brief An overlay container that paints children in tuple order, last on top.
///
/// Use .alignment for logical horizontal alignment; children are vertically
/// centered in the padded content rectangle. Unlike stacks, overlays do not accept
/// spacing. Put background content first and interactive foreground content last.
template <typename Children> struct ZStack {
	/// @brief Horizontal overlay alignment; children remain vertically centered.
	Alignment alignment = Alignment::center;
	/// @brief Shared box-model overrides applied to this component at mount and on reactive
	/// updates.
	::stana::layout::LayoutProperties layout{};
	/// @brief Shared fill, border, clipping, opacity and visibility configuration.
	AppearanceProperties appearance{};
	/// @brief Shared enabled/inert policy and pointer handlers for this subtree.
	InteractionProperties interaction{};
	/// @brief Explicit semantic naming for this occurrence, independent of diagnostic metadata.
	AccessibilityProperties accessibility{};
	/// @brief Mount/disposal callbacks and diagnostics owned by this mounted occurrence.
	LifecycleProperties lifecycle{};
	/// @brief Statically typed child tuple; construct with children(...) so CTAD infers its type.
	Children children;
};

/// @brief A styled containment boundary for a tuple of child descriptions.
///
/// Use shared groups to apply padding, frame, background, clipping or interaction
/// to the subtree. Children overlap rather than forming a row or column; choose
/// HStack or VStack when sequential placement is intended.
template <typename Children> struct Box {
	/// @brief Shared box-model overrides applied to this component at mount and on reactive
	/// updates.
	::stana::layout::LayoutProperties layout{};
	/// @brief Shared fill, border, clipping, opacity and visibility configuration.
	AppearanceProperties appearance{};
	/// @brief Shared enabled/inert policy and pointer handlers for this subtree.
	InteractionProperties interaction{};
	/// @brief Explicit semantic naming for this occurrence, independent of diagnostic metadata.
	AccessibilityProperties accessibility{};
	/// @brief Mount/disposal callbacks and diagnostics owned by this mounted occurrence.
	LifecycleProperties lifecycle{};
	/// @brief Statically typed child tuple; construct with children(...) so CTAD infers its type.
	Children children;
};

/// @brief A clipped vertical viewport containing one component description.
///
/// Set .content to a stack for multiple rows and constrain the viewport with
/// .layout.frame. The document owns scroll offset and clamps it to content extent.
/// All descendants mount eagerly; this is not a virtualized collection.
template <typename Content> struct ScrollView {
	/// @brief Shared box-model overrides applied to this component at mount and on reactive
	/// updates.
	::stana::layout::LayoutProperties layout{};
	/// @brief Shared fill, border, clipping, opacity and visibility configuration.
	AppearanceProperties appearance{};
	/// @brief Shared enabled/inert policy and pointer handlers for this subtree.
	InteractionProperties interaction{};
	/// @brief Explicit semantic naming for this occurrence, independent of diagnostic metadata.
	AccessibilityProperties accessibility{};
	/// @brief Mount/disposal callbacks and diagnostics owned by this mounted occurrence.
	LifecycleProperties lifecycle{};
	/// @brief Content description or row factory, depending on the containing component contract.
	Content content;
};

/// @brief A reactive structural branch with independent mounted lifetimes.
///
/// Set .condition to a boolean source and provide .thenContent/.elseContent.
/// Changing the condition disposes the active branch and mounts the other; ordinary
/// parent components do not rebuild. Hoist state outside the branch if it must
/// survive removal. Omitting elseContent uses Empty.
template <typename Then, typename Else = Empty> struct If {
	/// @brief Reactive branch selector; changing it disposes the previous branch scope.
	::stana::reactive::Property<bool> condition = false;
	/// @brief Description mounted when condition is true; its local state ends on branch removal.
	Then thenContent;
	/// @brief Description mounted when condition is false; defaults to an empty branch.
	Else elseContent{};
};

/// @brief A keyed dynamic region whose factory has one statically known return type.
///
/// .items is a vector or reactive vector source, .key maps an item to a stable key,
/// and .content builds its row description. Moves preserve row IDs/scopes; unequal
/// value items replace descendants. Shared-pointer rows require the same instance
/// for an existing key. Duplicate keys or changed reference identities reject the
/// update before tree mutation and are reported through the host error collection.
template <typename Items, typename Key, typename Content> struct ForEach {
	/// @brief Value vector or reactive vector source observed by the keyed region.
	Items items;
	/// @brief Pure item-to-key function; keys must be stable, hashable and unique within an update.
	Key key;
	/// @brief Content description or row factory, depending on the containing component contract.
	Content content;
};

namespace detail {
/// @brief Resolved-value no-op suppression, including ordered floating comparisons.
template <typename T>
[[nodiscard]] auto
assignRetained(T &target, const T &value, ::stana::document::detail::Dirty domains)
	-> ::stana::document::detail::Dirty {
	if (std::is_eq(target <=> value)) {
		return ::stana::document::detail::Dirty::none;
	}
	target = value;
	return domains;
}

void applyLayout(
	const ::stana::layout::LayoutProperties &properties,
	NodeId identity,
	MountContext &context
);
void applyTextStyle(const TextProperties &properties, NodeId identity, MountContext &context);
void applyAppearance(
	const AppearanceProperties &properties,
	NodeId identity,
	MountContext &context
);
void applyInteraction(
	const InteractionProperties &properties,
	NodeId identity,
	MountContext &context
);
void applyAccessibility(
	const AccessibilityProperties &properties,
	NodeId identity,
	MountContext &context
);
void applyLifecycle(const LifecycleProperties &properties, NodeId identity, MountContext &context);

template <typename Description>
void configure(const Description &description, NodeId identity, MountContext &context) {
	if constexpr (requires { description.layout; }) {
		applyLayout(description.layout, identity, context);
	}
	if constexpr (requires { description.textStyle; }) {
		applyTextStyle(description.textStyle, identity, context);
	}
	if constexpr (requires { description.appearance; }) {
		applyAppearance(description.appearance, identity, context);
	}
	if constexpr (requires { description.interaction; }) {
		applyInteraction(description.interaction, identity, context);
	}
	if constexpr (requires { description.accessibility; }) {
		applyAccessibility(description.accessibility, identity, context);
	}
	if constexpr (requires { description.lifecycle; }) {
		applyLifecycle(description.lifecycle, identity, context);
	}
}

template <typename Description>
[[nodiscard]] auto createConfigured(
	const Description &description,
	::stana::document::detail::NodeKind kind,
	MountContext &context
) -> NodeId {
	const auto identity = context.document->createNode(kind, context.parent);
	configure(description, identity, context);
	return identity;
}
} // namespace detail

[[nodiscard]] auto mount(const Empty &, MountContext &context) -> ::stana::document::NodeHandle;
[[nodiscard]] auto mount(const Text &description, MountContext &context)
	-> ::stana::document::NodeHandle;
[[nodiscard]] auto mount(const Button &description, MountContext &context)
	-> ::stana::document::NodeHandle;
[[nodiscard]] auto mount(const Toggle &description, MountContext &context)
	-> ::stana::document::NodeHandle;
[[nodiscard]] auto mount(const Spacer &description, MountContext &context)
	-> ::stana::document::NodeHandle;
[[nodiscard]] auto mount(const CPUCanvas &description, MountContext &context)
	-> ::stana::document::NodeHandle;
[[nodiscard]] auto mount(const GPUCanvas &description, MountContext &context)
	-> ::stana::document::NodeHandle;

template <typename T>
	requires requires(const T &description, ComponentContext &context) {
		description.render(context);
	}
[[nodiscard]] auto mount(const T &description, MountContext &context)
	-> ::stana::document::NodeHandle {
	const auto identity =
		context.document->createNode(::stana::document::detail::NodeKind::group, context.parent);
	auto *node = context.document->find(identity);
	ComponentContext componentContext{
		node->scope,
		context.document->environment(),
		context.document
	};
	auto content = description.render(componentContext);
	MountContext childContext{.document = context.document, .parent = identity};
	static_cast<void>(mount(content, childContext));
	return context.document->handle(identity);
}

namespace detail {
template <typename Description>
[[nodiscard]] auto mountContainer(
	const Description &description,
	::stana::document::detail::NodeKind kind,
	MountContext &context
) -> ::stana::document::NodeHandle {
	const auto identity = createConfigured(description, kind, context);
	if constexpr (requires { description.spacing; }) {
		context.document->bind(
			identity,
			description.spacing,
			[](::stana::document::detail::Node &node,
				double value) -> ::stana::document::detail::Dirty {
				::stana::detail::requireNonnegative(value);
				return assignRetained(
					node.spacing,
					value,
					::stana::document::detail::Dirty::measure |
						::stana::document::detail::Dirty::arrange |
						::stana::document::detail::Dirty::snapshot
				);
			}
		);
	}
	if constexpr (requires { description.alignment; }) {
		context.document->find(identity)->alignment = description.alignment;
	}
	MountContext childContext{.document = context.document, .parent = identity};
	std::apply(
		[&](const auto &...child) -> void {
			(static_cast<void>(mount(child, childContext)), ...);
		},
		description.children
	);
	return context.document->handle(identity);
}
} // namespace detail

template <typename Children>
[[nodiscard]] auto mount(const VStack<Children> &description, MountContext &context)
	-> ::stana::document::NodeHandle {
	return ::stana::ui::detail::mountContainer(
		description,
		::stana::document::detail::NodeKind::vstack,
		context
	);
}

template <typename Children>
[[nodiscard]] auto mount(const HStack<Children> &description, MountContext &context)
	-> ::stana::document::NodeHandle {
	return ::stana::ui::detail::mountContainer(
		description,
		::stana::document::detail::NodeKind::hstack,
		context
	);
}

template <typename Children>
[[nodiscard]] auto mount(const ZStack<Children> &description, MountContext &context)
	-> ::stana::document::NodeHandle {
	return ::stana::ui::detail::mountContainer(
		description,
		::stana::document::detail::NodeKind::zstack,
		context
	);
}

template <typename Children>
[[nodiscard]] auto mount(const Box<Children> &description, MountContext &context)
	-> ::stana::document::NodeHandle {
	return ::stana::ui::detail::mountContainer(
		description,
		::stana::document::detail::NodeKind::box,
		context
	);
}

template <typename Content>
[[nodiscard]] auto mount(const ScrollView<Content> &description, MountContext &context)
	-> ::stana::document::NodeHandle {
	const auto identity = ::stana::ui::detail::createConfigured(
		description,
		::stana::document::detail::NodeKind::scroll,
		context
	);
	MountContext childContext{.document = context.document, .parent = identity};
	static_cast<void>(mount(description.content, childContext));
	return context.document->handle(identity);
}

template <typename Then, typename Else>
[[nodiscard]] auto mount(const If<Then, Else> &description, MountContext &context)
	-> ::stana::document::NodeHandle {
	const auto identity =
		context.document->createNode(::stana::document::detail::NodeKind::group, context.parent);
	const auto weakDocument = context.document->weak_from_this();
	// Branch descriptions belong to this explicit dynamic boundary only. Ordinary
	// ancestor and leaf descriptions are never retained or rebuilt by this binding.
	auto subscription = ::stana::reactive::observe(
		description.condition,
		[weakDocument,
			identity,
			whenTrue = description.thenContent,
			whenFalse = description.elseContent](bool condition) -> void {
			if (const auto document = weakDocument.lock();
				document && document->find(identity) != nullptr) {
				document->removeChildren(identity);
				if (const auto *region = document->find(identity);
					region == nullptr || !region->scope.alive()) {
					return;
				}
				MountContext branchContext{.document = document, .parent = identity};
				if (condition) {
					static_cast<void>(mount(whenTrue, branchContext));
				} else {
					static_cast<void>(mount(whenFalse, branchContext));
				}
			}
		},
		true
	);
	static_cast<void>(context.document->find(identity)->scope.own(std::move(subscription)));
	return context.document->handle(identity);
}

namespace detail {
template <typename Source> [[nodiscard]] auto readItems(const Source &source) {
	if constexpr (requires { source.get(); }) {
		return source.get();
	} else {
		return source;
	}
}
} // namespace detail

template <typename Items, typename Key, typename Content>
[[nodiscard]] auto mount(const ForEach<Items, Key, Content> &description, MountContext &context)
	-> ::stana::document::NodeHandle {
	using Values = decltype(::stana::ui::detail::readItems(description.items));
	using Item = typename Values::value_type;
	using RowKey = std::remove_cvref_t<std::invoke_result_t<Key, const Item &>>;

	/// @brief Previous item and stable region identity for one keyed row occurrence.
	struct Row {
		Item item;		 ///< Previous item value, compared before deciding to replace descendants.
		NodeId identity; ///< Stable row region, independent of its current position in the list.
	};

	/// @brief Mount-owned key index used to validate and reconcile this ForEach region.
	struct Rows {
		std::unordered_map<RowKey, Row> rows;
	};

	const auto identity =
		context.document->createNode(::stana::document::detail::NodeKind::group, context.parent);
	const auto state = std::make_shared<Rows>();
	const auto source = ::stana::reactive::computed([items = description.items] -> Values {
		return ::stana::ui::detail::readItems(items);
	});
	const auto weakDocument = context.document->weak_from_this();
	auto subscription = ::stana::reactive::observe(
		::stana::reactive::Property<Values>{source},
		[weakDocument, identity, state, key = description.key, content = description.content](
			const Values &items
		) -> void {
			const auto document = weakDocument.lock();
			if (!document || document->find(identity) == nullptr) {
				return;
			}
			std::unordered_set<RowKey> keys;
			for (const auto &item : items) {
				const auto rowKey = key(item);
				if (!keys.insert(rowKey).second) {
					document->report(Error::duplicateKey);
					return;
				}
				if constexpr (requires {
								  typename Item::element_type;
								  item.get();
							  }) {
					if (const auto existing = state->rows.find(rowKey);
						existing != state->rows.end() && existing->second.item != item) {
						document->report(Error::rowIdentityChanged);
						return;
					}
				}
			}
			// Reject duplicate keys and changed reference instances before touching
			// any mounted row. Valid moves preserve row scopes and descendant IDs.
			std::erase_if(state->rows, [&](const auto &entry) -> bool {
				if (!keys.contains(entry.first)) {
					document->remove(entry.second.identity);
					return true;
				}
				return false;
			});
			std::vector<NodeId> order;
			for (const auto &item : items) {
				const auto rowKey = key(item);
				auto existing = state->rows.find(rowKey);
				if (existing != state->rows.end() && existing->second.item == item) {
					order.push_back(existing->second.identity);
					continue;
				}
				const auto rowId = existing == state->rows.end()
					? document->createNode(::stana::document::detail::NodeKind::group, identity)
					: existing->second.identity;
				document->removeChildren(rowId);
				MountContext rowContext{.document = document, .parent = rowId};
				static_cast<void>(mount(content(item), rowContext));
				state->rows.insert_or_assign(rowKey, Row{.item = item, .identity = rowId});
				order.push_back(rowId);
			}
			if (!document->reorder(identity, order)) {
				document->report(Error::invalidStructure);
			}
		},
		true
	);
	static_cast<void>(context.document->find(identity)->scope.own(std::move(subscription)));
	return context.document->handle(identity);
}

/// @brief Checks the mounting customization, rather than requiring a marker or base class.
template <typename T>
concept Component = requires(const std::remove_cvref_t<T> &description, MountContext &context) {
	{ mount(description, context) } -> std::same_as<::stana::document::NodeHandle>;
};

/// @brief Static child storage: no per-child component allocation or virtual dispatch.
template <Component... Children> [[nodiscard]] constexpr auto children(Children &&...descriptions) {
	return std::tuple<std::decay_t<Children>...>{std::forward<Children>(descriptions)...};
}
} // namespace stana::ui

export namespace stana::symbol {
inline constexpr double defaultSize = 24;

/// @brief Retained vector artwork. The field is asset so it does not collide with this type.
/// Default size is 24. Themed text is currentColor. Clipping is off unless appearance sets it.
/// Symbols are not buttons. textScale does not change measurement.
struct Symbol {
	::stana::reactive::Property<Asset> asset{};
	::stana::reactive::Property<double> size = defaultSize;
	::stana::reactive::Property<Color> color = Color::text();
	::stana::reactive::Property<Playback> animation = Playback::none();
	::stana::reactive::Property<std::optional<double>> strokeWidth{};
	::stana::layout::LayoutProperties layout{};
	::stana::ui::AppearanceProperties appearance{};
	::stana::ui::InteractionProperties interaction{};
	::stana::ui::AccessibilityProperties accessibility{};
	::stana::ui::LifecycleProperties lifecycle{};
	std::function<void(const Diagnostic &)> onError{};
};

[[nodiscard]] auto mount(const Symbol &description, ::stana::ui::MountContext &context)
	-> ::stana::document::NodeHandle;
} // namespace stana::symbol
