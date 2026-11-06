module;

#include <string_view>

export module stana;

export import :primitives;
export import :symbol;
export import :reactive;
export import :tasks;
export import :rendering;
export import :layout;
export import :document;
export import :text;
export import :ui;
export import :application;
export import :testing;

export namespace stana {

/// @brief Returns the framework build version, without creating runtime or native state.
[[nodiscard]] auto version() noexcept -> std::string_view;

} // namespace stana
