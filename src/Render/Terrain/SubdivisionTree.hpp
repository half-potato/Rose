#pragma once

#include <Core/Hash.hpp>
#include "SubdivisionNode.h"
#include <stack>

namespace RoseEngine {

// Dim=1: Binary tree
// Dim=2: Quadtree
// Dim=3: Octree
template<uint32_t Dimensions>
class SubdivisionNode {
public:
	using iterator       = SubdivisionNode*;
	using const_iterator = SubdivisionNode* const;
	using NodeId = SubdivisionNodeId<Dimensions>;

	SubdivisionNode() = default;
	SubdivisionNode(SubdivisionNode&&) = default;
	SubdivisionNode& operator=(SubdivisionNode&&) = default;
	SubdivisionNode(const SubdivisionNode&) = delete;
	SubdivisionNode& operator=(const SubdivisionNode&) = delete;

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
		children = std::make_unique<SubdivisionNode[]>(NodeId::ChildCount);
		for (uint32_t i = 0; i < NodeId::ChildCount; i++) {
			SubdivisionNode& c = children[i];
			c.id = id;
			c.id.SetChildIndex(id.depth, i);
			c.id.depth++;
			c.children = nullptr;
		}
		return true;
	}

	inline const NodeId& GetId() const { return id; }
	inline bool          IsLeaf() const { return !children; }
	inline float GetExtent() const { return 1 / float(1 << id.depth); }
	inline float3 GetMin() const { return float3((id.index >> (32u-id.depth)) + NodeId::Coordinate(0))*GetExtent(); }
	inline float3 GetMax() const { return float3((id.index >> (32u-id.depth)) + NodeId::Coordinate(1))*GetExtent(); }
	inline std::span<SubdivisionNode> Children() const { return children ? std::span{ &children[0], NodeId::ChildCount } : std::span<SubdivisionNode>{}; }

	template<std::invocable<SubdivisionNode&> F>
	inline void Enumerate(F&& fn) {
		std::queue<SubdivisionNode*> todo;
		todo.push(this);
		for (;!todo.empty();) {
			SubdivisionNode* n = todo.front();
			todo.pop();
			fn(*n);
			if (!n->IsLeaf()) {
				for (uint32_t i = 0; i < NodeId::ChildCount; i++)
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
				for (uint32_t i = 0; i < NodeId::ChildCount; i++)
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
				for (uint32_t i = 0; i < NodeId::ChildCount; i++)
					if (childMask & (1 << i))
						todo.push(&n->children[i]);
			}
		}
	}

private:
	NodeId id = { NodeId::Coordinate(0), 0 };
	std::unique_ptr<SubdivisionNode[]> children = nullptr;
};

using OctreeNode = SubdivisionNode<3>;

}

namespace std {

template<>
struct hash<RoseEngine::OctreeNodeId> {
inline size_t operator()(const RoseEngine::OctreeNodeId& id) const {
	return RoseEngine::HashArgs(id.index[0], id.index[1], id.index[2], id.depth);
}
};

}
