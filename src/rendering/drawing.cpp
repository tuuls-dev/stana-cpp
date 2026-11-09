module;
#include <algorithm>
#include <cmath>
#include <compare>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
module stana;
import :rendering;

namespace stana::rendering {
namespace {
void validateCommand(const PaintCommand &command) {
	using enum PaintKind;
	if (command.boxGeometry || command.shadow) {
		throw std::invalid_argument(
			"CPUCanvas cannot author projection-only geometry/shadow payloads"
		);
	}
	if (command.kind == popClip) {
		return;
	}
	const auto &rect = command.bounds;
	stana::detail::requireFinite(rect.origin.x.value());
	stana::detail::requireFinite(rect.origin.y.value());
	stana::detail::requireFinite(rect.origin.x.value() + rect.size.width.value());
	stana::detail::requireFinite(rect.origin.y.value() + rect.size.height.value());
	if (command.kind == line) {
		stana::detail::requireFinite(rect.size.width.value());
		stana::detail::requireFinite(rect.size.height.value());
	} else {
		stana::detail::requireNonnegative(rect.size.width);
		stana::detail::requireNonnegative(rect.size.height);
	}
	switch (command.kind) {
	case strokeRect:
	case line: {
		stana::detail::requireNonnegative(command.lineWidth);
		stana::detail::requireFinite(std::abs(rect.size.width.value()) + command.lineWidth.value());
		stana::detail::requireFinite(
			std::abs(rect.size.height.value()) + command.lineWidth.value()
		);
		const double half = command.lineWidth.value() / 2;
		for (const double endpoint :
			{rect.origin.x.value(),
				rect.origin.y.value(),
				rect.origin.x.value() + rect.size.width.value(),
				rect.origin.y.value() + rect.size.height.value()}) {
			stana::detail::requireFinite(endpoint - half);
			stana::detail::requireFinite(endpoint + half);
		}
		break;
	}
	case boxShadow:
		throw std::invalid_argument("CPUCanvas does not author box shadows");
	case fillRect:
	case text:
	case pushClip:
	case popClip:
		break;
	}
	if (command.kind == fillRect || command.kind == strokeRect || command.kind == pushClip) {
		stana::detail::requireNonnegative(command.cornerRadius);
	}
	if (command.kind == text) {
		stana::detail::requireNonnegative(command.font.size);
		stana::detail::requireFinite(command.font.weight.value());
	}
	if (command.kind != pushClip) {
		for (const double component :
			{command.color.red.value(),
				command.color.green.value(),
				command.color.blue.value(),
				command.color.alpha.value()}) {
			stana::detail::requireNonnegative(component);
			if (component > 1) {
				throw std::invalid_argument("Drawing SDR color exceeds one");
			}
		}
	}
}
} // namespace

auto PaintCommand::operator==(const PaintCommand &other) const -> bool {
	if (kind != other.kind) {
		return false;
	}
	if (boxGeometry.has_value() != other.boxGeometry.has_value() ||
		(boxGeometry &&
			(boxGeometry->outer != other.boxGeometry->outer ||
				boxGeometry->padding != other.boxGeometry->padding))) {
		return false;
	}
	using enum PaintKind;
	switch (kind) {
	case boxShadow:
		return shadow == other.shadow;
	case popClip:
		return true;
	case pushClip:
		return bounds == other.bounds && std::is_eq(cornerRadius <=> other.cornerRadius);
	case fillRect:
		return bounds == other.bounds && color == other.color &&
			std::is_eq(cornerRadius <=> other.cornerRadius);
	case strokeRect:
		return bounds == other.bounds && color == other.color &&
			std::is_eq(cornerRadius <=> other.cornerRadius) &&
			std::is_eq(lineWidth <=> other.lineWidth);
	case line:
		return bounds == other.bounds && color == other.color &&
			std::is_eq(lineWidth <=> other.lineWidth);
	case text:
		return bounds == other.bounds && color == other.color && this->text == other.text &&
			font == other.font;
	}
	throw std::logic_error("Unknown drawing command");
}

void Drawing::validate() const {
	std::size_t depth = 0;
	for (const auto &command : commands) {
		validateCommand(command);
		if (command.kind == PaintKind::pushClip) {
			++depth;
		} else if (command.kind == PaintKind::popClip) {
			if (depth == 0) {
				throw std::invalid_argument("CPUCanvas clip underflow");
			}
			--depth;
		}
	}
	if (depth != 0) {
		throw std::invalid_argument("Unbalanced CPUCanvas clips");
	}
}

void Drawing::fillRect(Rect bounds, Color color, Points radius) {
	PaintCommand command{
		.kind = PaintKind::fillRect,
		.bounds = bounds,
		.color = color,
		.cornerRadius = radius
	};
	validateCommand(command);

	commands.push_back(std::move(command));
}

void Drawing::strokeRect(Rect bounds, Color color, Points width, Points radius) {
	PaintCommand command{
		.kind = PaintKind::strokeRect,
		.bounds = bounds,
		.color = color,
		.cornerRadius = radius,
		.lineWidth = width
	};
	validateCommand(command);

	commands.push_back(std::move(command));
}
} // namespace stana::rendering
