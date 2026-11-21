#ifndef STANA_APPLICATION_TEXT_SERVICES_HPP
#define STANA_APPLICATION_TEXT_SERVICES_HPP
#include "../platform/text_contracts.hpp"
#include <memory>

namespace stana::application::detail {
struct TextAttachment {
	::stana::platform::detail::TextInputCallbacks callbacks{};
};

/// @brief Creates services before mount, without creating a native application or view.
[[nodiscard]] auto installTextServices(
	const std::shared_ptr<::stana::document::detail::Document> &document
) -> ::stana::platform::detail::TextInputCallbacks;
} // namespace stana::application::detail
#endif
