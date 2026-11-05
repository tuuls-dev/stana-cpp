module;

#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

module stana;
import :reactive;

namespace stana::reactive::detail {
namespace {
struct ExecutorThreadState {
	std::shared_ptr<ExecutorEndpoint> current;

	ExecutorThreadState() = default;
	ExecutorThreadState(const ExecutorThreadState &) = delete;
	ExecutorThreadState(ExecutorThreadState &&) = delete;
	auto operator=(const ExecutorThreadState &) -> ExecutorThreadState & = delete;
	auto operator=(ExecutorThreadState &&) -> ExecutorThreadState & = delete;

	~ExecutorThreadState() {
		if (current) {
			try {
				current->close();
			} catch (...) {
				std::terminate();
			}
		}
	}
};

[[nodiscard]] auto threadState() -> ExecutorThreadState & {
	static thread_local ExecutorThreadState state;
	return state;
}
} // namespace

struct ExecutorEndpoint::Impl {
	mutable std::mutex mutex;
	const std::thread::id owner = std::this_thread::get_id();
	ExecutorSessionKind kind = ExecutorSessionKind::dormant;
	std::deque<std::unique_ptr<ExecutorCommand>> queue;
	std::size_t pending = 0;
	std::size_t headlessLeases = 0;
	std::shared_ptr<const std::function<void()>> notify;
	std::function<void()> teardown;
	bool draining = false;
	bool deferredWakeup = false;
	std::function<void()> admissionBeforeLock;
	std::function<void()> admissionUnderLock;
	std::function<void()> closeBeforeLock;
	std::function<void()> closeUnderLock;
};

ExecutorEndpoint::ExecutorEndpoint() : impl_(std::make_unique<Impl>()) {
}

ExecutorEndpoint::~ExecutorEndpoint() {
	try {
		close();
	} catch (...) {
		std::terminate();
	}
}

auto ExecutorEndpoint::current() -> std::shared_ptr<ExecutorEndpoint> {
	auto &state = threadState();
	if (!state.current || state.current->kind() == ExecutorSessionKind::closed) {
		state.current = std::make_shared<ExecutorEndpoint>();
	}
	return state.current;
}

auto ExecutorEndpoint::currentIfAny() -> std::shared_ptr<ExecutorEndpoint> {
	return threadState().current;
}

auto ExecutorEndpoint::ownerThread() const -> std::thread::id {
	return impl_->owner;
}

auto ExecutorEndpoint::kind() const -> ExecutorSessionKind {
	const std::scoped_lock lock{impl_->mutex};
	return impl_->kind;
}

auto ExecutorEndpoint::pendingWrites() const -> std::size_t {
	const std::scoped_lock lock{impl_->mutex};
	return impl_->pending;
}

void ExecutorEndpoint::setAdmissionProbe(
	std::function<void()> beforeLock,
	std::function<void()> underLock
) {
	const std::scoped_lock lock{impl_->mutex};
	impl_->admissionBeforeLock = std::move(beforeLock);
	impl_->admissionUnderLock = std::move(underLock);
}

void ExecutorEndpoint::setCloseProbe(
	std::function<void()> beforeLock,
	std::function<void()> underLock
) {
	const std::scoped_lock lock{impl_->mutex};
	impl_->closeBeforeLock = std::move(beforeLock);
	impl_->closeUnderLock = std::move(underLock);
}

void ExecutorEndpoint::requestWakeup() {
	std::shared_ptr<const std::function<void()>> notify;
	{
		const std::scoped_lock lock{impl_->mutex};
		if (impl_->kind != ExecutorSessionKind::native) {
			return;
		}
		notify = impl_->notify;
	}
	if (notify && *notify) {
		(*notify)();
	}
}

void ExecutorEndpoint::rearmDeferredWakeup() {
	if (!impl_->deferredWakeup || impl_->draining) {
		return;
	}
	auto &runtime = ReactiveRuntime::current();
	if (runtime.depth != 0 || runtime.flushing || runtime.collector != nullptr) {
		return;
	}
	impl_->deferredWakeup = false;
	requestWakeup();
}

void ExecutorEndpoint::admit(std::unique_ptr<ExecutorCommand> command) {
	if (!command) {
		throw std::logic_error("Stana queued signal write requires a command");
	}
	std::function<void()> beforeLock;
	{
		const std::scoped_lock lock{impl_->mutex};
		beforeLock = impl_->admissionBeforeLock;
	}
	if (beforeLock) {
		beforeLock();
	}
	std::shared_ptr<const std::function<void()>> notify;
	{
		std::unique_lock lock{impl_->mutex};
		if (impl_->admissionUnderLock) {
			impl_->admissionUnderLock();
		}
		if (impl_->kind == ExecutorSessionKind::dormant ||
			impl_->kind == ExecutorSessionKind::closed) {
			lock.unlock();
			command.reset();
			throw std::logic_error("Stana queued signal write requires an active executor session");
		}
		if (impl_->pending >= maximumPendingSignalWrites) {
			lock.unlock();
			command.reset();
			throw std::overflow_error("Stana queued signal write exceeds the pending-write limit");
		}
		impl_->queue.emplace_back();
		impl_->queue.back() = std::move(command);
		++impl_->pending;
		notify = impl_->notify;
	}
	if (notify && *notify) {
		(*notify)();
	}
}

void ExecutorEndpoint::close() {
	std::function<void()> beforeLock;
	{
		const std::scoped_lock lock{impl_->mutex};
		beforeLock = impl_->closeBeforeLock;
	}
	if (beforeLock) {
		beforeLock();
	}
	std::deque<std::unique_ptr<ExecutorCommand>> detached;
	std::function<void()> teardown;
	{
		const std::scoped_lock lock{impl_->mutex};
		if (impl_->closeUnderLock) {
			impl_->closeUnderLock();
		}
		if (impl_->kind == ExecutorSessionKind::closed) {
			return;
		}
		impl_->kind = ExecutorSessionKind::closed;
		detached.swap(impl_->queue);
		impl_->pending -= detached.size();
		teardown = std::move(impl_->teardown);
		impl_->notify = {};
	}
	detached.clear();
	if (teardown) {
		teardown();
	}
}

void ExecutorEndpoint::activateNative(
	std::function<void()> notify,
	std::function<void()> teardown
) {
	if (impl_->owner != std::this_thread::get_id()) {
		throw std::logic_error("Stana native executor sessions belong to the owner thread");
	}
	const std::scoped_lock lock{impl_->mutex};
	if (impl_->kind == ExecutorSessionKind::closed) {
		throw std::logic_error("Stana cannot reopen a closed executor session");
	}
	if (impl_->kind == ExecutorSessionKind::native) {
		throw std::logic_error("Stana cannot start a nested native executor session");
	}
	if (impl_->kind == ExecutorSessionKind::headless) {
		throw std::logic_error("Stana cannot mix native and headless executor sessions");
	}
	auto installed = std::make_shared<std::function<void()>>(std::move(notify));
	impl_->kind = ExecutorSessionKind::native;
	impl_->notify = std::move(installed);
	impl_->teardown = std::move(teardown);
}

void ExecutorEndpoint::acquireHeadless() {
	if (impl_->owner != std::this_thread::get_id()) {
		throw std::logic_error("Stana headless executor sessions belong to the owner thread");
	}
	const std::scoped_lock lock{impl_->mutex};
	if (impl_->kind == ExecutorSessionKind::closed) {
		throw std::logic_error("Stana cannot reopen a closed executor session");
	}
	if (impl_->kind == ExecutorSessionKind::native) {
		throw std::logic_error("Stana cannot mix native and headless executor sessions");
	}
	impl_->kind = ExecutorSessionKind::headless;
	++impl_->headlessLeases;
}

void ExecutorEndpoint::releaseHeadless() {
	bool shouldClose = false;
	{
		const std::scoped_lock lock{impl_->mutex};
		if (impl_->headlessLeases == 0) {
			return;
		}
		--impl_->headlessLeases;
		shouldClose = impl_->headlessLeases == 0 && impl_->kind == ExecutorSessionKind::headless;
	}
	if (shouldClose) {
		close();
	}
}

void ExecutorEndpoint::drain() {
	if (impl_->owner != std::this_thread::get_id()) {
		throw std::logic_error("Stana reactive state accessed on a different UI executor");
	}
	auto &runtime = ReactiveRuntime::current();
	if (impl_->draining || runtime.depth != 0 || runtime.flushing || runtime.collector != nullptr) {
		impl_->deferredWakeup = true;
		return;
	}
	std::deque<std::unique_ptr<ExecutorCommand>> batch;
	{
		const std::scoped_lock lock{impl_->mutex};
		if (impl_->kind == ExecutorSessionKind::closed) {
			return;
		}
		batch.swap(impl_->queue);
	}
	impl_->draining = true;
	std::exception_ptr failure;
	try {
		while (!batch.empty()) {
			{
				const std::scoped_lock lock{impl_->mutex};
				if (impl_->kind == ExecutorSessionKind::closed) {
					break;
				}
			}
			auto command = std::move(batch.front());
			batch.pop_front();
			try {
				command->apply();
			} catch (...) {
				failure = std::current_exception();
			}
			command.reset();
			{
				const std::scoped_lock lock{impl_->mutex};
				if (impl_->pending > 0) {
					--impl_->pending;
				}
			}
			if (failure) {
				break;
			}
		}
		const auto leftover = batch.size();
		batch.clear();
		if (leftover > 0) {
			const std::scoped_lock lock{impl_->mutex};
			impl_->pending -= leftover;
		}
		if (failure) {
			close();
		} else {
			bool needsWake = false;
			{
				const std::scoped_lock lock{impl_->mutex};
				needsWake = !impl_->queue.empty() && impl_->kind == ExecutorSessionKind::native;
			}
			if (needsWake) {
				requestWakeup();
			}
		}
	} catch (...) {
		impl_->draining = false;
		rearmDeferredWakeup();
		throw;
	}
	impl_->draining = false;
	rearmDeferredWakeup();
	if (failure) {
		std::rethrow_exception(failure);
	}
}

HeadlessExecutorLease::HeadlessExecutorLease(std::shared_ptr<ExecutorEndpoint> endpoint)
	: endpoint_(std::move(endpoint)) {
	if (!endpoint_) {
		throw std::logic_error("Stana headless executor lease requires an endpoint");
	}
	endpoint_->acquireHeadless();
}

HeadlessExecutorLease::HeadlessExecutorLease(HeadlessExecutorLease &&other) noexcept
	: endpoint_(std::move(other.endpoint_)) {
}

auto HeadlessExecutorLease::operator=(HeadlessExecutorLease &&other) noexcept
	-> HeadlessExecutorLease & {
	if (this != &other) {
		release();
		endpoint_ = std::move(other.endpoint_);
	}
	return *this;
}

HeadlessExecutorLease::~HeadlessExecutorLease() {
	release();
}

void HeadlessExecutorLease::release() noexcept {
	if (const auto endpoint = std::exchange(endpoint_, {})) {
		try {
			endpoint->releaseHeadless();
		} catch (...) {
			std::terminate();
		}
	}
}

auto acquireHeadlessExecutorLease() -> HeadlessExecutorLease {
	return HeadlessExecutorLease{ExecutorEndpoint::current()};
}

void rearmDeferredExecutorWakeup() {
	if (const auto endpoint = ExecutorEndpoint::currentIfAny()) {
		endpoint->rearmDeferredWakeup();
	}
}
} // namespace stana::reactive::detail
