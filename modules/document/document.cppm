module;

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

export module stana:document;
import :primitives;
import :reactive;
import :rendering;
import :layout;
export import :symbol_asset;
export import :symbol_playback;
export import :symbol_geometry;

export namespace stana::document {
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
	cpuCanvas,
	gpuCanvas,
	symbol
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

	NodeHandle(std::weak_ptr<::stana::document::detail::Document> document, NodeId identity)
		: document_(std::move(document)),
		  identity_(identity) {}

	[[nodiscard]] auto id() const noexcept -> NodeId { return identity_; }

	[[nodiscard]] auto isAlive() const -> bool;
	[[nodiscard]] auto activate() const -> std::expected<void, Error>;
	[[nodiscard]] auto requestFocus() const -> std::expected<void, Error>;

  private:
	std::weak_ptr<::stana::document::detail::Document> document_;
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
/// change the UI. Native conversion consumes the internal immutable render tree, not this view.
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

namespace detail {
/// @brief Independent retained work domains. Routes do not imply local recomputation.
enum class [[clang::flag_enum]] Dirty : std::uint8_t {
	none = 0,
	measure = 1,
	arrange = 2,
	paint = 4,
	snapshot = 8,
	structure = 16
};

[[nodiscard]] constexpr auto operator|(Dirty left, Dirty right) -> Dirty {
	return static_cast<Dirty>(static_cast<unsigned>(left) | static_cast<unsigned>(right));
}

[[nodiscard]] constexpr auto has(Dirty value, Dirty bits) -> bool {
	return (static_cast<unsigned>(value) & static_cast<unsigned>(bits)) != 0;
}

[[nodiscard]] constexpr auto without(Dirty value, Dirty bits) -> Dirty {
	return static_cast<Dirty>(static_cast<unsigned>(value) & ~static_cast<unsigned>(bits));
}

inline constexpr Dirty fullWork =
	Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::snapshot | Dirty::structure;

} // namespace detail
} // namespace stana::document

export namespace stana::rendering::detail {
/// @brief One fitted symbol ready for native preparation. Omitted when nothing is painted.
struct SymbolLeaf {
	NodeId instance;
	Rect bounds{};
	Rect ink{};
	::stana::symbol::Presentation presentation{};
	double opacity = 1;
};

struct GPUCanvasLeaf {
	NodeId instance;
	Rect bounds;
	GPUDrawing drawing;
	Environment environment;
	double opacity = 1;
};

/// @brief Local painter segments; the suffix restores the node clip before its outline.
struct PaintSegments {
	std::uint64_t identity = 0;
	std::vector<PaintCommand> prefix;
	std::vector<PaintCommand> suffix;
	std::optional<GPUCanvasLeaf> gpu;
	std::optional<SymbolLeaf> symbol;
	std::optional<Rect> childClipBounds;
};

/// @brief Effective inherited state, separate from local disabled alpha application.
struct ProjectionContext {
	std::uint64_t parentRoute =
		0; ///< Acknowledged hit-route lineage, independent of paint identity.
	std::size_t siblingOrder = 0; ///< Reordering overlapping siblings changes interaction routing.
	double opacity = 1;
	bool visible = true;
	bool eligible = true;
	Rect rectangularClip{};
	auto operator<=>(const ProjectionContext &) const = default;
};

/// @brief Complete immutable occurrence projection; path copies share local payloads.
struct SceneBranch {
	std::uint64_t routeGeneration =
		0; ///< Stable until this occurrence's ordered ancestor route changes.
	NodeId id{};
	std::uint64_t generation = 0;
	ProjectionContext context{};
	std::shared_ptr<const PaintSegments> paint;
	std::shared_ptr<const ::stana::document::NodeSnapshot> snapshot;
	std::vector<std::shared_ptr<const SceneBranch>> children;
};

/// @brief Visible occurrence geometry, including acknowledged interaction policy.
struct GeometryRecord {
	std::uint64_t routeGeneration =
		0; ///< Gates order-only hit-route changes, including overflowing descendants.
	Rect bounds{};
	Rect rectangularClip{};
	NodeId parent{};
	bool eligible = true;
	std::shared_ptr<const std::vector<NodeId>>
		children{}; ///< Immutable acknowledged painter/hit order.

	[[nodiscard]] auto operator==(const GeometryRecord &other) const -> bool {
		const bool sameChildren = children == other.children ||
			((!children || children->empty()) && (!other.children || other.children->empty())) ||
			(children && other.children && *children == *other.children);
		return routeGeneration == other.routeGeneration && bounds == other.bounds &&
			rectangularClip == other.rectangularClip && parent == other.parent &&
			eligible == other.eligible && sameChildren;
	}
};
struct GeometryIndex;
using GeometryRoot = std::shared_ptr<const GeometryIndex>;
[[nodiscard]] auto geometryFind(const GeometryRoot &, NodeId) -> std::optional<GeometryRecord>;
[[nodiscard]] auto geometrySet(const GeometryRoot &, NodeId, const GeometryRecord &)
	-> GeometryRoot;
[[nodiscard]] auto geometryErase(const GeometryRoot &, NodeId) -> GeometryRoot;
void geometryChanges(const GeometryRoot &, const GeometryRoot &, std::vector<Rect> &);

/// @brief Complete internal production publication; no flat inspection is required.
struct RenderSnapshot {
	std::uint64_t revision = 0;
	Size size{};
	Color background = Color::white();
	std::uint64_t textEnvironment = 0;
	std::shared_ptr<const SceneBranch> root;
	GeometryRoot geometry;
};

} // namespace stana::rendering::detail

export namespace stana::document {
namespace detail {
/// @brief UI-confined caches and direct-child routes for one retained occurrence.
struct NodeWork {
	Dirty local = fullWork;
	std::set<NodeId> measureChildren;
	std::set<NodeId> arrangeChildren;
	std::set<NodeId> projectionChildren;
	std::optional<::stana::layout::detail::MeasureKey> measuredAt;
	std::optional<::stana::layout::detail::ArrangeKey> arrangedAt;
	std::shared_ptr<const ::stana::rendering::detail::SceneBranch> projection;
	std::uint64_t measureInputs = 0;
	std::uint64_t descendantInputs = 0;
	std::uint64_t placementInputs = 0;
	std::uint64_t childMeasurements = 0;
};
} // namespace detail

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
		0; ///< Commits that actually compute measurement or placement; paint-only commits excluded.
	std::uint64_t sceneRevisions =
		0; ///< Complete scenes published; not a presentation-completion count.
	std::uint64_t measurements = 0; ///< Actual local measure computations, excluding cache returns.
	std::uint64_t arrangements =
		0; ///< Actual local placement computations, excluding route forwarding.
	std::uint64_t paintBuilds = 0; ///< Local paint segment builds, excluding ancestor path copies.
	std::uint64_t projectionBuilds = 0;		 ///< New immutable branch objects.
	std::uint64_t nodeMaterializations = 0;	 ///< Lazy visible-node cache builds.
	std::uint64_t sceneMaterializations = 0; ///< Lazy complete display-list cache builds.
};

namespace detail {
inline constexpr double defaultSymbolSize = 24;

/// @brief Retained symbol occurrence. Layout reads size; paint reads the effect sample.
struct SymbolState {
	::stana::symbol::Asset asset{};
	double size = defaultSymbolSize;
	Color color = Color::text();
	std::optional<double> strokeWidth{};
	::stana::symbol::Playback playback = ::stana::symbol::Playback::none();
	::stana::symbol::PlaybackState playbackState{};
	::stana::symbol::EffectSample effect{};
	std::uint64_t generation = 0;
	bool available = true;
	std::optional<Rect> ink{};
	double baseline = 0;
	::stana::symbol::Asset previousAsset{};
	std::optional<double> previousStroke{};
	std::optional<double> previousScale{};
	std::uint64_t epoch = 0;
	std::set<::stana::symbol::Diagnostic::Code> failed{};
	std::uint64_t frameRegistration = 0;
	std::function<void(const ::stana::symbol::Diagnostic &)> onError{};
};

/// @brief Internal mutable storage for one mounted component occurrence.
///
/// Document owns these records; mounting installs property bindings into scope,
/// layout writes bounds/measured, and scene projection reads the resolved values.
/// Do not retain a Node pointer across application callbacks: those callbacks may
/// remove the occurrence. Resolve its NodeId again instead. Application code uses
/// NodeHandle and NodeSnapshot rather than accessing this implementation record.
struct Node {
	NodeWork work{}; ///< Classified local work and persistent projection cache.
	NodeId id{};	 ///< Identity used by document lookup and non-owning handles.
	NodeId parent{}; ///< Owning-tree parent; zero serial identifies the root boundary.
	NodeKind kind =
		NodeKind::group; ///< Built-in retained behavior used by layout, drawing and input routing.
	::stana::reactive::Scope
		scope{}; ///< Subscriptions and cleanup disposed when this occurrence is removed.
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
	std::optional<CornerRadii>
		cornerRadii{}; ///< Opt-in outer contour; scalar fallback is preserved.
	std::optional<BorderWidths> borderWidths{}; ///< Opt-in inward edges; never layout insets.
	std::vector<::stana::rendering::BoxShadow> boxShadows{}; ///< Local, non-inherited paint layers.
	double opacity = 1; ///< Local alpha multiplied into descendant paint opacity.
	double spacing = 0; ///< Gap used by sequential child layout.
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
	::stana::rendering::Drawing drawing{};
	std::optional<::stana::rendering::GPUDrawing> gpuDrawing;
	std::function<void(const ::stana::rendering::GPUDiagnostic &)>
		onGPUError; ///< Local-coordinate CPUCanvas commands copied at mount.
	std::optional<SymbolState> symbol{};
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
/// const RenderSnapshot, never this Document or borrowed Node pointers. close()
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
	void invalidate(NodeId identity, Dirty domains);
	void forceFullRecompute();
	void commit();
	void resize(Size size);
	void setEnvironment(Environment environment);

	[[nodiscard]] auto environment() const noexcept -> Environment { return environment_; }

	[[nodiscard]] auto snapshots() const -> const std::vector<NodeSnapshot> &;

	[[nodiscard]] auto scene() const -> std::shared_ptr<const ::stana::rendering::SceneSnapshot>;

	[[nodiscard]] auto renderSnapshot() const
		-> std::shared_ptr<const ::stana::rendering::detail::RenderSnapshot> {
		return render_;
	}

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
		-> std::expected<::stana::rendering::PresentationAttempt, Error>;
	/// @brief Joins GPU evidence and publishes geometry only after matching positive display time.
	[[nodiscard]] auto
	reportGPUCompletion(::stana::rendering::PresentationAttempt attempt, bool success) -> bool;
	/// @brief Joins finite positive display evidence; stale feedback cannot gate current input.
	[[nodiscard]] auto
	reportPresentation(::stana::rendering::PresentationAttempt attempt, double timestamp) -> bool;
	/// @brief Expires one submission, gating its changed geometry and freeing history admission.
	void abandonPresentation(::stana::rendering::PresentationAttempt attempt);
	/// @brief Invalidates submission identities after surface/attachment replacement.
	/// A new epoch retries every symbol: failures clear, generation advances, and loops
	/// that were stopped by a failure restart at rest on the next commit.
	void resetPresentation(std::uint64_t epoch);
	void holdPreparedGeometry(std::uint64_t revision);
	void queueGPUDiagnostic(
		::stana::rendering::GPUDiagnostic,
		std::uint64_t drawingVersion,
		std::uint64_t epoch
	);

	/// @brief One display-link sample. The first sample after a reset reports zero elapsed.
	struct FrameStamp {
		double timestamp = 0;
		double targetTimestamp = 0;
	};

	[[nodiscard]] auto addFrameRegistration(
		std::function<bool()> active,
		std::function<void(FrameStamp, double elapsed)> callback
	) -> std::uint64_t;
	void cancelFrameRegistration(std::uint64_t registration);
	void resetFrameTimestamp(std::uint64_t registration);
	void tickFrames(FrameStamp stamp);
	void setFrameDemandHandler(std::function<void(bool)> handler);
	void refreshFrameDemand();
	[[nodiscard]] auto frameDemand() const -> bool;
	void setHostVisible(bool visible);

	[[nodiscard]] auto hostVisible() const noexcept -> bool { return hostVisible_; }

	/// @brief Which symbol preparation a native failure belongs to.
	/// Generation and epoch travel together so a stale raster result cannot
	/// attach to a newer asset or a newer renderer epoch.
	struct SymbolPreparation {
		NodeId instance{};
		std::uint64_t generation = 0;
		std::uint64_t epoch = 0;
	};

	/// @brief Applies one raster outcome after the matching generation and epoch checks.
	/// An empty diagnostic is success and restores a leaf that had been omitted.
	/// @return True when retained state changed and a commit effect was scheduled.
	[[nodiscard]] auto queueSymbolResult(
		std::optional<::stana::symbol::Diagnostic> diagnostic,
		SymbolPreparation preparation
	) -> bool;
	void
	queueSymbolDiagnostic(::stana::symbol::Diagnostic diagnostic, SymbolPreparation preparation);
	[[nodiscard]] auto hitTest(Point point) const -> std::optional<NodeId>;

	[[nodiscard]] auto beginPresentation(
		std::uint64_t epoch,
		std::uint64_t revision,
		std::span<const ::stana::rendering::TileId> tiles
	) -> std::expected<::stana::rendering::PresentationAttempt, Error>;
	[[nodiscard]] auto reportPresentation(
		::stana::rendering::PresentationAttempt,
		::stana::rendering::TilePresentation
	) -> bool;
	void gatePendingPresentation(::stana::rendering::PresentationAttempt);
	void gateSurface();

	template <typename T, typename F>
	void bind(NodeId identity, ::stana::reactive::Property<T> property, F assign) {
		const auto weakDocument = weak_from_this();
		auto subscription = ::stana::reactive::observe(
			std::move(property),
			[weakDocument, identity, setter = std::move(assign)](const T &value) -> void {
				if (const auto document = weakDocument.lock()) {
					if (auto *node = document->find(identity);
						node != nullptr && node->scope.alive()) {
						if constexpr (std::is_same_v<
										  std::invoke_result_t<F, Node &, const T &>,
										  Dirty>) {
							const auto domains = setter(*node, value);
							if (domains != Dirty::none) {
								document->invalidate(identity, domains);
							}
						} else {
							setter(*node, value);
							document->invalidate();
						}
						++document->metrics_.propertyUpdates;
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
	mutable std::vector<NodeSnapshot> snapshots_;
	mutable bool hasNodeMaterialization_ = false;
	mutable std::shared_ptr<const ::stana::rendering::SceneSnapshot> scene_;
	std::shared_ptr<const ::stana::rendering::detail::RenderSnapshot> render_;
	mutable Metrics metrics_;
	std::uint64_t mutationStamp_ = 0;
	std::optional<std::uint64_t> failedStamp_;
	std::uint64_t textEnvironment_ = 1;
	bool isCommitting_ = false;
	std::vector<Error> errors_;
	bool isDirty_ = true;
	bool isClosed_ = false;
	std::optional<NodeId> focused_;
	std::optional<NodeId> pressed_;
	std::function<Size(const std::string &, Font, double)> measureText_;
	std::function<void()> onPublish_;
	bool usesPresentationGeometry_ = false; ///< Selects acknowledged geometry for location input.
	::stana::rendering::PresentationTracker
		presentation_{}; ///< Shared native/test evidence join on the UI executor.
	std::uint64_t unknownRevision_ =
		0; ///< Newest explicitly unknown scene; older confirmations cannot clear its gate.
	std::uint64_t confirmedRevision_ = 0; ///< Last scene whose native display evidence was joined.
	::stana::rendering::detail::GeometryRoot confirmedGeometry_;
	std::map<std::uint64_t, ::stana::rendering::detail::GeometryRoot> geometryHistory_;
	std::optional<std::pair<std::uint64_t, ::stana::rendering::detail::GeometryRoot>>
		preparedGeometry_;

	struct PinnedGeometry {
		::stana::rendering::PresentationAttempt attempt;
		::stana::rendering::detail::GeometryRoot root;
	};

	std::map<std::uint64_t, PinnedGeometry> pinnedGeometry_;
	std::vector<Rect> ambiguousRegions_;
	bool surfaceGated_ = false;
	std::uint64_t presentationEpoch_ = 0;
	using GPUFailureKey = std::tuple<
		NodeId,
		std::uint64_t,
		std::uint64_t,
		std::uint64_t,
		::stana::rendering::GPUDiagnostic::Category>;
	std::set<GPUFailureKey> gpuFailures_;

	struct FrameRegistration {
		std::uint64_t id = 0;
		std::function<bool()> active;
		std::function<void(FrameStamp, double elapsed)> callback;
		std::optional<double> lastTimestamp;
		bool cancelled = false;
	};

	std::vector<FrameRegistration> frames_{};
	std::function<void(bool)> frameDemandHandler_{};
	bool lastFrameDemand_ = false;
	bool hostVisible_ = true;

	void reconcileSymbols();
	[[nodiscard]] auto frameById(std::uint64_t registration) -> FrameRegistration *;
	[[nodiscard]] auto frameById(std::uint64_t registration) const -> const FrameRegistration *;

	[[nodiscard]] auto interactionBounds(NodeId identity) const -> std::optional<Rect>;
	void rememberGeometry();
	[[nodiscard]] auto measure(NodeId identity, Size available) -> Size;
	void arrange(NodeId identity, Rect bounds);
	[[nodiscard]] auto project(
		NodeId,
		::stana::rendering::detail::ProjectionContext,
		::stana::rendering::detail::GeometryRoot &,
		std::vector<
			std::pair<NodeId, std::shared_ptr<const ::stana::rendering::detail::SceneBranch>>> &
	) -> std::shared_ptr<const ::stana::rendering::detail::SceneBranch>;
	void gateGeometry(std::uint64_t, const ::stana::rendering::detail::GeometryRoot &);
	void acceptPresentation(::stana::rendering::PresentationAttempt);
	[[nodiscard]] auto isEligible(NodeId identity) const -> bool;
	[[nodiscard]] auto hitTestNode(NodeId identity, Point point) const -> std::optional<NodeId>;
};
} // namespace detail
} // namespace stana::document
