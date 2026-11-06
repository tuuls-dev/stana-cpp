module;

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

module stana;
import :document;
import :primitives;
import :reactive;
import :rendering;

namespace stana::document {
namespace {
constexpr double buttonPadding = 10;
constexpr double defaultCornerRadius = 6;
} // namespace

namespace detail {
auto Document::create(Size size, Environment environment) -> std::shared_ptr<Document> {
	auto document = std::make_shared<Document>(size, environment);
	const auto weak = std::weak_ptr{document};
	stana::reactive::detail::ReactiveRuntime::current().commits.emplace(
		document->identity_,
		[weak] -> void {
			if (const auto owner = weak.lock()) {
				owner->commit();
			}
		}
	);
	return document;
}

Document::Document(Size size, Environment environment)
	: identity_(stana::detail::nextIdentity()),
	  viewport_(size),
	  environment_(environment) {
	stana::detail::requireNonnegative(size.width);
	stana::detail::requireNonnegative(size.height);
	root_ = createNode(NodeKind::root, {});
}

Document::~Document() {
	try {
		close();
	} catch (...) {
		std::terminate();
	}
}

void Document::close() {
	if (std::exchange(isClosed_, true)) {
		return;
	}
	stana::reactive::detail::ReactiveRuntime::current().commits.erase(identity_);
	onPublish_ = {};
	frames_.clear();
	frameDemandHandler_ = {};
	lastFrameDemand_ = false;
	remove(root_);
	snapshots_.clear();
	scene_.reset();
	render_.reset();
	geometryHistory_.clear();
	preparedGeometry_.reset();
	gpuFailures_.clear();
	pinnedGeometry_.clear();
	confirmedGeometry_.reset();
}

auto Document::createNode(NodeKind kind, NodeId parent) -> NodeId {
	if (isClosed_) {
		throw std::logic_error("Cannot mount into a closed document");
	}
	const NodeId nodeId{.document = identity_, .serial = stana::detail::nextIdentity()};
	auto node = std::make_unique<Node>();
	node->id = nodeId;
	node->parent = parent;
	node->kind = kind;
	node->clip =
		kind == NodeKind::scroll || kind == NodeKind::cpuCanvas || kind == NodeKind::gpuCanvas;
	if (kind == NodeKind::button || kind == NodeKind::toggle) {
		node->padding = Insets::all(buttonPadding);
		node->cornerRadius = defaultCornerRadius;
		node->background = Color::accent();
		node->foreground = Color::white();
		node->isForegroundExplicit = true;
	}
	if (parent.serial != 0) {
		auto *parentNode = find(parent);
		if (parentNode == nullptr) {
			throw std::logic_error("Cannot mount into a disposed parent");
		}
		parentNode->children.push_back(nodeId);
	}
	nodes_.emplace(nodeId.serial, std::move(node));
	++metrics_.mounts;
	invalidate(nodeId, fullWork);
	if (parent.serial != 0) {
		invalidate(parent, Dirty::measure | Dirty::arrange | Dirty::structure);
	}
	return nodeId;
}

auto Document::find(NodeId identity) const -> Node * {
	if (identity.document != identity_) {
		return nullptr;
	}
	const auto found = nodes_.find(identity.serial);
	return found == nodes_.end() ? nullptr : found->second.get();
}

auto Document::handle(NodeId identity) -> NodeHandle {
	return {weak_from_this(), identity};
}

void Document::removeChildren(NodeId identity) {
	if (const auto *node = find(identity)) {
		const auto children = node->children;
		for (const auto child : children) {
			remove(child);
		}
	}
}

void Document::remove(NodeId identity) {
	const auto *node = find(identity);
	if (node == nullptr) {
		return;
	}
	const auto parentId = node->parent;
	std::vector<NodeId> retired{identity};
	for (std::size_t index = 0; index < retired.size(); ++index) {
		if (const auto *record = find(retired.at(index))) {
			retired.insert(retired.end(), record->children.begin(), record->children.end());
		}
	}
	if (auto *parent = find(parentId)) {
		std::erase(parent->children, identity);
	}
	// Lookup retirement precedes every application cleanup. An owning batch keeps
	// scopes/storage alive even when cleanup reenters removal or commits a sibling.
	std::vector<std::unique_ptr<Node>> disposal;
	for (const auto id : retired) {
		auto entry = nodes_.extract(id.serial);
		if (!entry.empty()) {
			disposal.push_back(std::move(entry.mapped()));
		}
		if (focused_ == id) {
			focused_.reset();
		}
		if (pressed_ == id) {
			pressed_.reset();
		}
		if (hovered_ == id) {
			hovered_.reset();
		}
	}
	for (const auto &[serial, record] : nodes_) {
		static_cast<void>(serial);
		for (const auto id : retired) {
			record->work.measureChildren.erase(id);
			record->work.arrangeChildren.erase(id);
			record->work.projectionChildren.erase(id);
		}
	}
	if (find(parentId) != nullptr) {
		invalidate(parentId, Dirty::measure | Dirty::arrange | Dirty::structure);
	}
	for (auto &record : std::views::reverse(disposal)) {
		record->scope.dispose();
	}
}

auto Document::reorder(NodeId parent, const std::vector<NodeId> &children)
	-> std::expected<void, Error> {
	auto *node = find(parent);
	if (node == nullptr || children.size() != node->children.size()) {
		return std::unexpected(Error::invalidStructure);
	}
	auto wanted = children;
	auto existing = node->children;
	std::ranges::sort(wanted);
	std::ranges::sort(existing);
	if (wanted != existing || std::ranges::adjacent_find(wanted) != wanted.end()) {
		return std::unexpected(Error::invalidStructure);
	}
	if (node->children == children) {
		return {};
	}
	node->children = children;
	invalidate(parent, Dirty::measure | Dirty::arrange | Dirty::structure);
	return {};
}

void Document::invalidate(NodeId identity, Dirty domains) {
	auto *node = find(identity);
	if (node == nullptr || domains == Dirty::none) {
		return;
	}
	mutationStamp_ = stana::detail::nextIdentity();
	isDirty_ = true;
	if (has(domains, Dirty::measure) && !has(node->work.local, Dirty::measure)) {
		node->work.measureInputs = stana::detail::nextIdentity();
	}
	if (has(domains, Dirty::arrange) && !has(node->work.local, Dirty::arrange)) {
		node->work.placementInputs = stana::detail::nextIdentity();
	}
	node->work.local = node->work.local | domains;
	auto child = identity;
	for (auto *parent = find(node->parent); parent != nullptr; parent = find(parent->parent)) {
		bool changed = parent->work.projectionChildren.insert(child).second;
		if (has(domains, Dirty::measure)) {
			if (parent->work.measureChildren.insert(child).second) {
				parent->work.descendantInputs = stana::detail::nextIdentity();
				changed = true;
			}
		}
		if (has(domains, Dirty::arrange)) {
			changed = parent->work.arrangeChildren.insert(child).second || changed;
		}
		if (!changed) {
			break;
		}
		child = parent->id;
	}
}

void Document::invalidate() {
	for (const auto &[serial, node] : nodes_) {
		static_cast<void>(serial);
		node->work.measuredAt.reset();
		node->work.arrangedAt.reset();
		node->work.local = fullWork;
	}
	if (find(root_) != nullptr) {
		invalidate(root_, fullWork);
	}
}

void Document::forceFullRecompute() {
	invalidate();
	commit();
}

void Document::resize(Size size) {
	stana::detail::requireNonnegative(size.width);
	stana::detail::requireNonnegative(size.height);
	if (viewport_ == size) {
		return;
	}
	viewport_ = size;
	invalidate(root_, Dirty::measure | Dirty::arrange | Dirty::snapshot);
	commit();
}

void Document::setEnvironment(Environment environment) {
	stana::detail::requireNonnegative(environment.textScale);
	const bool textChanged = std::is_neq(environment.textScale <=> environment_.textScale);
	const bool directionChanged = environment.direction != environment_.direction;
	const bool themeChanged = environment.theme != environment_.theme;
	const bool colorChanged = themeChanged || environment.highContrast != environment_.highContrast;
	const bool motionChanged = environment.reducedMotion != environment_.reducedMotion;
	if (environment == environment_) {
		return;
	}
	environment_ = environment;
	if (textChanged) {
		textEnvironment_ = stana::detail::nextIdentity();
	}
	for (const auto &[serial, node] : nodes_) {
		static_cast<void>(serial);
		Dirty domains = Dirty::none;
		if (directionChanged) {
			domains = domains | Dirty::arrange | Dirty::snapshot;
			if (node->kind == NodeKind::cpuCanvas || node->kind == NodeKind::gpuCanvas ||
				node->kind == NodeKind::symbol || node->kind == NodeKind::text ||
				node->kind == NodeKind::button || node->kind == NodeKind::toggle ||
				node->cornerRadii || node->borderWidths || !node->boxShadows.empty()) {
				domains = domains | Dirty::paint;
			}
		}
		if (node->kind == NodeKind::symbol && (colorChanged || motionChanged)) {
			domains = domains | Dirty::paint;
		}
		if (node->kind == NodeKind::text || node->kind == NodeKind::button ||
			node->kind == NodeKind::toggle) {
			if (textChanged) {
				domains = domains | Dirty::measure | Dirty::paint;
			}
			if (themeChanged && !node->isForegroundExplicit) {
				domains = domains | Dirty::paint;
			}
		}
		if (colorChanged && (node->gpuDrawing || !node->boxShadows.empty())) {
			domains = domains | Dirty::paint;
		}
		if (colorChanged) {
			const auto semantic = [](Color color) -> bool {
				return color.token != Color::Token::explicitColor;
			};
			if (semantic(node->background) || semantic(node->foreground) ||
				semantic(node->borderColor) ||
				std::ranges::any_of(
					node->drawing.commands,
					[&](const stana::rendering::PaintCommand &command) -> bool {
						return semantic(command.color);
					}
				)) {
				domains = domains | Dirty::paint;
			}
		}
		invalidate(node->id, domains);
	}
	if (themeChanged || (!textChanged && !directionChanged)) {
		invalidate(root_, Dirty::snapshot);
	}
	commit();
}

void Document::setTextMeasure(std::function<Size(const std::string &, Font, double)> measure) {
	measureText_ = std::move(measure);
	textEnvironment_ = stana::detail::nextIdentity();
	for (const auto &[serial, node] : nodes_) {
		static_cast<void>(serial);
		if (node->kind == NodeKind::text || node->kind == NodeKind::button ||
			node->kind == NodeKind::toggle) {
			invalidate(node->id, Dirty::measure | Dirty::paint);
		}
	}
}

void Document::setOnPublish(std::function<void()> callback) {
	onPublish_ = std::move(callback);
}

void Document::queueGPUDiagnostic(
	stana::rendering::GPUDiagnostic diagnostic,
	std::uint64_t drawingVersion,
	std::uint64_t epoch
) {
	const auto current =
		[&](NodeId instance, std::uint64_t version, std::uint64_t generation) -> bool {
		const auto *node = find(instance);
		return !isClosed_ && generation == presentationEpoch_ && node != nullptr &&
			node->gpuDrawing && node->gpuDrawing->version() == version;
	};
	if (!current(diagnostic.instance, drawingVersion, epoch)) {
		return;
	}
	if (diagnostic.programVersion != 0) {
		const auto *node = find(diagnostic.instance);
		if (node == nullptr || !node->gpuDrawing) {
			return;
		}
		bool matches = false;
		for (const auto &command :
			stana::rendering::detail::GPUAccess::commands(*node->gpuDrawing)) {
			if (const auto *program = std::get_if<stana::rendering::GPUProgram>(&command);
				program != nullptr && program->version() == diagnostic.programVersion) {
				matches = true;
			}
		}
		if (!matches) {
			return;
		}
	}
	constexpr std::size_t queueLimit = 256;
	constexpr std::size_t messageLimit = 8192;
	for (auto iterator = gpuFailures_.begin(); iterator != gpuFailures_.end();) {
		if (!current(std::get<0>(*iterator), std::get<1>(*iterator), std::get<2>(*iterator))) {
			iterator = gpuFailures_.erase(iterator);
		} else {
			++iterator;
		}
	}
	const GPUFailureKey key{
		diagnostic.instance,
		drawingVersion,
		epoch,
		diagnostic.programVersion,
		diagnostic.category
	};
	if (gpuFailures_.size() >= queueLimit || !gpuFailures_.insert(key).second) {
		return;
	}
	if (diagnostic.message.size() > messageLimit) {
		diagnostic.message.resize(messageLimit);
	}
	const auto weak = weak_from_this();
	stana::reactive::detail::ReactiveRuntime::current().effects.emplace_back(
		[weak, queued = std::move(diagnostic), drawingVersion, epoch] -> void {
			if (const auto owner = weak.lock();
				owner && !owner->isClosed_ && owner->presentationEpoch_ == epoch) {
				const auto *node = owner->find(queued.instance);
				if (node == nullptr || !node->gpuDrawing ||
					node->gpuDrawing->version() != drawingVersion || !node->onGPUError) {
					return;
				}
				const auto handler = node->onGPUError;
				try {
					handler(queued);
				} catch (...) {
					owner->report(Error::nativeFailure);
				}
			}
		}
	);
}

auto Document::frameById(std::uint64_t registration) -> FrameRegistration * {
	for (auto &frame : frames_) {
		if (frame.id == registration && !frame.cancelled) {
			return &frame;
		}
	}
	return nullptr;
}

auto Document::frameById(std::uint64_t registration) const -> const FrameRegistration * {
	for (const auto &frame : frames_) {
		if (frame.id == registration && !frame.cancelled) {
			return &frame;
		}
	}
	return nullptr;
}

auto Document::addFrameRegistration(
	std::function<bool()> active,
	std::function<void(FrameStamp, double elapsed)> callback
) -> std::uint64_t {
	const auto id = stana::detail::nextIdentity();
	frames_.push_back(
		{.id = id,
			.active = std::move(active),
			.callback = std::move(callback),
			.lastTimestamp = std::nullopt,
			.cancelled = false}
	);
	refreshFrameDemand();
	return id;
}

void Document::cancelFrameRegistration(std::uint64_t registration) {
	if (auto *frame = frameById(registration)) {
		frame->cancelled = true;
		frame->active = {};
		frame->callback = {};
	}
	refreshFrameDemand();
}

void Document::resetFrameTimestamp(std::uint64_t registration) {
	if (auto *frame = frameById(registration)) {
		frame->lastTimestamp.reset();
	}
}

void Document::tickFrames(FrameStamp stamp) {
	if (isClosed_) {
		return;
	}
	std::vector<std::uint64_t> identifiers;
	identifiers.reserve(frames_.size());
	for (const auto &frame : frames_) {
		if (!frame.cancelled) {
			identifiers.push_back(frame.id);
		}
	}
	for (const auto registration : identifiers) {
		auto *frame = frameById(registration);
		if (frame == nullptr) {
			continue;
		}
		bool active = false;
		if (frame->active) {
			active = frame->active();
		}
		if (!active) {
			frame->lastTimestamp.reset();
			continue;
		}
		const double elapsed =
			frame->lastTimestamp ? std::max(0.0, stamp.timestamp - *frame->lastTimestamp) : 0.0;
		frame->lastTimestamp = stamp.timestamp;
		const auto callback = frame->callback;
		if (callback) {
			callback(stamp, elapsed);
		}
	}
	refreshFrameDemand();
}

void Document::setFrameDemandHandler(std::function<void(bool)> handler) {
	frameDemandHandler_ = std::move(handler);
	lastFrameDemand_ = false;
	refreshFrameDemand();
}

void Document::refreshFrameDemand() {
	const bool demand = frameDemand();
	if (demand == lastFrameDemand_ || !frameDemandHandler_) {
		lastFrameDemand_ = demand;
		return;
	}
	lastFrameDemand_ = demand;
	frameDemandHandler_(demand);
}

auto Document::frameDemand() const -> bool {
	for (const auto &frame : frames_) {
		if (!frame.cancelled && frame.active && frame.active()) {
			return true;
		}
	}
	return false;
}

void Document::setHostVisible(bool visible) {
	if (hostVisible_ == visible) {
		return;
	}
	hostVisible_ = visible;
	for (const auto &[serial, node] : nodes_) {
		static_cast<void>(serial);
		if (node->kind == NodeKind::symbol) {
			invalidate(node->id, Dirty::paint);
		}
	}
	refreshFrameDemand();
}

auto Document::queueSymbolResult(
	std::optional<::stana::symbol::Diagnostic> diagnostic,
	SymbolPreparation preparation
) -> bool {
	auto *node = find(preparation.instance);
	if (isClosed_ || node == nullptr || !node->symbol ||
		node->symbol->generation != preparation.generation) {
		return false;
	}
	auto &symbol = *node->symbol;
	if (symbol.epoch != 0 && symbol.epoch != preparation.epoch) {
		return false;
	}
	if (symbol.epoch != preparation.epoch) {
		symbol.failed.clear();
		symbol.epoch = preparation.epoch;
	}
	if (!diagnostic) {
		symbol.failed.clear();
		if (symbol.available) {
			return false;
		}
		symbol.available = true;
		invalidate(preparation.instance, Dirty::paint);
		const auto weak = weak_from_this();
		stana::reactive::detail::ReactiveRuntime::current().effects.emplace_back(
			[weak, preparation] -> void {
				const auto owner = weak.lock();
				if (!owner || owner->isClosed_) {
					return;
				}
				const auto *current = owner->find(preparation.instance);
				if (current == nullptr || !current->symbol ||
					current->symbol->generation != preparation.generation) {
					return;
				}
				if (current->symbol->epoch != 0 && current->symbol->epoch != preparation.epoch) {
					return;
				}
				owner->commit();
			}
		);
		return true;
	}
	if (!symbol.failed.insert(diagnostic->code).second) {
		return false;
	}
	symbol.available = false;
	invalidate(preparation.instance, Dirty::paint);
	const auto weak = weak_from_this();
	stana::reactive::detail::ReactiveRuntime::current().effects.emplace_back(
		[weak, queued = std::move(*diagnostic), preparation] -> void {
			const auto owner = weak.lock();
			if (!owner || owner->isClosed_) {
				return;
			}
			const auto *current = owner->find(preparation.instance);
			if (current == nullptr || !current->symbol ||
				current->symbol->generation != preparation.generation) {
				return;
			}
			if (current->symbol->epoch != 0 && current->symbol->epoch != preparation.epoch) {
				return;
			}
			owner->commit();
			try {
				if (current->symbol->onError) {
					current->symbol->onError(queued);
				} else {
					owner->report(Error::nativeFailure);
				}
			} catch (...) {
				owner->report(Error::nativeFailure);
			}
		}
	);
	return true;
}

void Document::queueSymbolDiagnostic(
	::stana::symbol::Diagnostic diagnostic,
	SymbolPreparation preparation
) {
	static_cast<void>(queueSymbolResult(std::move(diagnostic), preparation));
}

void Document::report(Error error) {
	errors_.push_back(error);
}

} // namespace detail

auto NodeHandle::isAlive() const -> bool {
	const auto document = document_.lock();
	const auto *node = document ? document->find(identity_) : nullptr;
	return node != nullptr && node->scope.alive();
}

auto NodeHandle::activate() const -> std::expected<void, Error> {
	const auto document = document_.lock();
	return document ? document->activate(identity_) : std::unexpected(Error::staleHandle);
}

auto NodeHandle::requestFocus() const -> std::expected<void, Error> {
	const auto document = document_.lock();
	return document ? document->focus(identity_) : std::unexpected(Error::staleHandle);
}
} // namespace stana::document
