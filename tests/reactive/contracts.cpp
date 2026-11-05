#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

import stana.reactive;

namespace {
void require(bool condition, const std::string &message = "Reactive contract failed") {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void graphAndTransactions() {
	using namespace stana;
	const Signal left{1};
	const Signal right{2};
	const Signal chooseLeft{true};
	int evaluations = 0;
	const auto selected = computed([&] -> int {
		++evaluations;
		return chooseLeft.get() ? left.get() : right.get();
	});
	std::vector<int> deliveries;
	const auto subscription = observe(selected, [&](int value) -> void {
		deliveries.push_back(value);
	});
	require(deliveries == std::vector{1});
	Transaction::perform([&] -> void {
		left.set(2);
		left.set(1);
		require(selected.get() == 1);
		require(deliveries.size() == 1);
	});
	require(deliveries.size() == 1);
	chooseLeft.set(false);
	require(deliveries == std::vector({1, 2}));
	const int previousEvaluations = evaluations;
	left.set(0);
	require(evaluations == previousEvaluations);
	right.set(1);
	require(deliveries == std::vector({1, 2, 1}));
	const auto parity = computed([&] -> int {
		return right.get() % 2;
	});
	int changes = 0;
	const auto equality = observe(parity, [&](int) -> void {
		++changes;
	});
	constexpr int odd = 3;
	right.set(odd);
	require(changes == 1);
}

void deliveryPreparationAndLifetime() {
	using namespace stana;
	const Signal first{0};
	const Signal second{0};
	std::vector<int> observed;
	const auto one = observe(first, [&](int value) -> void {
		if (value == 1) {
			second.set(2);
		}
	});
	const auto two = observe(second, [&](int value) -> void {
		observed.push_back(value);
	});
	Transaction::perform([&] -> void {
		first.set(1);
		second.set(1);
	});
	require(observed == std::vector({0, 1, 2}));
	const Scope scope;
	const auto token = scope.beginRequest();
	require(scope.isCurrent(token));
	const auto newer = scope.beginRequest();
	require(!scope.isCurrent(token) && scope.isCurrent(newer));
	int deliveries = 0;
	const auto owned = scope.own(observe(first, [&](int) -> void {
		++deliveries;
	}));
	static_cast<void>(owned);
	scope.dispose();
	first.set(2);
	require(deliveries == 1 && !scope.isCurrent(newer));
	std::vector<int> disposal;
	const Scope ordered;
	const auto a = ordered.own([&] -> void {
		disposal.push_back(1);
	});
	const auto b = ordered.own([&] -> void {
		disposal.push_back(2);
	});
	ordered.disown(a);
	ordered.dispose();
	require(disposal == std::vector{2});
	static_cast<void>(b);
}

void contractsAndFailures() {
	using namespace stana;
	const Signal value{0};
	const auto invalid = computed([&] -> int {
		value.set(1);
		return 0;
	});
	bool rejected = false;
	try {
		static_cast<void>(invalid.get());
	} catch (const std::logic_error &) {
		rejected = true;
	}
	require(rejected && value.get() == 0);
	try {
		Transaction::perform([&] -> void {
			value.set(2);
			throw std::runtime_error("Expected transaction failure");
		});
	} catch (const std::runtime_error &) {
		require(value.get() == 2);
	}
	// A future keeps the test independent of libc++'s known named-module/jthread
	// linkage bug (llvm/llvm-project#172241), without changing the affinity check.
	const bool wrongThread = std::async(std::launch::async, [&] -> bool {
		try {
			static_cast<void>(value.get());
		} catch (const std::logic_error &) {
			return true;
		}
		return false;
	}).get();
	require(wrongThread);
	const bool setWrongThread = std::async(std::launch::async, [&] -> bool {
		try {
			value.set(1);
		} catch (const std::logic_error &) {
			return true;
		}
		return false;
	}).get();
	require(setWrongThread, "worker set() must throw logic_error");
	require(value.get() == 2, "worker set() must not assign");
	const Property<std::string> text{"owned literal"};
	require(text.get() == "owned literal", "owned literal");
}
} // namespace

auto main() -> int {
	try {
		graphAndTransactions();
		deliveryPreparationAndLifetime();
		contractsAndFailures();
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
