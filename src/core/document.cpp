module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module stana.core;
import stana.primitives;
import stana.reactive;
import stana.graphics;

namespace stana {
namespace {
constexpr double buttonPadding = 10;
constexpr double defaultCornerRadius = 6;
} // namespace

namespace detail {
auto Document::create(Size size, Environment environment) -> std::shared_ptr<Document> {
	auto document = std::make_shared<Document>(size, environment);
	const auto weak = std::weak_ptr{document};
	ReactiveRuntime::current().commits.emplace(document->identity_, [weak] -> void {
		if (const auto owner = weak.lock()) {
			owner->commit();
		}
	});
	return document;
}

Document::Document(Size size, Environment environment)
	: identity_(nextIdentity()),
	  viewport_(size),
	  environment_(environment) {
	requireNonnegative(size.width);
	requireNonnegative(size.height);
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
	ReactiveRuntime::current().commits.erase(identity_);
	onPublish_ = {};
	remove(root_);
	snapshots_.clear();
	scene_.reset();
}

auto Document::createNode(NodeKind kind, NodeId parent) -> NodeId {
	if (isClosed_) {
		throw std::logic_error("Cannot mount into a closed document");
	}
	const NodeId nodeId{.document = identity_, .serial = nextIdentity()};
	auto node = std::make_unique<Node>();
	node->id = nodeId;
	node->parent = parent;
	node->kind = kind;
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
	invalidate();
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
	auto *node = find(identity);
	if (node == nullptr || !node->scope.alive()) {
		return;
	}
	// Mark the scope dead before cancellation can call application code. Descendant
	// records stay alive until their own cancellation has finished.
	node->scope.dispose();
	removeChildren(identity);
	if (auto *parent = find(node->parent)) {
		std::erase(parent->children, identity);
	}
	if (focused_ == identity) {
		focused_.reset();
	}
	if (pressed_ == identity) {
		pressed_.reset();
	}
	nodes_.erase(identity.serial);
	invalidate();
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
	node->children = children;
	invalidate();
	return {};
}

void Document::invalidate() {
	isDirty_ = true;
}

void Document::resize(Size size) {
	requireNonnegative(size.width);
	requireNonnegative(size.height);
	viewport_ = size;
	invalidate();
	commit();
}

void Document::setEnvironment(Environment environment) {
	requireNonnegative(environment.textScale);
	environment_ = environment;
	invalidate();
	commit();
}

void Document::setTextMeasure(std::function<Size(const std::string &, Font, double)> measure) {
	measureText_ = std::move(measure);
	invalidate();
}

void Document::setOnPublish(std::function<void()> callback) {
	onPublish_ = std::move(callback);
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
} // namespace stana
