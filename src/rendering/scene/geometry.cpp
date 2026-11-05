module;
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
module stana;
import :document;

namespace stana::rendering::detail {
/// @brief Immutable AVL entry; roots are shared by publications, history and admitted attempts.
struct GeometryIndex {
	NodeId id;
	GeometryRecord record;
	GeometryRoot left;
	GeometryRoot right;
	int height = 1;
};

namespace {
[[nodiscard]] auto height(const GeometryRoot &root) -> int {
	return root ? root->height : 0;
}

[[nodiscard]] auto
make(NodeId id, const GeometryRecord &record, GeometryRoot left, GeometryRoot right)
	-> GeometryRoot {
	const int depth = 1 + std::max(height(left), height(right));
	return std::make_shared<GeometryIndex>(GeometryIndex{
		.id = id,
		.record = record,
		.left = std::move(left),
		.right = std::move(right),
		.height = depth
	});
}

[[nodiscard]] auto rotateLeft(const GeometryRoot &root) -> GeometryRoot {
	const auto &pivot = root->right;
	return make(
		pivot->id,
		pivot->record,
		make(root->id, root->record, root->left, pivot->left),
		pivot->right
	);
}

[[nodiscard]] auto rotateRight(const GeometryRoot &root) -> GeometryRoot {
	const auto &pivot = root->left;
	return make(
		pivot->id,
		pivot->record,
		pivot->left,
		make(root->id, root->record, pivot->right, root->right)
	);
}

[[nodiscard]] auto balance(GeometryRoot root) -> GeometryRoot {
	if (height(root->left) - height(root->right) > 1) {
		if (height(root->left->left) < height(root->left->right)) {
			root = make(root->id, root->record, rotateLeft(root->left), root->right);
		}
		return rotateRight(root);
	}
	if (height(root->right) - height(root->left) > 1) {
		if (height(root->right->right) < height(root->right->left)) {
			root = make(root->id, root->record, root->left, rotateRight(root->right));
		}
		return rotateLeft(root);
	}
	return root;
}

void entries(const GeometryRoot &root, std::vector<const GeometryIndex *> &values) {
	if (!root) {
		return;
	}
	entries(root->left, values);
	values.push_back(root.get());
	entries(root->right, values);
}

void append(const GeometryIndex *entry, std::vector<Rect> &regions) {
	if (entry != nullptr) {
		regions.push_back(entry->record.bounds);
	}
}
} // namespace

auto geometryFind(const GeometryRoot &root, NodeId id) -> std::optional<GeometryRecord> {
	auto cursor = root;
	while (cursor) {
		if (id == cursor->id) {
			return cursor->record;
		}
		cursor = id < cursor->id ? cursor->left : cursor->right;
	}
	return std::nullopt;
}

auto geometrySet(const GeometryRoot &root, NodeId id, const GeometryRecord &record)
	-> GeometryRoot {
	if (!root) {
		return make(id, record, {}, {});
	}
	if (id == root->id) {
		return root->record == record ? root : make(id, record, root->left, root->right);
	}
	if (id < root->id) {
		const auto left = geometrySet(root->left, id, record);
		return left == root->left ? root : balance(make(root->id, root->record, left, root->right));
	}
	const auto right = geometrySet(root->right, id, record);
	return right == root->right ? root : balance(make(root->id, root->record, root->left, right));
}

auto geometryErase(const GeometryRoot &root, NodeId id) -> GeometryRoot {
	if (!root) {
		return root;
	}
	if (id < root->id) {
		const auto left = geometryErase(root->left, id);
		return left == root->left ? root : balance(make(root->id, root->record, left, root->right));
	}
	if (id > root->id) {
		const auto right = geometryErase(root->right, id);
		return right == root->right ? root
									: balance(make(root->id, root->record, root->left, right));
	}
	if (!root->left) {
		return root->right;
	}
	if (!root->right) {
		return root->left;
	}
	auto successor = root->right;
	while (successor->left) {
		successor = successor->left;
	}
	return balance(make(
		successor->id,
		successor->record,
		root->left,
		geometryErase(root->right, successor->id)
	));
}

void geometryChanges(
	const GeometryRoot &before,
	const GeometryRoot &after,
	std::vector<Rect> &regions
) {
	if (before == after) {
		return;
	}
	if (before && after && before->id == after->id) {
		if (!(before->record == after->record)) {
			append(before.get(), regions);
			append(after.get(), regions);
		}
		geometryChanges(before->left, after->left, regions);
		geometryChanges(before->right, after->right, regions);
		return;
	}
	// Rotated shapes are compared by key, not treated as changed geometry.
	std::vector<const GeometryIndex *> oldEntries;
	std::vector<const GeometryIndex *> newEntries;
	entries(before, oldEntries);
	entries(after, newEntries);
	std::size_t oldIndex = 0;
	std::size_t newIndex = 0;
	while (oldIndex < oldEntries.size() || newIndex < newEntries.size()) {
		if (newIndex == newEntries.size() ||
			(oldIndex < oldEntries.size() &&
				oldEntries.at(oldIndex)->id < newEntries.at(newIndex)->id)) {
			append(oldEntries.at(oldIndex++), regions);
		} else if (oldIndex == oldEntries.size() ||
			newEntries.at(newIndex)->id < oldEntries.at(oldIndex)->id) {
			append(newEntries.at(newIndex++), regions);
		} else {
			if (!(oldEntries.at(oldIndex)->record == newEntries.at(newIndex)->record)) {
				append(oldEntries.at(oldIndex), regions);
				append(newEntries.at(newIndex), regions);
			}
			++oldIndex;
			++newIndex;
		}
	}
}
} // namespace stana::rendering::detail
