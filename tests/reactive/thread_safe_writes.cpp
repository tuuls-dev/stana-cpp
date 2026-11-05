#include <algorithm>
#include <atomic>
#include <exception>
#include <future>
#include <iostream>
#include <latch>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import stana;
import stana.testing;

namespace {
using namespace stana;

void require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] auto worker(auto operation) {
	return std::async(std::launch::async, std::move(operation));
}

[[nodiscard]] auto throwsLogic(auto operation) -> bool {
	try {
		operation();
	} catch (const std::logic_error &) {
		return true;
	}
	return false;
}

[[nodiscard]] auto throwsOverflow(auto operation) -> bool {
	try {
		operation();
	} catch (const std::overflow_error &) {
		return true;
	}
	return false;
}

struct Payload {
	std::shared_ptr<int> live;
	int identity = 0;

	Payload(std::shared_ptr<int> counter, int value) : live(std::move(counter)), identity(value) {
		++*live;
	}

	Payload(const Payload &other) : live(other.live), identity(other.identity) {
		if (live) {
			++*live;
		}
	}

	Payload(Payload &&other) noexcept : live(std::move(other.live)), identity(other.identity) {}

	auto operator=(const Payload &other) -> Payload & {
		if (this != &other) {
			if (live) {
				--*live;
			}
			live = other.live;
			identity = other.identity;
			if (live) {
				++*live;
			}
		}
		return *this;
	}

	auto operator=(Payload &&other) noexcept -> Payload & {
		if (this != &other) {
			if (live) {
				--*live;
			}
			live = std::move(other.live);
			identity = other.identity;
		}
		return *this;
	}

	~Payload() {
		if (live) {
			--*live;
		}
	}

	auto operator==(const Payload &other) const -> bool { return identity == other.identity; }
};

[[nodiscard]] auto findKind(const TestHost &host, const std::string &kind) -> NodeSnapshot {
	const auto nodes = host.nodes();
	const auto found = std::ranges::find(nodes, kind, &NodeSnapshot::kind);
	if (found == nodes.end()) {
		throw std::runtime_error("Missing node: " + kind);
	}
	return *found;
}

void apiOrderingAndSettlement() {
	constexpr int first = 1;
	constexpr int second = 2;
	constexpr int third = 3;
	const auto owner = std::this_thread::get_id();

	{
		TestHost host;
		const Signal value{0};
		std::thread::id delivered{};
		const auto subscription = observe(value, [&](int) -> void {
			delivered = std::this_thread::get_id();
		});
		worker([value] -> void {
			value.setThreadSafe(first);
		}).get();
		require(value.get() == 0, "S01 value unchanged until owner pump");
		host.flush();
		require(value.get() == first && delivered == owner, "S01 applied on owner");
	}

	{
		TestHost host;
		const Signal value{0};
		const Signal batched{0};
		int deliveries = 0;
		const auto subscription = observe(batched, [&](int) -> void {
			++deliveries;
		});
		value.setThreadSafe(first);
		require(value.get() == 0, "S02 owner queue defers");
		Transaction::perform([&] -> void {
			value.setThreadSafe(second);
			batched.set(first);
			batched.set(second);
			require(value.get() == 0, "S02 queued write stays deferred in a transaction");
			require(deliveries == 1, "S02 synchronous writes still batch");
		});
		require(value.get() == 0 && deliveries == 2, "S02 mailbox is not an implicit pump");
		host.flush();
		require(value.get() == second, "S02 later queued value applies");
	}

	{
		TestHost host;
		const Signal left{0};
		const Signal right{0};
		std::vector<int> order;
		const auto leftSubscription = observe(left, [&](int value) -> void {
			if (value == first) {
				require(right.get() == 0, "S03 independent settlement");
			}
			if (value != 0) {
				order.push_back(value);
			}
		});
		const auto rightSubscription = observe(right, [&](int value) -> void {
			if (value != 0) {
				order.push_back(value);
			}
		});
		left.setThreadSafe(first);
		right.setThreadSafe(second);
		left.setThreadSafe(third);
		host.flush();
		require(order == std::vector({first, second, third}), "S03 FIFO without coalescing");
	}

	{
		TestHost host;
		constexpr int producers = 4;
		constexpr int writesEach = 8;
		const Signal value{0};
		std::vector<int> applied;
		const auto subscription = observe(value, [&](int next) -> void {
			if (next != 0) {
				applied.push_back(next);
			}
		});
		std::latch start{producers + 1};
		std::vector<std::future<void>> tasks;
		tasks.reserve(static_cast<std::size_t>(producers));
		for (int producer = 0; producer < producers; ++producer) {
			tasks.push_back(worker([value, producer, &start] -> void {
				start.arrive_and_wait();
				for (int index = 0; index < writesEach; ++index) {
					constexpr int stride = 100;
					value.setThreadSafe(producer * stride + index + 1);
				}
			}));
		}
		start.arrive_and_wait();
		for (auto &task : tasks) {
			task.get();
		}
		host.flush();
		require(
			applied.size() ==
				static_cast<std::size_t>(producers) * static_cast<std::size_t>(writesEach),
			"S04 every admitted command applies once"
		);
		for (int producer = 0; producer < producers; ++producer) {
			int previous = 0;
			for (const auto sample : applied) {
				constexpr int stride = 100;
				if (sample / stride == producer) {
					require(sample > previous, "S04 per-producer order");
					previous = sample;
				}
			}
		}
	}

	{
		TestHost host;
		const auto ownerThread = std::this_thread::get_id();
		std::thread::id compared{};
		bool treatEqual = false;
		const Signal value{0, [&](int left, int right) -> bool {
							   compared = std::this_thread::get_id();
							   return treatEqual || left == right;
						   }};
		worker([value] -> void {
			value.setThreadSafe(first);
		}).get();
		require(compared == std::thread::id{}, "S05 equality does not run at enqueue");
		treatEqual = true;
		host.flush();
		require(value.get() == 0 && compared == ownerThread, "S05 equal at delivery is skipped");
		treatEqual = false;
		value.setThreadSafe(second);
		host.flush();
		require(value.get() == second, "S05 unequal at delivery assigns");
	}

	{
		TestHost host;
		const Signal value{0};
		value.setThreadSafe(first);
		value.set(second);
		require(value.get() == second, "S06 synchronous set overtakes the queue");
		host.flush();
		require(value.get() == first, "S06 queued command is not cancelled");
	}

	{
		TestHost host;
		const Signal value{0};
		const auto subscription = observe(value, [&](int next) -> void {
			if (next == first) {
				value.setThreadSafe(second);
			}
		});
		value.setThreadSafe(first);
		host.flush();
		require(value.get() == first, "S07 observer enqueue waits for a later turn");
		host.flush();
		require(value.get() == second, "S07 later turn applies the nested write");
		const Signal nested{0};
		Transaction::perform([&] -> void {
			nested.setThreadSafe(first);
			host.flush();
			require(nested.get() == 0, "S07 nested pump during a transaction defers");
		});
		require(nested.get() == 0, "S07 transaction exit does not drain the mailbox");
		host.flush();
		require(nested.get() == first, "S07 explicit flush after a safe turn applies");
	}

	{
		TestHost host;
		const Signal value{0};
		const auto invalid = computed([&] -> int {
			value.setThreadSafe(first);
			return 0;
		});
		require(
			throwsLogic([&] -> void {
				static_cast<void>(invalid.get());
			}) &&
				value.get() == 0,
			"S08 owner queued mutation during computed evaluation rejects"
		);
		worker([value] -> void {
			value.setThreadSafe(second);
		}).get();
		host.flush();
		require(value.get() == second, "S08 worker admission does not inspect the owner collector");
	}
}

void retainedUi() {
	const Signal label{std::string{"Ready"}};
	TestHost host;
	host.mount(Text{.text = label});
	const auto initial = findKind(host, "text");
	const auto mounts = host.metrics().mounts;
	worker([label] -> void {
		label.setThreadSafe(std::string{"Done"});
	}).get();
	host.flush();
	const auto updated = findKind(host, "text");
	require(updated.id == initial.id && updated.text == "Done", "S09 retained text updates");
	require(host.metrics().mounts == mounts, "S09 no remount");
}

void activationIsolationAndOwnership() {
	constexpr int first = 1;
	constexpr int second = 2;
	constexpr int third = 3;
	const Signal adopted{0};
	require(
		throwsLogic([&] -> void {
			adopted.setThreadSafe(first);
		}),
		"L01 dormant submission rejects"
	);
	{
		TestHost host;
		adopted.setThreadSafe(first);
		host.flush();
		require(adopted.get() == first, "L01 activation adopts the pre-host signal");
	}

	{
		TestHost firstHost;
		TestHost secondHost;
		const Signal value{0};
		value.setThreadSafe(first);
		firstHost.close();
		secondHost.flush();
		require(value.get() == first, "L02 closing one host keeps the shared endpoint");
		firstHost.close();
	}

	{
		std::promise<Signal<int>> ready;
		std::promise<void> apply;
		auto isolated = worker([&ready, &apply] -> void {
			TestHost host;
			const Signal value{0};
			ready.set_value(value);
			apply.get_future().wait();
			host.flush();
			require(value.get() == first, "L03 owner pump applies its own mailbox");
		});
		const auto foreign = ready.get_future().get();
		worker([foreign] -> void {
			foreign.setThreadSafe(first);
		}).get();
		TestHost local;
		local.flush();
		apply.set_value();
		isolated.get();
	}

	{
		TestHost host;
		const auto live = std::make_shared<int>(0);
		std::optional<Signal<Payload>> value{std::in_place, Payload{live, 0}};
		require(*live == 1, "L04 initial payload");
		value->setThreadSafe(Payload{live, first});
		value.reset();
		host.flush();
		require(*live == 0, "L04 expired command releases the payload");
	}

	{
		const TestHost host;
		const auto live = std::make_shared<int>(0);
		const Signal value{Payload{live, 0}};
		value.setThreadSafe(Payload{live, first});
		const auto endpoint = detail::ExecutorEndpoint::current();
		endpoint->close();
		require(*live == 1, "L05 unapplied payload released");
		require(
			throwsLogic([&] -> void {
				value.setThreadSafe(Payload{live, second});
			}),
			"L05 subsequent admission rejects"
		);
		value.set(Payload{live, second});
		require(value.get().identity == second, "L07 old signal remains synchronously usable");
	}

	{
		const TestHost host;
		const Signal value{0};
		const auto endpoint = detail::ExecutorEndpoint::current();
		std::promise<void> entered;
		std::promise<void> closed;
		endpoint->setAdmissionProbe(
			[&] -> void {
				entered.set_value();
				closed.get_future().wait();
			},
			{}
		);
		auto pending = worker([value] -> bool {
			return throwsLogic([&] -> void {
				value.setThreadSafe(first);
			});
		});
		entered.get_future().wait();
		endpoint->close();
		closed.set_value();
		require(pending.get() && value.get() == 0, "L06 rejected command never applies");
	}

	{
		std::promise<Signal<int>> ready;
		auto finished = worker([&ready] -> void {
			const TestHost host;
			ready.set_value(Signal<int>{0});
		});
		const auto orphaned = ready.get_future().get();
		finished.get();
		require(
			throwsLogic([&] -> void {
				orphaned.setThreadSafe(first);
			}),
			"L07 submission rejects after the owner thread exits"
		);
		TestHost host;
		const Signal fresh{0};
		fresh.setThreadSafe(third);
		host.flush();
		require(fresh.get() == third, "L07 new-epoch signals accept queued writes");
	}
}

void capacityAndFailure() {
	constexpr int first = 1;
	constexpr int second = 2;
	{
		TestHost host;
		const Signal value{0};
		for (std::size_t index = 0; index < detail::maximumPendingSignalWrites; ++index) {
			value.setThreadSafe(static_cast<int>(index + 1));
		}
		require(
			throwsOverflow([&] -> void {
				value.setThreadSafe(first);
			}),
			"F01 overflow rejects without displacement"
		);
		require(
			detail::ExecutorEndpoint::current()->pendingWrites() ==
				detail::maximumPendingSignalWrites,
			"F01 capacity stays full"
		);
		host.flush();
		require(
			value.get() == static_cast<int>(detail::maximumPendingSignalWrites) &&
				detail::ExecutorEndpoint::current()->pendingWrites() == 0,
			"F01 applying returns capacity"
		);
		value.setThreadSafe(first);
		host.flush();
		require(value.get() == first, "F01 restored capacity admits again");
	}

	{
		TestHost host;
		const Signal value{0};
		int deliveries = 0;
		const auto subscription = observe(value, [&](int next) -> void {
			++deliveries;
			if (next == first) {
				require(
					throwsOverflow([&] -> void {
						value.setThreadSafe(second);
					}),
					"F01 detached snapshot still occupies capacity"
				);
			}
		});
		for (std::size_t index = 0; index < detail::maximumPendingSignalWrites; ++index) {
			value.setThreadSafe(static_cast<int>(index + 1));
		}
		host.flush();
		require(
			deliveries == static_cast<int>(detail::maximumPendingSignalWrites) + 1,
			"F01 drain"
		);
	}

	{
		TestHost host;
		const auto live = std::make_shared<int>(0);
		const Signal value{0};
		const Signal extra{Payload{live, 0}};
		const auto subscription = observe(value, [&](int next) -> void {
			if (next == first) {
				throw std::runtime_error("apply failed");
			}
		});
		value.setThreadSafe(first);
		extra.setThreadSafe(Payload{live, first});
		bool failed = false;
		try {
			host.flush();
		} catch (const std::runtime_error &) {
			failed = true;
		}
		require(failed && *live == 1, "F02 remaining backlog is released");
		require(
			throwsLogic([&] -> void {
				value.setThreadSafe(second);
			}),
			"F02 failed session stays closed"
		);
	}

	{
		const TestHost host;
		const auto target = std::make_shared<Signal<int>>(0);

		struct Reentrant {
			std::shared_ptr<Signal<int>> other;
			int identity = 0;

			Reentrant(std::shared_ptr<Signal<int>> source, int value)
				: other(std::move(source)),
				  identity(value) {}

			Reentrant(const Reentrant &) = default;
			Reentrant(Reentrant &&) = default;
			auto operator=(const Reentrant &) -> Reentrant & = default;
			auto operator=(Reentrant &&) -> Reentrant & = default;

			auto operator==(const Reentrant &otherValue) const -> bool {
				return identity == otherValue.identity;
			}

			~Reentrant() {
				if (!other) {
					return;
				}
				try {
					other->setThreadSafe(identity);
				} catch (const std::exception &error) {
					static_cast<void>(error);
					identity = 0;
				}
			}
		};

		const Signal value{Reentrant{target, 0}};
		value.setThreadSafe(Reentrant{target, first});
		detail::ExecutorEndpoint::current()->close();
		require(true, "F03 reentrant cleanup does not deadlock");
	}
}
} // namespace

auto main() -> int {
	try {
		apiOrderingAndSettlement();
		retainedUi();
		activationIsolationAndOwnership();
		capacityAndFailure();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
