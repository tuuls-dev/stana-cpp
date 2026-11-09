module;

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

export module stana:testing;
export import :ui;
import :document;
import :symbol;

export namespace stana::testing {
/// @brief Owning GPU inspection record; this is not a headless GPU execution result.
struct GPUCanvasPaint {
	NodeId instance;
	Rect bounds;
	::stana::rendering::GPUDrawing drawing;
	Environment environment;
	Opacity opacity{1};
};

using MixedPaint =
	std::variant<::stana::rendering::PaintCommand, GPUCanvasPaint, ::stana::symbol::Presentation>;

/// @brief Deterministic consumer host. Uses the production mount/document path without
/// creating NSApplication, a native window, or a wall-clock-driven scheduler.
class TestHost {
  public:
	explicit TestHost(
		Size size = {.width = defaultWidth, .height = defaultHeight},
		Environment environment = {}
	)
		: lease_(::stana::reactive::detail::acquireHeadlessExecutorLease()),
		  document_(::stana::document::detail::Document::create(size, environment)) {}

	TestHost(const TestHost &) = delete;
	TestHost(TestHost &&) = delete;
	auto operator=(const TestHost &) -> TestHost & = delete;
	auto operator=(TestHost &&) -> TestHost & = delete;

	~TestHost() { close(); }

	/// @brief Replaces root content through the production typed mounting path.
	/// Existing root descendants are disposed; subscriptions settle before publication.
	template <::stana::ui::Component T> void mount(const T &description) {
		::stana::reactive::Transaction::perform([&] -> void {
			document_->removeChildren(document_->root());
			::stana::ui::MountContext context{.document = document_, .parent = document_->root()};
			using ::stana::ui::mount;
			static_cast<void>(mount(description, context));
		});
	}

	/// @brief Enables native-style last-confirmed geometry and initial input gating.
	void enablePresentationTesting() { document_->enablePresentationTracking(); }

	/// @brief Replaces the synthetic drawable surface; old callbacks become ineligible.
	/// Last-confirmed geometry remains available until new evidence changes it.
	void replaceSurface() {
		presentationEpoch_ = ::stana::detail::nextIdentity();
		document_->resetPresentation(presentationEpoch_);
	}

	/// @brief Registers a synthetic frame; optional acknowledgement is explicit test evidence.
	[[nodiscard]] auto render(bool acknowledge = true) -> ::stana::rendering::PresentationAttempt {
		flush();
		const auto scene = document_->renderSnapshot();
		if (!scene) {
			throw std::logic_error("No scene has been published");
		}
		const auto attempt = document_->beginPresentation(presentationEpoch_, scene->revision);
		if (!attempt) {
			throw std::runtime_error("Presentation history is full");
		}
		if (acknowledge) {
			reportGPUCompletion(*attempt, true);
			reportPresentation(*attempt, PresentationTimePoint{PresentationDuration{1}});
		}
		return *attempt;
	}

	/// @brief Injects one GPU completion observation without pretending it proves display.
	void reportGPUCompletion(::stana::rendering::PresentationAttempt attempt, bool success) {
		static_cast<void>(document_->reportGPUCompletion(attempt, success));
	}

	/// @brief Injects native presentation time; zero/unknown evidence keeps geometry gated.
	void reportPresentation(
		::stana::rendering::PresentationAttempt attempt,
		PresentationTimePoint timestamp
	) {
		static_cast<void>(document_->reportPresentation(attempt, timestamp));
	}

	/// @brief Simulates a submission deadline without creating positive display evidence.
	void abandonPresentation(::stana::rendering::PresentationAttempt attempt) {
		document_->abandonPresentation(attempt);
	}

	/// @brief Invalidates production caches to provide a semantic oracle for reuse tests.
	void forceFullRecompute() { document_->forceFullRecompute(); }

	/// @brief Admits a synthetic composite and gates its changed old/new geometry.
	[[nodiscard]] auto renderComposite(std::span<const ::stana::rendering::TileId> tiles)
		-> ::stana::rendering::PresentationAttempt {
		flush();
		const auto snapshot = document_->renderSnapshot();
		if (!snapshot) {
			throw std::logic_error("No published scene");
		}
		const auto attempt =
			document_->beginPresentation(presentationEpoch_, snapshot->revision, tiles);
		if (!attempt) {
			throw std::runtime_error("Composite admission failed");
		}
		document_->gatePendingPresentation(*attempt);
		return *attempt;
	}

	/// @brief Supplies typed synthetic evidence, never actual native display proof.
	void reportTilePresentation(
		::stana::rendering::PresentationAttempt attempt,
		::stana::rendering::TilePresentation observation
	) {
		static_cast<void>(document_->reportPresentation(attempt, observation));
	}

	/// @brief Drains one mailbox snapshot on this owner, then settles and commits.
	/// Hosts that share the owner executor also share that mailbox; flushing this
	/// host can apply queued writes observed by another live host on the same thread.
	void flush() {
		if (const auto endpoint = ::stana::reactive::detail::ExecutorEndpoint::currentIfAny()) {
			endpoint->drain();
		}
		::stana::reactive::detail::ReactiveRuntime::current().flush();
		document_->commit();
	}

	/// @brief Permanently disposes this host document; subsequent handles become stale.
	void close() {
		document_->close();
		lease_.release();
	}

	/// @brief Returns value snapshots from the last complete document commit.
	/// Query again after changes; retaining a snapshot does not keep its node alive.
	[[nodiscard]] auto nodes() const -> std::vector<::stana::document::NodeSnapshot> {
		return document_->snapshots();
	}

	/// @brief Returns cumulative work counters for detecting unwanted remounts or extra commits.
	[[nodiscard]] auto metrics() const -> ::stana::document::Metrics {
		return document_->metrics();
	}

	/// @brief Returns reported recoverable errors, including rejected keyed updates.
	[[nodiscard]] auto errors() const -> std::vector<Error> { return document_->errors(); }

	/// @brief Shares the last complete immutable paint scene, or null before publication.
	[[nodiscard]] auto scene() const -> std::shared_ptr<const ::stana::rendering::SceneSnapshot> {
		return document_->scene();
	}

	/// @brief Complete typed painter order. scene()->commands deliberately excludes GPU pixels.
	[[nodiscard]] auto mixedPaint() const -> std::vector<MixedPaint> {
		std::vector<MixedPaint> result;
		const auto append =
			[&](
				const auto &self,
				const std::shared_ptr<const ::stana::rendering::detail::SceneBranch> &branch
			) -> void {
			if (!branch || !branch->context.visible) {
				return;
			}
			for (const auto &command : branch->paint->prefix) {
				result.emplace_back(command);
			}
			if (const auto &gpu = branch->paint->gpu) {
				result.emplace_back(
					GPUCanvasPaint{
						.instance = gpu->instance,
						.bounds = gpu->bounds,
						.drawing = gpu->drawing,
						.environment = gpu->environment,
						.opacity = gpu->opacity
					}
				);
			}
			if (const auto &symbol = branch->paint->symbol) {
				result.emplace_back(symbol->presentation);
			}
			for (const auto &child : branch->children) {
				self(self, child);
			}
			for (const auto &command : branch->paint->suffix) {
				result.emplace_back(command);
			}
		};
		if (const auto snapshot = document_->renderSnapshot()) {
			append(append, snapshot->root);
		}
		return result;
	}

	/// @brief Injects exact production diagnostic identities, without fabricating GPU work.
	void injectGPUDiagnostic(
		::stana::rendering::GPUDiagnostic diagnostic,
		std::uint64_t drawingVersion,
		std::uint64_t epoch = 0
	) {
		document_->queueGPUDiagnostic(std::move(diagnostic), drawingVersion, epoch);
		flush();
	}

	/// @brief Creates a non-owning capability for an occurrence in this host document.
	[[nodiscard]] auto handle(NodeId identity) const -> ::stana::document::NodeHandle {
		return document_->handle(identity);
	}

	/// @brief Uses normal logical activation; it does not synthesize a location event.
	[[nodiscard]] auto activate(NodeId identity) -> std::expected<void, Error> {
		return document_->activate(identity);
	}

	/// @brief Dispatches press and release through normal hit testing and presentation gates.
	void click(Point point) {
		document_->pointerDown(point);
		document_->pointerUp(point);
	}

	/// @brief Starts pointer dispatch, including capture/bubble handlers and default focus.
	void pointerDown(Point point) { document_->pointerDown(point); }

	/// @brief Releases the current press; eligible release-inside activates its control.
	void pointerUp(Point point) { document_->pointerUp(point); }

	/// @brief Cancels pending default activation without invoking the pressed control.
	void cancelPointer() { document_->cancelPointer(); }

	/// @brief Moves keyboard focus through eligible controls in published tree order.
	void tab(bool backwards = false) { document_->tab(backwards); }

	/// @brief Activates the currently focused eligible control through the common action path.
	void keyActivate() { document_->keyActivate(); }

	/// @brief Applies a logical vertical delta to the nearest eligible scroll ancestor.
	/// A scroll handler may prevent that default. The event still runs first.
	void scroll(Point point, Points delta) { document_->scroll(point, delta); }

	/// @brief Moves the pointer and reports enter, leave, and move against hit testing.
	void pointerMove(Point point) { document_->pointerMove(point); }

	/// @brief Leaves the window. Entered occurrences receive pointer leave.
	void pointerLeave() { document_->pointerLeave(); }

	/// @brief Delivers a key press to the focused control, or the root when none is eligible.
	/// Tab, Space, and Return keep their default unless a handler prevents it.
	void keyDown(::stana::document::Key key, bool backwards = false) {
		document_->keyDown(key, backwards);
	}

	/// @brief Delivers a key release. Release has no built-in default.
	void keyUp(::stana::document::Key key, bool backwards = false) {
		document_->keyUp(key, backwards);
	}

	/// @brief Delivers one explicit frame, then publishes the resulting paint.
	void tickFrames(::stana::ui::FrameTime frame) {
		::stana::reactive::Transaction::perform([&] -> void {
			document_->tickFrames(
				{.timestamp = frame.timestamp, .targetTimestamp = frame.targetTimestamp}
			);
		});
		flush();
	}

	/// @brief Pauses symbol playback while a host is hidden, minimized, or detached.
	void setHostVisible(bool visible) {
		document_->setHostVisible(visible);
		flush();
	}

	[[nodiscard]] auto frameDemand() const -> bool { return document_->frameDemand(); }

	[[nodiscard]] auto symbolBaseline(NodeId identity) const -> std::optional<Points> {
		const auto *node = document_->find(identity);
		if (node == nullptr || !node->symbol) {
			return std::nullopt;
		}
		return node->symbol->baseline;
	}

	/// @brief Preparation identity of one mounted symbol, for stale-result checks.
	[[nodiscard]] auto symbolGeneration(NodeId identity) const -> std::optional<std::uint64_t> {
		const auto *node = document_->find(identity);
		if (node == nullptr || !node->symbol) {
			return std::nullopt;
		}
		return node->symbol->generation;
	}

	/// @brief Renderer epoch last adopted by one mounted symbol. Zero means none yet.
	[[nodiscard]] auto symbolEpoch(NodeId identity) const -> std::optional<std::uint64_t> {
		const auto *node = document_->find(identity);
		if (node == nullptr || !node->symbol) {
			return std::nullopt;
		}
		return node->symbol->epoch;
	}

	void injectSymbolDiagnostic(
		::stana::symbol::Diagnostic diagnostic,
		NodeId instance,
		std::uint64_t generation,
		std::uint64_t epoch = 0
	) {
		document_->queueSymbolDiagnostic(
			std::move(diagnostic),
			{.instance = instance, .generation = generation, .epoch = epoch}
		);
		flush();
	}

	/// @brief Delivers a successful raster for one preparation. Restores an omitted leaf.
	void injectSymbolSuccess(NodeId instance, std::uint64_t generation, std::uint64_t epoch = 0) {
		static_cast<void>(document_->queueSymbolResult(
			std::nullopt,
			{.instance = instance, .generation = generation, .epoch = epoch}
		));
		flush();
	}

	/// @brief Advances deterministic task wakeups without waiting for wall-clock time.
	void advanceTime(std::chrono::milliseconds elapsed) {
		::stana::tasks::detail::TaskScheduler::current().advance(elapsed);
		flush();
	}

	/// @brief Reflows the document for a new logical viewport without opening a native window.
	void resize(Size size) { document_->resize(size); }

	/// @brief Republishes layout and paint for new document-wide appearance/direction inputs.
	void setEnvironment(Environment environment) { document_->setEnvironment(environment); }

  private:
	static constexpr Points defaultWidth{800};
	static constexpr Points defaultHeight{600};
	::stana::reactive::detail::HeadlessExecutorLease
		lease_; ///< Shared-owner claim; last host closes the endpoint.
	std::shared_ptr<::stana::document::detail::Document>
		document_; ///< Owns the same retained engine used by native windows.
	std::uint64_t presentationEpoch_ =
		::stana::detail::nextIdentity(); ///< Synthetic surface generation.
};
} // namespace stana::testing
