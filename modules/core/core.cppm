module;

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

export module stana.core;
import stana.primitives;
import stana.reactive;
import stana.graphics;

export namespace stana {
namespace detail {
class Document;
enum class NodeKind : std::uint8_t {
	root,
	group,
	box,
	vstack,
	hstack,
	zstack,
	spacer,
	text,
	button,
	toggle,
	scroll,
	canvas
};
} // namespace detail

/// @brief An application-facing reference to an already mounted UI node.
///
/// LifecycleProperties::onMount and TestHost::handle() supply handles for issuing
/// actions without exposing the retained record or extending its lifetime. Copies
/// identify the same occurrence, even when reactive properties change. Removal or
/// document closure makes the handle stale; operations then return Error::staleHandle.
/// Use isAlive() for inspection, but still check each operation's result because
/// application callbacks can remove nodes. Access on the owning UI executor only.
class NodeHandle {
  public:
	NodeHandle() = default;

	NodeHandle(std::weak_ptr<detail::Document> document, NodeId identity)
		: document_(std::move(document)),
		  identity_(identity) {}

	[[nodiscard]] auto id() const noexcept -> NodeId { return identity_; }

	[[nodiscard]] auto isAlive() const -> bool;
	[[nodiscard]] auto activate() const -> std::expected<void, Error>;
	[[nodiscard]] auto requestFocus() const -> std::expected<void, Error>;

  private:
	std::weak_ptr<detail::Document> document_;
	NodeId identity_{};
};

/// @brief Pointer-down dispatch context shared by capture and bubble handlers.
///
/// InteractionProperties callbacks receive this mutable event. Call
/// preventDefault() to prevent Stana's default press/focus behavior while allowing
/// handlers already on the dispatch route to run. position uses document-relative,
/// top-left logical points, not physical pixels or screen coordinates.
struct PointerEvent {
	Point position;				   ///< Document-relative pointer position in logical points.
	bool defaultPrevented = false; ///< Set by preventDefault(); disables default press/focus
								   ///< behavior for this dispatch.

	void preventDefault() noexcept { defaultPrevented = true; }
};

/// @brief A read-only description of one visible node from the document's last commit.
///
/// TestHost::nodes() returns these values for assertions about rendered content,
/// layout, enabled state and focus without exposing mutable engine records. The
/// document rebuilds this projection after reactive bindings and layout settle;
/// reads during a transaction still see the previous complete commit.
///
/// A snapshot is a value, not a live view. Keep its id to compare node identity
/// across commits, then query the host again to inspect updated properties. To
/// activate or focus that node, obtain a NodeHandle; editing this value does not
/// change the UI. Native renderers consume SceneSnapshot instead of this type.
struct NodeSnapshot {
	/// @brief Identity used to compare this occurrence across commits; not a persistent key.
	NodeId id{};
	/// @brief Diagnostic node kind (for example, "text"), used to select nodes in tests.
	std::string kind{};
	/// @brief Resolved display text at this commit; not a writable binding or a live source.
	std::string text{};
	/// @brief Outer layout rectangle in document-relative logical points, including padding.
	Rect bounds{};
	/// @brief Effective interaction eligibility, including disabled or inert ancestors.
	bool isEnabled = true;
	/// @brief Whether this occurrence owns keyboard focus in the published document.
	bool isFocused = false;
	/// @brief Explicit accessible name; no value differs from an explicitly empty label.
	std::optional<std::string> accessibilityLabel{};
};

/// @brief Cumulative work counters for a single document lifetime.
///
/// TestHost::metrics() exposes these for regression tests, such as proving a
/// signal update changes properties without increasing mounts. Counters describe
/// engine work, not elapsed time, GPU completion or confirmed screen presentation.
struct Metrics {
	std::uint64_t mounts =
		0; ///< Cumulative records created, including structural nodes; never decremented.
	std::uint64_t propertyUpdates =
		0; ///< Delivered retained assignments, including initial property installation.
	std::uint64_t layoutPasses =
		0; ///< Committed document layout projections, not individual measurement calls.
	std::uint64_t sceneRevisions =
		0; ///< Complete scenes published; not a presentation-completion count.
};

namespace detail {
/// @brief Internal mutable storage for one mounted component occurrence.
///
/// Document owns these records; mounting installs property bindings into scope,
/// layout writes bounds/measured, and scene projection reads the resolved values.
/// Do not retain a Node pointer across application callbacks: those callbacks may
/// remove the occurrence. Resolve its NodeId again instead. Application code uses
/// NodeHandle and NodeSnapshot rather than accessing this implementation record.
struct Node {
	NodeId id{};	 ///< Identity used by document lookup and non-owning handles.
	NodeId parent{}; ///< Owning-tree parent; zero serial identifies the root boundary.
	NodeKind kind =
		NodeKind::group; ///< Built-in retained behavior used by layout, drawing and input routing.
	Scope scope{};		 ///< Subscriptions and cleanup disposed when this occurrence is removed.
	std::vector<NodeId> children; ///< Current child/paint order; structural regions update it
								  ///< without remounting ancestors.
	Rect bounds{};	 ///< Latest arranged outer rectangle in document-relative logical coordinates.
	Size measured{}; ///< Latest intrinsic/constrained size computed before arrangement.
	Insets
		padding{}; ///< Resolved content insets; leading/trailing remain logical until arrangement.
	Frame frame{}; ///< Resolved explicit axis sizing and min/max constraints.
	Font font{};   ///< Unscaled system-font request shared by measurement and scene text commands.
	Color foreground = Color::black();	///< Text color used when isForegroundExplicit is set.
	Color background = Color::clear();	///< Resolved fill behind this occurrence and descendants.
	Color borderColor = Color::clear(); ///< Resolved outline color when borderWidth is positive.
	double borderWidth = 0;				///< Nonnegative outline width in logical points.
	double cornerRadius = 0; ///< Rounding radius shared by fill, outline and clip projection.
	double opacity = 1;		 ///< Local alpha multiplied into descendant paint opacity.
	double spacing = 0;		 ///< Gap used by sequential child layout.
	double scrollOffset =
		0; ///< Retained vertical viewport displacement, clamped during arrangement.
	Alignment alignment =
		Alignment::leading; ///< Logical horizontal placement policy for child content.
	Visibility visibility =
		Visibility::visible; ///< Paint/layout eligibility without scope disposal.
	bool clip = false;		 ///< Whether this occurrence clips descendant painting to its bounds.
	bool isForegroundExplicit =
		false;			   ///< False paints the theme label color instead of the stored foreground.
	bool isEnabled = true; ///< Local interaction gate; ancestor policy is checked separately.
	bool inert =
		false; ///< Blocks descendant focus and input without forcing a disabled paint style.
	bool checked = false; ///< Latest boolean binding value of a Toggle occurrence.
	std::string
		text{}; ///< Latest resolved text input used by label/control measurement and painting.
	std::optional<std::string>
		accessibilityLabel{};	  ///< Explicit application-authored semantic label, if any.
	std::string diagnosticName{}; ///< Debug identity supplied by lifecycle configuration, not an
								  ///< accessible name.
	Drawing drawing{};			  ///< Local-coordinate Canvas commands copied at mount.
	std::function<void()> onClick;
	std::function<void(PointerEvent &)> onPointerDown;
	std::function<void(PointerEvent &)> onPointerDownCapture;
};

/// @brief Owns one window's retained nodes, layout state and published projections.
///
/// Native application sessions and TestHost both create a Document, then mount
/// descriptions through MountContext. Subscriptions update existing records; the
/// reactive runtime calls commit() only after the current binding batch settles.
/// commit() publishes coherent layout, node snapshots and a complete paint scene
/// before mount effects run. The host observes publication to request native paint.
///
/// Confined to its UI executor. The native rendering boundary receives an owning
/// const SceneSnapshot, never this Document or borrowed Node pointers. close()
/// disposes scopes and invalidates handles; importing modules creates no Document.
class Document : public std::enable_shared_from_this<Document> {
  public:
	[[nodiscard]] static auto create(Size size, Environment environment = {})
		-> std::shared_ptr<Document>;
	explicit Document(Size size, Environment environment);
	Document(const Document &) = delete;
	Document(Document &&) = delete;
	auto operator=(const Document &) -> Document & = delete;
	auto operator=(Document &&) -> Document & = delete;
	~Document();
	void close();

	[[nodiscard]] auto root() const noexcept -> NodeId { return root_; }

	[[nodiscard]] auto createNode(NodeKind kind, NodeId parent) -> NodeId;
	[[nodiscard]] auto find(NodeId identity) const -> Node *;
	[[nodiscard]] auto handle(NodeId identity) -> NodeHandle;
	void remove(NodeId identity);
	void removeChildren(NodeId identity);
	[[nodiscard]] auto reorder(NodeId parent, const std::vector<NodeId> &children)
		-> std::expected<void, Error>;
	void invalidate();
	void commit();
	void resize(Size size);
	void setEnvironment(Environment environment);

	[[nodiscard]] auto environment() const noexcept -> Environment { return environment_; }

	[[nodiscard]] auto snapshots() const -> const std::vector<NodeSnapshot> & { return snapshots_; }

	[[nodiscard]] auto scene() const -> std::shared_ptr<const SceneSnapshot> { return scene_; }

	[[nodiscard]] auto metrics() const noexcept -> Metrics { return metrics_; }

	[[nodiscard]] auto errors() const -> const std::vector<Error> & { return errors_; }

	void report(Error error);
	[[nodiscard]] auto activate(NodeId identity) -> std::expected<void, Error>;
	[[nodiscard]] auto focus(NodeId identity) -> std::expected<void, Error>;
	void tab(bool backwards = false);
	void pointerDown(Point point);
	void pointerUp(Point point);
	void cancelPointer();
	void scroll(Point point, double delta);
	void keyActivate();
	void setTextMeasure(std::function<Size(const std::string &, Font, double)> measure);
	void setOnPublish(std::function<void()> callback);
	/// @brief Uses last-confirmed immutable geometry for coordinate-driven input.
	/// Native hosts enable this before presentation; default headless tests use
	/// current committed layout unless presentation testing is explicitly enabled.
	void enablePresentationTracking();
	/// @brief Registers one exact native/synthetic submission against retained scene geometry.
	[[nodiscard]] auto beginPresentation(std::uint64_t epoch, std::uint64_t sceneRevision)
		-> std::expected<PresentationAttempt, Error>;
	/// @brief Joins GPU evidence and publishes geometry only after matching positive display time.
	[[nodiscard]] auto reportGPUCompletion(PresentationAttempt attempt, bool success) -> bool;
	/// @brief Joins finite positive display evidence; stale feedback cannot gate current input.
	[[nodiscard]] auto reportPresentation(PresentationAttempt attempt, double timestamp) -> bool;
	/// @brief Expires one submission, gating its changed geometry and freeing history admission.
	void abandonPresentation(PresentationAttempt attempt);
	/// @brief Invalidates submission identities after surface/attachment replacement.
	void resetPresentation(std::uint64_t epoch);
	/// @brief Publishes geometry only after the caller joins GPU and presentation evidence.
	void confirmPresentation(std::uint64_t sceneRevision);
	/// @brief Gates changed old/new regions when display evidence becomes unknown.
	void markPresentationUnknown(std::uint64_t sceneRevision);
	[[nodiscard]] auto hitTest(Point point) const -> std::optional<NodeId>;

	template <typename T, typename F> void bind(NodeId identity, Property<T> property, F assign) {
		const auto weakDocument = weak_from_this();
		auto subscription = observe(
			std::move(property),
			[weakDocument, identity, setter = std::move(assign)](const T &value) -> void {
				if (const auto document = weakDocument.lock()) {
					if (auto *node = document->find(identity);
						node != nullptr && node->scope.alive()) {
						setter(*node, value);
						++document->metrics_.propertyUpdates;
						document->invalidate();
					}
				}
			}
		);
		if (auto *node = find(identity)) {
			static_cast<void>(node->scope.own(std::move(subscription)));
		}
	}

  private:
	std::uint64_t identity_ = 0;
	NodeId root_{};
	Size viewport_{};
	Environment environment_;
	std::map<std::uint64_t, std::unique_ptr<Node>> nodes_;
	std::vector<NodeSnapshot> snapshots_;
	std::shared_ptr<const SceneSnapshot> scene_;
	Metrics metrics_;
	std::vector<Error> errors_;
	bool isDirty_ = true;
	bool isClosed_ = false;
	std::optional<NodeId> focused_;
	std::optional<NodeId> pressed_;
	std::function<Size(const std::string &, Font, double)> measureText_;
	std::function<void()> onPublish_;
	bool usesPresentationGeometry_ = false; ///< Selects acknowledged geometry for location input.
	PresentationTracker presentation_{}; ///< Shared native/test evidence join on the UI executor.
	std::uint64_t unknownRevision_ =
		0; ///< Newest explicitly unknown scene; older confirmations cannot clear its gate.
	std::uint64_t confirmedRevision_ = 0; ///< Last scene whose native display evidence was joined.
	std::map<NodeId, Rect>
		confirmedBounds_{}; ///< Immutable-by-replacement displayed node rectangles.
	std::map<std::uint64_t, std::map<NodeId, Rect>>
		geometryHistory_{}; ///< Bounded pending scene geometry.
	std::vector<Rect>
		ambiguousRegions_{}; ///< Changed regions with explicitly unknown display state.
	[[nodiscard]] auto interactionBounds(NodeId identity) const -> std::optional<Rect>;
	void rememberGeometry();
	[[nodiscard]] auto measure(NodeId identity, Size available) -> Size;
	void arrange(NodeId identity, Rect bounds);
	void paint(NodeId identity, SceneSnapshot &scene, double opacity);
	void snapshot(NodeId identity);
	[[nodiscard]] auto isEligible(NodeId identity) const -> bool;
	[[nodiscard]] auto hitTestNode(NodeId identity, Point point) const -> std::optional<NodeId>;
};
} // namespace detail
} // namespace stana
