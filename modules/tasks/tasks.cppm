module;

#include <chrono>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>

export module stana:tasks;
export import :reactive;
import :primitives;

export namespace stana::tasks {
namespace detail {
/// @brief Routes task wakeups to the native UI queue or a deterministic test clock.
/// Application installs dispatcher before mounting. Without a native dispatcher,
/// TestHost::advanceTime drives delayed callbacks; no background thread is created.
struct TaskScheduler {
	std::function<void(std::chrono::milliseconds, std::function<void()>)>
		dispatcher{};						 ///< Optional native UI timer provider.
	std::chrono::milliseconds logicalTime{}; ///< Test-only monotonic elapsed time.
	std::map<std::pair<std::chrono::milliseconds, std::uint64_t>, std::function<void()>>
		timers{}; ///< Ordered deterministic wakeups.

	[[nodiscard]] static auto current() -> TaskScheduler & {
		static thread_local TaskScheduler scheduler;
		return scheduler;
	}

	/// @brief Schedules an owning callback without sleeping or blocking the UI executor.
	void post(std::chrono::milliseconds delay, std::function<void()> callback) {
		if (delay.count() < 0) {
			throw std::invalid_argument("Task delay must be nonnegative");
		}
		if (dispatcher) {
			dispatcher(delay, std::move(callback));
			return;
		}
		if (delay > std::chrono::milliseconds::max() - logicalTime) {
			throw std::overflow_error("Task clock exhausted");
		}
		timers.emplace(
			std::pair{logicalTime + delay, ::stana::detail::nextIdentity()},
			std::move(callback)
		);
	}

	/// @brief Advances only deterministic timers; callbacks run in deadline/insertion order.
	void advance(std::chrono::milliseconds elapsed) {
		if (dispatcher) {
			throw std::logic_error("Cannot advance a native task clock");
		}
		if (elapsed.count() < 0 || elapsed > std::chrono::milliseconds::max() - logicalTime) {
			throw std::invalid_argument("Invalid task clock advance");
		}
		logicalTime += elapsed;
		constexpr std::size_t iterationLimit = 10000;
		std::size_t iterations = 0;
		while (!timers.empty() && timers.begin()->first.first <= logicalTime) {
			if (++iterations > iterationLimit) {
				throw std::logic_error("Task timers did not settle");
			}
			auto callback = std::move(timers.begin()->second);
			timers.erase(timers.begin());
			callback();
		}
	}
};

/// @brief Owns a suspended coroutine frame until scope cancellation or completion.
/// Timer callbacks keep weak references, so cancellation destroys suspended frames
/// without leaving a future callback able to resume freed memory.
struct TaskState {
	std::coroutine_handle<> coroutine{}; ///< Owning frame; destroyed exactly once by this state.
	bool isCancelled = false;			 ///< Blocks all future scheduled resumptions.
	std::exception_ptr failure{};		 ///< Exception captured by the coroutine promise, if any.
	std::function<bool()>
		isScopeAlive{};					///< Weak owner-liveness check, avoiding a scope/task cycle.
	std::function<void()> onComplete{}; ///< Unregisters the completed task from its owning scope.
	std::function<void(std::exception_ptr)>
		onError{}; ///< Application error callback, invoked only for a live owner.
	TaskState() = default;
	TaskState(const TaskState &) = delete;
	TaskState(TaskState &&) = delete;
	auto operator=(const TaskState &) -> TaskState & = delete;
	auto operator=(TaskState &&) -> TaskState & = delete;

	~TaskState() {
		try {
			if (coroutine) {
				coroutine.destroy();
			}
		} catch (...) {
			std::terminate();
		}
	}

	/// @brief Runs one continuation as a synchronous transaction, ending the batch at suspension.
	void resume() {
		if (isCancelled || !isScopeAlive() || !coroutine || coroutine.done()) {
			return;
		}
		::stana::reactive::Transaction::perform([this] -> void {
			coroutine.resume();
		});
		if (coroutine.done()) {
			const auto error = failure;
			const auto errorHandler = onError;
			auto completion = std::exchange(onComplete, {});
			if (completion) {
				completion();
			}
			if (error && isScopeAlive()) {
				errorHandler(error);
			}
		}
	}
};
} // namespace detail

/// @brief A lazy void coroutine whose continuations belong to a mounted Scope.
///
/// Write a named coroutine taking owned state handles by value, then pass its
/// returned Task to ComponentContext::task(). The task starts after publication,
/// batches writes until suspension, and cancels when the occurrence is removed.
/// Supply an explicit error handler; exceptions do not escape the coroutine frame.
///
/// @note A coroutine lambda does not extend the lifetime of its closure. Prefer
/// named functions with value parameters; never borrow a temporary description.
class Task {
  public:
	/// @brief Coroutine protocol used by the compiler; application code returns Task instead.
	struct promise_type {
		std::weak_ptr<::stana::tasks::detail::TaskState>
			state{}; ///< Weak backlink to the external owning frame state.

		[[nodiscard]] auto get_return_object() -> Task {
			auto owner = std::make_shared<::stana::tasks::detail::TaskState>();
			owner->coroutine = std::coroutine_handle<promise_type>::from_promise(*this);
			state = owner;
			return Task{std::move(owner)};
		}

		[[nodiscard]] auto initial_suspend() const noexcept -> std::suspend_always { return {}; }

		[[nodiscard]] auto final_suspend() const noexcept -> std::suspend_always { return {}; }

		void return_void() const noexcept {}

		void unhandled_exception() const noexcept {
			if (const auto owner = state.lock()) {
				owner->failure = std::current_exception();
			}
		}
	};

	Task(const Task &) = delete;
	auto operator=(const Task &) -> Task & = delete;
	Task(Task &&) noexcept = default;
	auto operator=(Task &&) noexcept -> Task & = default;
	~Task() = default;

	/// @brief Transfers the coroutine into a scope and queues its initial continuation.
	/// @param onError Required callback for failures while the owning scope is alive.
	void start(
		const ::stana::reactive::Scope &scope,
		std::function<void(std::exception_ptr)> onError
	) && {
		if (!onError) {
			throw std::invalid_argument("Task requires an error handler");
		}
		if (::stana::reactive::detail::ReactiveRuntime::current().collector != nullptr) {
			throw std::logic_error("Cannot start a task inside a computed evaluation");
		}
		if (!state_ || !scope.alive()) {
			state_.reset();
			return;
		}
		auto owner = std::exchange(state_, {});
		owner->isScopeAlive = ::stana::reactive::detail::ScopeAccess::liveness(scope);
		owner->onError = std::move(onError);
		const auto token = scope.own([owner] -> void {
			owner->isCancelled = true;
			owner->onComplete = {};
			owner->onError = {};
		});
		owner->onComplete = ::stana::reactive::detail::ScopeAccess::completion(scope, token);
		const auto weakOwner = std::weak_ptr{owner};
		::stana::reactive::detail::ReactiveRuntime::current().effects.emplace_back(
			[weakOwner] -> void {
				if (const auto current = weakOwner.lock()) {
					current->resume();
				}
			}
		);
		::stana::reactive::detail::ReactiveRuntime::current().flush();
	}

  private:
	explicit Task(std::shared_ptr<::stana::tasks::detail::TaskState> state)
		: state_(std::move(state)) {}

	std::shared_ptr<::stana::tasks::detail::TaskState>
		state_{}; ///< Owns the unstarted frame until moved into a scope.
};

/// @brief Awaitable UI-executor delay; never sleeps or creates a thread.
/// Native applications use the main queue; TestHost advances the same contract
/// with explicit time. The wakeup cannot resume a cancelled or disposed task.
struct TaskDelay {
	std::chrono::milliseconds duration{}; ///< Nonnegative delay relative to the current task clock.

	[[nodiscard]] auto await_ready() const noexcept -> bool { return duration.count() == 0; }

	void await_suspend(std::coroutine_handle<Task::promise_type> coroutine) const {
		const auto weakOwner = coroutine.promise().state;
		::stana::tasks::detail::TaskScheduler::current().post(duration, [weakOwner] -> void {
			if (const auto owner = weakOwner.lock()) {
				owner->resume();
			}
		});
	}

	void await_resume() const noexcept {}
};

/// @brief Creates an awaitable nonnegative delay for a scope-owned Task.
[[nodiscard]] inline auto sleepFor(std::chrono::milliseconds duration) -> TaskDelay {
	if (duration.count() < 0) {
		throw std::invalid_argument("Task delay must be nonnegative");
	}
	return {.duration = duration};
}
} // namespace stana::tasks
