#pragma once

#include <Core/Hash.hpp>
#include <stack>

namespace RoseEngine {

// Dim=1: Binary tree
// Dim=2: Quadtree
// Dim=3: Octree
template<uint32_t Dimensions, typename T>
class SubdivisionNode {
public:
	static const uint32_t ChildCount = (1u << Dimensions);

	using iterator       = SubdivisionNode*;
	using const_iterator = SubdivisionNode* const;
	using Coordinate = glm::vec<Dimensions, T>;

	SubdivisionNode() = default;
	SubdivisionNode(SubdivisionNode&&) = default;
	SubdivisionNode& operator=(SubdivisionNode&&) = default;
	SubdivisionNode(const SubdivisionNode&) = delete;
	SubdivisionNode& operator=(const SubdivisionNode&) = delete;

	struct NodeId {
		uint64_t depth = 0;
		uint64_t packedIds = 0; // for an octree: 64 bits / 3 bits per level = 21 levels max

		inline bool operator==(const NodeId& rhs) const = default;
		inline bool operator!=(const NodeId& rhs) const = default;

		// gets offset of node at childDepth.
		// e.g. for a node n at depth d, the node is under n.children[GetChildIndex(d)]
		inline uint32_t GetChildIndex(const uint64_t d) const {
			return uint32_t(packedIds >> (d*Dimensions)) & (ChildCount - 1);
		}
		inline void     SetChildIndex(const uint64_t d, const uint64_t i) {
			const uint64_t mask = uint64_t(ChildCount-1) << (d*Dimensions);
			packedIds = (packedIds & ~mask) | ((i << (d*Dimensions)) & mask);
		}
	};

	inline SubdivisionNode& Decode(const NodeId node) {
		SubdivisionNode* n = this;
		for (uint32_t d = id.depth; d < node.depth; d++) {
			if (!n->children) break;
			n = &n->children[node.GetChildIndex(d)];
		}
		return *n;
	}

	inline bool Join() {
		if (IsLeaf()) return false;
		children.reset();
		return true;
	}
	inline bool Split() {
		if (!IsLeaf()) return false;
		const Coordinate childExtent = (aabbMax - aabbMin) / T(2);
		children = std::make_unique<SubdivisionNode[]>(ChildCount);
		for (uint32_t i = 0; i < ChildCount; i++) {
			SubdivisionNode& c = children[i];
			c.id = id;
			c.id.SetChildIndex(id.depth, i);
			c.id.depth++;
			c.children = nullptr;
			c.aabbMin = aabbMin + childExtent * Coordinate(uint3(i & 1, (i >> 1) & 1, (i >> 2) & 1));
			c.aabbMax = c.aabbMin + childExtent;
		}
		return true;
	}

	inline const NodeId& GetId() const { return id; }
	inline bool          IsLeaf() const { return !children; }
	inline Coordinate    GetMin() const { return aabbMin; }
	inline Coordinate    GetMax() const { return aabbMax; }
	inline std::span<SubdivisionNode> Children() const { return children ? std::span{ &children[0], ChildCount } : std::span<SubdivisionNode>{}; }

	template<std::invocable<SubdivisionNode&> F>
	inline void Enumerate(F&& fn) {
		std::queue<SubdivisionNode*> todo;
		todo.push(this);
		for (;!todo.empty();) {
			SubdivisionNode* n = todo.front();
			todo.pop();
			fn(*n);
			if (!n->IsLeaf()) {
				for (uint32_t i = 0; i < ChildCount; i++)
					todo.push(&n->children[i]);
			}
		}
	}

	template<std::invocable<SubdivisionNode&> F>
	inline void EnumerateLeaves(F&& fn) {
		std::queue<SubdivisionNode*> todo;
		todo.push(this);
		for (;!todo.empty();) {
			SubdivisionNode* n = todo.front();
			todo.pop();
			if (n->IsLeaf()) {
				fn(*n);
			} else {
				for (uint32_t i = 0; i < ChildCount; i++)
					todo.push(&n->children[i]);
			}
		}
	}

	template<std::invocable<SubdivisionNode&> F>
	inline void EnumerateMasked(F&& fn, uint8_t childMask = 0xFF) {
		std::queue<SubdivisionNode*> todo;
		todo.push(this);
		for (;!todo.empty();) {
			SubdivisionNode* n = todo.front();
			todo.pop();
			fn(*n);
			if (!n->IsLeaf()) {
				for (uint32_t i = 0; i < ChildCount; i++)
					if (childMask & (1 << i))
						todo.push(&n->children[i]);
			}
		}
	}

private:
	NodeId id = { 0, 0 };
	std::unique_ptr<SubdivisionNode[]> children = nullptr;
	Coordinate aabbMin = Coordinate(T(0));
	Coordinate aabbMax = Coordinate(T(1));
};

using OctreeNode = SubdivisionNode<3, float>;

}

namespace std {

template<>
struct hash<RoseEngine::OctreeNode::NodeId> {
inline size_t operator()(const RoseEngine::OctreeNode::NodeId& id) const {
	return RoseEngine::HashArgs(id.packedIds, id.depth);
}
};

}
