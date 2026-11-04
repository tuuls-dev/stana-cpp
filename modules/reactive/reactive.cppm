module;

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module stana.reactive;
import stana.primitives;

export namespace stana {
namespace detail {
struct ScopeAccess;
// The supported system libc++ does not yet provide C++23 move_only_function.
// Erasure is confined to retained callbacks, not component composition.
template <typename Signature> class MoveOnlyFunction;

template <typename R, typename... Args> class MoveOnlyFunction<R(Args...)> {
	struct Callable {
		Callable() = default;
		Callable(const Callable &) = delete;
		Callable(Callable &&) = delete;
		auto operator=(const Callable &) -> Callable & = delete;
		auto operator=(Callable &&) -> Callable & = delete;
		virtual ~Callable() = default;
		virtual auto invoke(Args... args) -> R = 0;
	};

	template <typename F> struct Model final : Callable {
		F operation;

		explicit Model(F value) : operation(std::move(value)) {}

		auto invoke(Args... args) -> R override {
			return std::invoke(operation, std::forward<Args>(args)...);
		}
	};

	std::unique_ptr<Callable> callable_;

  public:
	template <typename F>
		requires(!std::same_as<std::remove_cvref_t<F>, MoveOnlyFunction>)
	MoveOnlyFunction(F &&operation)
		: callable_(std::make_unique<Model<std::decay_t<F>>>(std::forward<F>(operation))) {}

	MoveOnlyFunction(const MoveOnlyFunction &) = delete;
	auto operator=(const MoveOnlyFunction &) -> MoveOnlyFunction & = delete;
	MoveOnlyFunction(MoveOnlyFunction &&) noexcept = default;
	auto operator=(MoveOnlyFunction &&) noexcept -> MoveOnlyFunction & = default;
	~MoveOnlyFunction() = default;

	auto operator()(Args... args) -> R { return callable_->invoke(std::forward<Args>(args)...); }
};

struct Source;

/// @brief Retained graph consumer used by computed values and property subscriptions.
/// Sources keep weak references to observers; callback closures also capture weak
/// state, preventing ownership cycles between graph nodes and their reverse edges.
struct Observer {
	bool active = true;		 ///< False after cancellation; pending queue entries then become inert.
	bool queued = false;	 ///< Prevents multiple pending entries for the same observation.
	bool structural = false; ///< Prioritizes branch/list delivery before ordinary leaf assignments.
	std::function<void()>
		onInvalidate{}; ///< Weak-state invalidation callback installed by the concrete consumer.
	std::function<void()>
		onPrepare{}; ///< Demands a binding value without running its application delivery.
	std::function<void()> onDeliver{}; ///< Delivers the previously prepared binding value.
	std::function<void()>
		onCancel{}; ///< Disconnects active dependency edges when subscription ownership ends.

	void invalidate() {
		if (onInvalidate) {
			onInvalidate();
		}
	}

	void prepare() {
		if (onPrepare) {
			onPrepare();
		}
	}

	void deliver() {
		if (onDeliver) {
			onDeliver();
		}
	}

	void cancel() noexcept {
		active = false;
		try {
			if (onCancel) {
				onCancel();
			}
		} catch (...) {
			std::terminate();
		}
	}
};

/// @brief Per-UI-executor transaction scheduler shared by reactive nodes and documents.
/// Signal writes enqueue observations; outermost completion demands all binding
/// values before delivery, publishes registered documents, then runs mount effects.
struct ReactiveRuntime {
	std::size_t depth =
		0; ///< Nested synchronous transaction depth; only the outermost exit flushes.
	bool flushing =
		false; ///< Prevents recursive flush entry while callbacks enqueue additional work.
	std::vector<std::shared_ptr<Source>> *collector = nullptr;
	std::deque<std::weak_ptr<Observer>>
		pending; ///< Weak observations waiting for the next prepare/deliver batch.
	std::deque<detail::MoveOnlyFunction<void()>> effects;
	std::map<std::uint64_t, std::function<void()>> commits;

	[[nodiscard]] static auto current() -> ReactiveRuntime & {
		static thread_local ReactiveRuntime runtime;
		return runtime;
	}

	void enqueue(const std::shared_ptr<Observer> &observer) {
		if (observer->active && !observer->queued) {
			observer->queued = true;
			pending.emplace_back(observer);
		}
	}

	void flush() {
		if (depth != 0 || flushing) {
			return;
		}
		flushing = true;
		try {
			std::size_t iterations = 0;
			bool again = true;
			while (again) {
				while (!pending.empty()) {
					constexpr std::size_t iterationLimit = 10000;
					if (++iterations > iterationLimit) {
						throw std::logic_error("Stana reactive update did not settle");
					}
					auto batch = std::move(pending);
					pending.clear();
					// Demand every binding before invoking any application callback.
					std::vector<std::shared_ptr<Observer>> ready;
					for (const auto &weak : batch) {
						if (auto observer = weak.lock(); observer && observer->active) {
							observer->queued = false;
							observer->prepare();
							ready.push_back(std::move(observer));
						}
					}
					std::ranges::stable_sort(ready, {}, [](const auto &observer) -> auto {
						return !observer->structural;
					});
					for (const auto &observer : ready) {
						if (observer->active) {
							observer->deliver();
						}
					}
				}
				const auto callbacks = commits;
				for (const auto &[id, callback] : callbacks) {
					if (commits.contains(id)) {
						callback();
					}
				}
				// A native measure provider can enqueue bindings during document
				// preparation. Settle those and publish before exposing mount effects.
				if (!pending.empty()) {
					again = true;
					continue;
				}
				auto actions = std::move(effects);
				effects.clear();
				for (auto &action : actions) {
					action();
				}
				again = !pending.empty() || !effects.empty();
			}
			flushing = false;
		} catch (...) {
			flushing = false;
			throw;
		}
	}
};

/// @brief Executor-confined reactive dependency with weak reverse edges.
/// SignalState and ComputedState inherit this internal graph role. A read records
/// the source with the current dependency collector; writes invalidate observers.
struct Source {
	std::thread::id owner =
		std::this_thread::get_id(); ///< Immutable creating-executor identity checked before source
									///< access. ///< Executor thread recorded at source
									///< construction; reads/writes validate it.
	std::vector<std::weak_ptr<Observer>>
		observers; ///< Weak reverse edges so a source never extends consumer lifetimes.
	Source() = default;
	Source(const Source &) = delete;
	Source(Source &&) = delete;
	auto operator=(const Source &) -> Source & = delete;
	auto operator=(Source &&) -> Source & = delete;
	virtual ~Source() = default;

	void checkThread() const {
		if (owner != std::this_thread::get_id()) {
			throw std::logic_error("Stana reactive state accessed on a different UI executor");
		}
	}

	void remove(const Observer *observer) {
		std::erase_if(observers, [observer](const auto &weak) -> auto {
			const auto value = weak.lock();
			return !value || value.get() == observer;
		});
	}

	void changed() {
		checkThread();
		// Reverse edges are weak so sources never retain their consumers. Snapshot
		// delivery permits observers to detach safely while invalidating.
		const auto snapshot = observers;
		std::erase_if(observers, [](const auto &weak) -> auto {
			return weak.expired();
		});
		for (const auto &weak : snapshot) {
			if (const auto observer = weak.lock(); observer && observer->active) {
				observer->invalidate();
			}
		}
	}
};

inline void track(const std::shared_ptr<Source> &source) {
	if (auto *collector = ReactiveRuntime::current().collector;
		collector != nullptr && std::ranges::find(*collector, source) == collector->end()) {
		collector->push_back(source);
	}
}

/// @brief Stack-scoped replacement for the current reactive dependency collector.
/// Computed evaluation and observation preparation use it so nested reads attach
/// to the correct consumer and exception unwinding restores the previous collector.
class CollectDependencies {
  public:
	explicit CollectDependencies(std::vector<std::shared_ptr<Source>> &dependencies)
		: previous_(std::exchange(ReactiveRuntime::current().collector, &dependencies)) {}

	CollectDependencies(const CollectDependencies &) = delete;
	CollectDependencies(CollectDependencies &&) = delete;
	auto operator=(const CollectDependencies &) -> CollectDependencies & = delete;
	auto operator=(CollectDependencies &&) -> CollectDependencies & = delete;

	~CollectDependencies() { ReactiveRuntime::current().collector = previous_; }

  private:
	std::vector<std::shared_ptr<Source>>
		*previous_; ///< Non-owning collector restored when nested evaluation or unwinding finishes.
};

template <typename T> /// Shared storage behind Signal<T> handles, owned independently of UI nodes.
/// @brief The comparator defines whether a write invalidates dependents. Its owner thread
/// is fixed at construction and every read/write must use that executor.
struct SignalState final : Source {
	T value;
	std::function<bool(const T &, const T &)> equals;

	SignalState(T initial, std::function<bool(const T &, const T &)> comparator)
		: value(std::move(initial)),
		  equals(std::move(comparator)) {}
};

template <typename T>
/// @brief Lazy cached derivation and its last successful dependency set.
/// Invalidation marks the cache dirty and propagates through weak reverse edges;
/// read() reevaluates on demand, replaces dependencies and rejects evaluation cycles.
struct ComputedState final : Source, Observer, std::enable_shared_from_this<ComputedState<T>> {
	detail::MoveOnlyFunction<T()>
		evaluate; ///< Owned pure evaluation operation; invoked only when this cache is dirty.
	std::optional<T>
		value; ///< Last successful computed result, absent before the first evaluation.
	std::vector<std::shared_ptr<Source>> dependencies;
	bool dirty = true; ///< Whether an upstream change requires recomputing the cached result.
	bool evaluating =
		false; ///< Detects actual recursive evaluation rather than obsolete dependency edges.

	explicit ComputedState(detail::MoveOnlyFunction<T()> operation)
		: evaluate(std::move(operation)) {}

	void markDirty() {
		if (!dirty) {
			dirty = true;
			changed();
		}
	}

	[[nodiscard]] auto read() -> T {
		checkThread();
		if (evaluating) {
			throw std::logic_error("Stana computed dependency cycle");
		}
		if (dirty) {
			evaluating = true;
			std::vector<std::shared_ptr<Source>> next;
			try {
				const CollectDependencies collecting{next};
				auto result = evaluate();
				for (const auto &source : dependencies) {
					source->remove(this);
				}
				dependencies = std::move(next);
				for (const auto &source : dependencies) {
					source->observers.emplace_back(this->shared_from_this());
				}
				value = std::move(result);
				dirty = false;
				evaluating = false;
			} catch (...) {
				evaluating = false;
				throw;
			}
		}
		if (!value.has_value()) {
			throw std::logic_error("Stana computed has no evaluated value");
		}
		return value.value();
	}
};
} // namespace detail

/// @brief Owns the lifetime of one reactive observation.
///
/// Keep the value returned by observe(), or transfer it to Scope::own(). Dropping
/// it immediately disconnects the observation. Moving transfers cancellation
/// ownership; cancel() is idempotent and prevents already-queued deliveries.
class Subscription {
  public:
	Subscription() = default;

	explicit Subscription(std::shared_ptr<detail::Observer> observer)
		: observer_(std::move(observer)) {}

	Subscription(const Subscription &) = delete;
	auto operator=(const Subscription &) -> Subscription & = delete;
	Subscription(Subscription &&) noexcept = default;

	auto operator=(Subscription &&other) noexcept -> Subscription & {
		if (this != &other) {
			cancel();
			observer_ = std::move(other.observer_);
		}
		return *this;
	}

	~Subscription() { cancel(); }

	void cancel() noexcept {
		if (observer_) {
			observer_->cancel();
			observer_.reset();
		}
	}

  private:
	std::shared_ptr<detail::Observer> observer_; ///< Owns the consumer until cancellation; sources
												 ///< retain only weak reverse edges.
};

/// @brief Synchronous UI-executor batch. Source writes persist even if the operation throws.
class Transaction {
  public:
	template <std::invocable F> static void perform(F &&operation) {
		auto &runtime = detail::ReactiveRuntime::current();
		if (runtime.collector != nullptr) {
			throw std::logic_error("Stana cannot mutate inside a computed evaluation");
		}
		++runtime.depth;
		try {
			std::invoke(std::forward<F>(operation));
		} catch (...) {
			--runtime.depth;
			runtime.flush();
			throw;
		}
		--runtime.depth;
		runtime.flush();
	}
};

template <typename T> class Computed;
template <typename T> class Binding;

/// @brief Shares one typed mutable value on its creating UI executor.
///
/// Capture this lightweight handle by value in retained actions. Pass the handle
/// itself to Property<T> for live updates; get() reads a snapshot unless evaluated
/// inside a computed or observation. set() commits synchronously outside a batch,
/// while Transaction::perform coalesces delivery. Equal writes do not invalidate
/// dependents. Use Signal<std::string> for owned text rather than a character pointer.
/// @note Reads and writes on a different executor are diagnosed logic errors.
template <typename T> class Signal {
  public:
	explicit Signal(T initial)
		requires std::equality_comparable<T>
		: Signal(std::move(initial), std::equal_to<T>{}) {}

	template <typename Equal>
	Signal(T initial, Equal comparator)
		: state_(
			  std::make_shared<detail::SignalState<T>>(std::move(initial), std::move(comparator))
		  ) {}

	[[nodiscard]] auto get() const -> T {
		state_->checkThread();
		detail::track(state_);
		return state_->value;
	}

	void set(T value) const {
		state_->checkThread();
		Transaction::perform([&] -> auto {
			if (!state_->equals(state_->value, value)) {
				state_->value = std::move(value);
				state_->changed();
			}
		});
	}

	template <typename F> void update(F &&operation) const {
		set(std::invoke(std::forward<F>(operation), get()));
	}

	template <typename F> [[nodiscard]] auto map(F operation) const;
	[[nodiscard]] auto binding() const -> Binding<T>;

	auto operator++() const -> const Signal &
		requires requires(T value) { ++value; }
	{
		update([](T value) -> auto {
			return ++value;
		});
		return *this;
	}

  private:
	std::shared_ptr<detail::SignalState<T>>
		state_; ///< Shared mutable source storage; copied Signal handles observe the same value.
};

/// @brief Shares a lazy cached derivation of other reactive values.
///
/// Create with computed(lambda) or Signal::map(), capturing source handles by
/// value. get() demands current dependencies and tracks actual reads, so changing
/// a conditional dependency disconnects obsolete sources. Evaluation must remain
/// pure: signal writes, transactions and task creation inside it are rejected.
/// Mounted property observations compare output values before assigning nodes.
template <typename T> class Computed {
  public:
	template <typename F>
	explicit Computed(F operation)
		: state_(std::make_shared<detail::ComputedState<T>>(std::move(operation))) {
		const auto weakState = std::weak_ptr{state_};
		state_->onInvalidate = [weakState] -> void {
			if (const auto state = weakState.lock()) {
				state->markDirty();
			}
		};
	}

	[[nodiscard]] auto get() const -> T {
		auto value = state_->read();
		detail::track(state_);
		return value;
	}

	template <typename F> [[nodiscard]] auto map(F operation) const {
		return Computed<std::remove_cvref_t<std::invoke_result_t<F, T>>>{
			[source = *this, transform = std::move(operation)] -> auto {
				return std::invoke(transform, source.get());
			}
		};
	}

  private:
	std::shared_ptr<detail::ComputedState<T>>
		state_; ///< Shared lazy cache and dependency graph for all copies of this computed.
};

template <std::invocable F>
	requires std::copy_constructible<std::remove_cvref_t<std::invoke_result_t<F>>>
[[nodiscard]] auto computed(F operation) {
	return Computed<std::remove_cvref_t<std::invoke_result_t<F>>>{std::move(operation)};
}

template <typename T> template <typename F> auto Signal<T>::map(F operation) const {
	return computed([source = *this, transform = std::move(operation)] -> auto {
		return std::invoke(transform, source.get());
	});
}

/// @brief Grants a control explicit read/write access to a typed model value.
///
/// Obtain with Signal::binding() and pass to writable controls such as Toggle.
/// Read-only display inputs accept Property<T> instead; a Binding is deliberately
/// not implicitly convertible to Property, preventing accidental capability mixing.
template <typename T> class Binding {
  public:
	explicit Binding(Signal<T> signal) : source_(std::move(signal)) {}

	[[nodiscard]] auto get() const -> T { return source_.get(); }

	void set(T value) const { source_.set(std::move(value)); }

  private:
	Signal<T> source_; ///< Model source to which this explicit writable capability delegates.
};

template <typename T> auto Signal<T>::binding() const -> Binding<T> {
	return Binding<T>{*this};
}

/// @brief Normalizes a constant, Signal<T> or Computed<T> into a read-only input.
///
/// Component fields use fixed Property types so leaf controls need not become
/// templates for every closure/source. Construction owns its constant or source
/// handle but installs no subscription. Mounting observes the input in a node
/// scope; copying a reusable property group does not mount or subscribe anything.
template <typename T> class Property {
  public:
	Property()
		requires std::default_initializable<T>
		: source_(T{}) {}

	Property(T value) : source_(std::move(value)) {}

	template <typename U>
		requires std::constructible_from<T, U> && (!std::same_as<std::remove_cvref_t<U>, T>)
	Property(U &&value) : source_(std::in_place_type<T>, std::forward<U>(value)) {}

	Property(Signal<T> signal) : source_(std::move(signal)) {}

	Property(Computed<T> value) : source_(std::move(value)) {}

	[[nodiscard]] auto get() const -> T {
		return std::visit(
			[](const auto &value) -> T {
				if constexpr (std::same_as<std::decay_t<decltype(value)>, T>) {
					return value;
				} else {
					return value.get();
				}
			},
			source_
		);
	}

  private:
	std::variant<T, Signal<T>, Computed<T>>
		source_; ///< Owns either a constant value or a typed reactive source handle.
};

namespace detail {
template <typename T>
/// @brief Mount-owned subscription that normalizes a Property into retained assignments.
/// prepareValue() samples dependencies before any callbacks run; deliverValue()
/// compares against the previous delivery. Subscription cancellation disconnects it.
struct Observation final : Observer, std::enable_shared_from_this<Observation<T>> {
	Property<T> source; ///< Normalized constant/reactive input read during observation preparation.
	detail::MoveOnlyFunction<void(const T &)>
		callback; ///< Retained assignment/action invoked only after batch values are demanded.
	std::vector<std::shared_ptr<Source>> dependencies;
	std::optional<T>
		delivered; ///< Previous delivered output used to skip equal retained assignments.
	std::optional<T> prepared; ///< Next demanded output, held until this batch enters delivery.

	Observation(Property<T> value, detail::MoveOnlyFunction<void(const T &)> action)
		: source(std::move(value)),
		  callback(std::move(action)) {}

	void enqueue() { ReactiveRuntime::current().enqueue(this->shared_from_this()); }

	void prepareValue() {
		std::vector<std::shared_ptr<Source>> next;
		const CollectDependencies collecting{next};
		prepared = source.get();
		for (const auto &dependency : dependencies) {
			dependency->remove(this);
		}
		dependencies = std::move(next);
		for (const auto &dependency : dependencies) {
			dependency->observers.emplace_back(this->shared_from_this());
		}
	}

	[[nodiscard]] auto isEqualToDelivered() const -> bool {
		if constexpr (std::equality_comparable<T>) {
			return delivered.has_value() && prepared.has_value() &&
				std::equal_to<T>{}(*delivered, *prepared);
		}
		return false;
	}

	void deliverValue() {
		if (prepared && !isEqualToDelivered()) {
			delivered = std::move(prepared);
			prepared.reset();
			callback(delivered.value());
		}
	}

	void disconnect() {
		active = false;
		for (const auto &dependency : dependencies) {
			dependency->remove(this);
		}
		dependencies.clear();
	}
};
} // namespace detail

template <typename T, typename F>
[[nodiscard]] auto observe(Property<T> source, F callback, bool structural = false)
	-> Subscription {
	auto observation =
		std::make_shared<detail::Observation<T>>(std::move(source), std::move(callback));
	observation->structural = structural;
	const auto weakObservation = std::weak_ptr{observation};
	observation->onInvalidate = [weakObservation] -> void {
		if (const auto state = weakObservation.lock()) {
			state->enqueue();
		}
	};
	observation->onPrepare = [weakObservation] -> void {
		if (const auto state = weakObservation.lock()) {
			state->prepareValue();
		}
	};
	observation->onDeliver = [weakObservation] -> void {
		if (const auto state = weakObservation.lock()) {
			state->deliverValue();
		}
	};
	observation->onCancel = [weakObservation] -> void {
		if (const auto state = weakObservation.lock()) {
			state->disconnect();
		}
	};
	detail::ReactiveRuntime::current().enqueue(observation);
	detail::ReactiveRuntime::current().flush();
	return Subscription{std::move(observation)};
}

template <typename T, typename F>
[[nodiscard]] auto observe(Signal<T> source, F callback) -> Subscription {
	return observe(Property<T>{std::move(source)}, std::move(callback));
}

template <typename T, typename F>
[[nodiscard]] auto observe(Computed<T> source, F callback) -> Subscription {
	return observe(Property<T>{std::move(source)}, std::move(callback));
}

/// @brief Observes an explicit source and runs its action after document publication.
///
/// Unlike a computed, the action may perform I/O or write signals. Dependency
/// selection is explicit in source; callback reads do not create hidden edges.
/// Keep the returned Subscription or transfer it to a Scope. Cancelling also
/// invalidates actions queued for a publication that has not yet completed.
template <typename T, typename F>
[[nodiscard]] auto effect(Property<T> source, F operation) -> Subscription {
	const auto isAlive = std::make_shared<bool>(true);
	const auto action = std::make_shared<F>(std::move(operation));
	const auto owner = std::make_shared<detail::Observer>();
	const auto observation = std::make_shared<Subscription>(
		observe(std::move(source), [isAlive, action](const T &value) -> void {
			detail::ReactiveRuntime::current().effects.emplace_back(
				[isAlive, action, captured = value] -> void {
					if (*isAlive) {
						std::invoke(*action, captured);
					}
				}
			);
		})
	);
	owner->onCancel = [isAlive, observation] -> void {
		*isAlive = false;
		observation->cancel();
	};
	return Subscription{owner};
}

template <typename T, typename F>
[[nodiscard]] auto effect(Signal<T> source, F operation) -> Subscription {
	return effect(Property<T>{std::move(source)}, std::move(operation));
}

template <typename T, typename F>
[[nodiscard]] auto effect(Computed<T> source, F operation) -> Subscription {
	return effect(Property<T>{std::move(source)}, std::move(operation));
}

/// @brief Shares the lifetime of one mounted component occurrence and its resources.
///
/// The retained node owns a Scope; ComponentContext transfers subscriptions,
/// external cleanup and tasks into it. dispose() marks the scope dead first, then
/// invokes a detached registration snapshot in insertion order. Reentrant cleanup
/// cannot alter that snapshot or make an obsolete request token current again.
/// Copies share ownership, so occurrence removal explicitly calls dispose().
/// @note Cleanup callbacks must not throw. All operations belong to the UI executor.
class Scope {
	/// @brief Shared control block for ordered cleanup and request freshness.
	/// Scope handles own this block; task completion captures weak adapters instead
	/// of retaining its own owner. The final owner also disposes remaining resources.
	struct State {
		State() = default;
		State(const State &) = delete;
		State(State &&) = delete;
		auto operator=(const State &) -> State & = delete;
		auto operator=(State &&) -> State & = delete;
		std::uint64_t identity = detail::nextIdentity();
		std::uint64_t revision = 0;
		bool alive = true;
		std::map<std::uint64_t, detail::MoveOnlyFunction<void()>> resources;

		void dispose() {
			if (!std::exchange(alive, false)) {
				return;
			}
			auto snapshot = std::move(resources);
			resources.clear();
			for (auto &[token, cleanup] : snapshot) {
				cleanup();
			}
		}

		~State() { dispose(); }
	};

  public:
	/// @brief A scope identity plus request generation used to reject obsolete async results.
	/// Keep the token returned by beginRequest(), then check isCurrent() before writing
	/// results after suspension. Cancellation by itself does not establish freshness.
	struct RequestToken {
		std::uint64_t scope = 0;	///< Never-reused identity of the originating scope.
		std::uint64_t revision = 0; ///< Latest-request generation at token creation.
	};

	Scope() : state_(std::make_shared<State>()) {}

	[[nodiscard]] auto alive() const noexcept -> bool { return state_->alive; }

	void dispose() const { state_->dispose(); }

	template <typename F> [[nodiscard]] auto own(F cleanup) const -> std::uint64_t {
		const auto token = detail::nextIdentity();
		if (alive()) {
			state_->resources.emplace(token, std::move(cleanup));
		} else {
			cleanup();
		}
		return token;
	}

	[[nodiscard]] auto own(Subscription subscription) const -> std::uint64_t {
		return own([owned = std::move(subscription)]() mutable -> void {
			owned.cancel();
		});
	}

	void disown(std::uint64_t token) const { state_->resources.erase(token); }

	[[nodiscard]] auto beginRequest() const -> RequestToken {
		state_->revision = detail::nextIdentity();
		return {.scope = state_->identity, .revision = state_->revision};
	}

	[[nodiscard]] auto isCurrent(RequestToken token) const noexcept -> bool {
		return alive() && token.scope == state_->identity && token.revision == state_->revision;
	}

  private:
	friend struct detail::ScopeAccess;
	std::shared_ptr<State>
		state_; ///< Shared lifetime control block; explicit disposal ends every copied capability.
};

namespace detail {
/// @brief Weak lifetime adapters used by the task scheduler without retaining a scope.
/// Keeping a Scope handle inside its own task completion closure would form an
/// ownership cycle; these adapters allow completion to unregister safely instead.
struct ScopeAccess {
	[[nodiscard]] static auto liveness(const Scope &scope) -> std::function<bool()> {
		const auto weakOwner = std::weak_ptr{scope.state_};
		return [weakOwner] -> bool {
			const auto owner = weakOwner.lock();
			return owner && owner->alive;
		};
	}

	[[nodiscard]] static auto completion(const Scope &scope, std::uint64_t token)
		-> std::function<void()> {
		const auto weakOwner = std::weak_ptr{scope.state_};
		return [weakOwner, token] -> void {
			if (const auto owner = weakOwner.lock()) {
				owner->resources.erase(token);
			}
		};
	}
};
} // namespace detail
} // namespace stana
