module;

#include <compare>
#include <cstdint>
#include <optional>
#include <string>

export module stana:symbol_playback;
import :primitives;

export namespace stana::symbol {
/// @brief Paint-only motion. Effects never change layout, hit targets, or semantics.
enum class Effect : std::uint8_t {
	rotate, ///< One-second clockwise turn about the fitted square.
	pulse,	///< One-second uniform scale, without fading.
	bounce	///< 0.6-second upward translation.
};

/// @brief Caller-owned playback request. Equal once-requests do not replay.
struct Playback {
	enum class Mode : std::uint8_t { none, loop, once };

	Mode mode = Mode::none;
	Effect effect = Effect::rotate;
	std::uint64_t request = 0; ///< Observed only for Mode::once. Any different value restarts.

	[[nodiscard]] static constexpr auto none() noexcept -> Playback { return {}; }

	[[nodiscard]] static constexpr auto loop(Effect value) noexcept -> Playback {
		return {.mode = Mode::loop, .effect = value};
	}

	[[nodiscard]] static constexpr auto once(Effect value, std::uint64_t request) noexcept
		-> Playback {
		return {.mode = Mode::once, .effect = value, .request = request};
	}

	[[nodiscard]] auto operator==(const Playback &) const -> bool = default;
};

/// @brief Recoverable native failure delivered after commit on the UI executor.
struct Diagnostic {
	enum class Code : std::uint8_t {
		preparationFailed,	   ///< Path conversion failed for these inputs.
		resourceLimitExceeded, ///< Depth, segment, or cache budget rejected the artwork.
		renderingFailed		   ///< The raster pass failed after a valid preparation.
	};

	Code code = Code::preparationFailed;
	std::string message{};
	[[nodiscard]] auto operator==(const Diagnostic &) const -> bool = default;
};

/// @brief One frame's paint transform relative to the fitted square. Rest is identity.
struct EffectSample {
	double rotation = 0;		 ///< Clockwise radians about the square center.
	double scale = 1;			 ///< Uniform scale about the square center.
	double verticalFraction = 0; ///< Translation as a fraction of the square side, y-down.

	auto operator<=>(const EffectSample &) const = default;

	[[nodiscard]] auto operator==(const EffectSample &other) const -> bool {
		return std::is_eq(*this <=> other);
	}

	/// @brief Maps square-local artwork around its center. Layout geometry is unchanged.
	[[nodiscard]] auto transform(Rect square) const -> AffineTransform {
		const double x = square.origin.x + square.size.width / 2;
		const double y = square.origin.y + square.size.height / 2;
		return AffineTransform::translation(x, y + verticalFraction * square.size.height)
			.concatenating(AffineTransform::rotation(rotation))
			.concatenating(AffineTransform::scale(scale, scale))
			.concatenating(AffineTransform::translation(-x, -y));
	}
};

/// @brief One mount's clock. Reconcile after reactive delivery; advance only from frame ticks.
/// Hidden, reduced-motion, and unavailable symbols release frame demand without catch-up.
class PlaybackState {
  public:
	[[nodiscard]] auto elapsed() const noexcept -> double { return elapsed_; }

	[[nodiscard]] auto active() const noexcept -> std::optional<Effect> { return active_; }

	[[nodiscard]] auto looping() const noexcept -> bool { return looping_; }

	[[nodiscard]] auto lastRequest() const noexcept -> std::optional<std::uint64_t> {
		return lastRequest_;
	}

	[[nodiscard]] auto needsFrame() const noexcept -> bool { return needsFrame_; }

	[[nodiscard]] auto resetClock() const noexcept -> bool { return resetClock_; }

	void reconcile(
		bool assetChanged,
		Playback playback,
		bool reducedMotion,
		bool visible,
		bool available = true
	);
	void advance(double delta);
	[[nodiscard]] auto sample() const -> EffectSample;
	[[nodiscard]] static auto sample(Effect effect, double phase) -> EffectSample;

  private:
	void start(Effect effect, bool looping);
	void stop();

	double elapsed_ = 0;
	std::optional<Effect> active_{};
	bool looping_ = false;
	std::optional<std::uint64_t> lastRequest_{};
	Playback previous_{};
	bool initialized_ = false;
	bool reduced_ = false;
	bool visible_ = false;
	bool available_ = true;
	bool needsFrame_ = false;
	bool resetClock_ = true;
};
} // namespace stana::symbol
