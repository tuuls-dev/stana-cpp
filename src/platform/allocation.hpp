#ifndef STANA_ALLOCATION_HPP
#define STANA_ALLOCATION_HPP
#include <algorithm>
#include <cstddef>
#include <expected>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace stana::native {
inline constexpr std::size_t windowAllocationLimit = 256ULL * 1024 * 1024;
inline constexpr std::size_t deviceAllocationLimit = 512ULL * 1024 * 1024;

/// @brief Shared by raster, GPU, cache and retired owners. Charges end with final ownership.
struct AllocationLedger {
	std::mutex mutex;
	std::size_t bytes = 0;
	std::size_t peak = 0;
	std::size_t limit = windowAllocationLimit;
};

class AllocationLease {
  public:
	AllocationLease(
		std::shared_ptr<AllocationLedger> device,
		std::shared_ptr<AllocationLedger> window,
		std::size_t bytes
	)
		: device_(std::move(device)),
		  window_(std::move(window)),
		  bytes_(bytes) {}

	AllocationLease(const AllocationLease &) = delete;
	AllocationLease(AllocationLease &&) = delete;
	auto operator=(const AllocationLease &) -> AllocationLease & = delete;
	auto operator=(AllocationLease &&) -> AllocationLease & = delete;

	~AllocationLease() {
		const std::scoped_lock lock{device_->mutex, window_->mutex};
		device_->bytes -= bytes_;
		window_->bytes -= bytes_;
	}

	[[nodiscard]] static auto reserve(
		const std::shared_ptr<AllocationLedger> &device,
		const std::shared_ptr<AllocationLedger> &window,
		std::size_t bytes
	) -> std::expected<std::shared_ptr<AllocationLease>, std::string> {
		if (!device || !window || device == window) {
			return std::unexpected("Invalid allocation ledgers");
		}
		// Allocate the control block before changing counters; allocation failure is atomic.
		auto result = std::make_shared<AllocationLease>(device, window, 0);
		const std::scoped_lock lock{device->mutex, window->mutex};
		if (bytes > device->limit - device->bytes || bytes > window->limit - window->bytes) {
			return std::unexpected("Combined canvas allocation budget exceeded");
		}
		device->bytes += bytes;
		window->bytes += bytes;
		device->peak = std::max(device->peak, device->bytes);
		window->peak = std::max(window->peak, window->bytes);
		result->bytes_ = bytes;
		return result;
	}

	/// @brief Transfers part of an already-admitted peak reservation without double charging.
	[[nodiscard]] auto split(std::size_t bytes) -> std::shared_ptr<AllocationLease> {
		auto result = std::make_shared<AllocationLease>(device_, window_, 0);
		const std::scoped_lock lock{device_->mutex, window_->mutex};
		if (bytes > bytes_) {
			throw std::logic_error("Allocation exceeds its admitted reservation");
		}
		bytes_ -= bytes;
		result->bytes_ = bytes;
		return result;
	}

  private:
	std::shared_ptr<AllocationLedger> device_;
	std::shared_ptr<AllocationLedger> window_;
	std::size_t bytes_;
};
} // namespace stana::native
#endif
