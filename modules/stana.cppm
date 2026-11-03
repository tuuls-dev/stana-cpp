module;

#include <string_view>

export module stana;

export import stana.primitives;
export import stana.reactive;
export import stana.graphics;
export import stana.text;
export import stana.ui;
export import stana.application;

export namespace stana {

/// @brief Returns the framework build version, without creating runtime or native state.
[[nodiscard]] auto version() noexcept -> std::string_view;

} // namespace stana
