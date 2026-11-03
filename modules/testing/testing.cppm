module;

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module stana.testing;
export import stana.ui;
import stana.core;

export namespace stana {
/// @brief Deterministic consumer host. Uses the production mount/document path without
/// creating NSApplication, a native window, or a wall-clock-driven scheduler.
class TestHost {
  public:
	explicit TestHost(
		Size size = {.width = defaultWidth, .height = defaultHeight},
		Environment environment = {}
	)
		: document_(detail::Document::create(size, environment)) {}

	TestHost(const TestHost &) = delete;
	TestHost(TestHost &&) = delete;
	auto operator=(const TestHost &) -> TestHost & = delete;
	auto operator=(TestHost &&) -> TestHost & = delete;

	~TestHost() { close(); }

	/// @brief Replaces root content through the production typed mounting path.
	/// Existing root descendants are disposed; subscriptions settle before publication.
	template <Component T> void mount(const T &description) {
		Transaction::perform([&] -> void {
			document_->removeChildren(document_->root());
			MountContext context{.document = document_, .parent = document_->root()};
			static_cast<void>(stana::mount(description, context));
		});
	}

	/// @brief Enables native-style last-confirmed geometry and initial input gating.
	void enablePresentationTesting() { document_->enablePresentationTracking(); }

	/// @brief Replaces the synthetic drawable surface; old callbacks become ineligible.
	/// Last-confirmed geometry remains available until new evidence changes it.
	void replaceSurface() {
		presentationEpoch_ = detail::nextIdentity();
		document_->resetPresentation(presentationEpoch_);
	}

	/// @brief Registers a synthetic frame; optional acknowledgement is explicit test evidence.
	[[nodiscard]] auto render(bool acknowledge = true) -> PresentationAttempt {
		flush();
		const auto scene = document_->scene();
		if (!scene) {
			throw std::logic_error("No scene has been published");
		}
		const auto attempt = document_->beginPresentation(presentationEpoch_, scene->revision);
		if (!attempt) {
			throw std::runtime_error("Presentation history is full");
		}
		if (acknowledge) {
			reportGPUCompletion(*attempt, true);
			reportPresentation(*attempt, 1);
		}
		return *attempt;
	}

	/// @brief Injects one GPU completion observation without pretending it proves display.
	void reportGPUCompletion(PresentationAttempt attempt, bool success) {
		static_cast<void>(document_->reportGPUCompletion(attempt, success));
	}

	/// @brief Injects native presentation time; zero/unknown evidence keeps geometry gated.
	void reportPresentation(PresentationAttempt attempt, double timestamp) {
		static_cast<void>(document_->reportPresentation(attempt, timestamp));
	}

	/// @brief Simulates a submission deadline without creating positive display evidence.
	void abandonPresentation(PresentationAttempt attempt) {
		document_->abandonPresentation(attempt);
	}

	void flush() {
		detail::ReactiveRuntime::current().flush();
		document_->commit();
	}

	/// @brief Permanently disposes this host document; subsequent handles become stale.
	void close() { document_->close(); }

	/// @brief Returns value snapshots from the last complete document commit.
	/// Query again after changes; retaining a snapshot does not keep its node alive.
	[[nodiscard]] auto nodes() const -> std::vector<NodeSnapshot> { return document_->snapshots(); }

	/// @brief Returns cumulative work counters for detecting unwanted remounts or extra commits.
	[[nodiscard]] auto metrics() const -> Metrics { return document_->metrics(); }

	/// @brief Returns reported recoverable errors, including rejected keyed updates.
	[[nodiscard]] auto errors() const -> std::vector<Error> { return document_->errors(); }

	/// @brief Shares the last complete immutable paint scene, or null before publication.
	[[nodiscard]] auto scene() const -> std::shared_ptr<const SceneSnapshot> {
		return document_->scene();
	}

	/// @brief Creates a non-owning capability for an occurrence in this host document.
	[[nodiscard]] auto handle(NodeId identity) const -> NodeHandle {
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
	void scroll(Point point, double delta) { document_->scroll(point, delta); }

	/// @brief Advances deterministic task wakeups without waiting for wall-clock time.
	void advanceTime(std::chrono::milliseconds elapsed) {
		detail::TaskScheduler::current().advance(elapsed);
		flush();
	}

	/// @brief Reflows the document for a new logical viewport without opening a native window.
	void resize(Size size) { document_->resize(size); }

	/// @brief Republishes layout and paint for new document-wide appearance/direction inputs.
	void setEnvironment(Environment environment) { document_->setEnvironment(environment); }

  private:
	static constexpr double defaultWidth = 800;
	static constexpr double defaultHeight = 600;
	std::shared_ptr<detail::Document>
		document_; ///< Owns the same retained engine used by native windows.
	std::uint64_t presentationEpoch_ = detail::nextIdentity(); ///< Synthetic surface generation.
};
} // namespace stana
