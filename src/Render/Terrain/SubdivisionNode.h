#pragma once

#include <Core/RoseEngine.h>

namespace RoseEngine {

CPP_TEMPLATE(uint, Dimensions)
struct SubdivisionNodeId SLANG_GENERIC(uint, Dimensions) {
	static const uint ChildCount = 1u << Dimensions;
	static const uint ChildIndexBitmask = ChildCount - 1;

	#ifdef __cplusplus
	typedef glm::vec<Dimensions, uint32_t> Coordinate;
	#else
	typedef vector<uint, Dimensions> Coordinate;
	#endif

	Coordinate index; // 32 levels max
	uint depth = 0;

	// gets offset of node at childDepth.
	// e.g., for d=0, gets the index in the root
	inline uint GetChildIndex(const uint d) CPP_CONST {
		uint bit = 31 - d;
		uint i = 0;
		for (uint dim = 0; dim < Dimensions; dim++)
			i |= BF_GET(index[dim], bit, 1) << dim;
		return i;
	}

	SLANG_MUTATING
	inline void SetChildIndex(const uint d, const uint i) {
		uint bit = 31 - d;
		for (uint dim = 0; dim < Dimensions; dim++)
			BF_SET(index[dim], i >> dim, bit, 1);
	}

	inline SubdivisionNodeId<Dimensions> GetParent() CPP_CONST {
		if (depth == 0) return THIS_REF;
		uint bit = 32 - depth;
		SubdivisionNodeId<Dimensions> n = THIS_REF;
		n.index &= Coordinate(~(1 << bit));
		n.depth -= 1;
		return n;
	}

    inline SubdivisionNodeId<Dimensions> GetInnerNeighbor(uint axis, uint d) CPP_CONST {
		SubdivisionNodeId<Dimensions> n = THIS_REF;
		n.index[axis] ^= 1 << (31 - d);
		return n;
	}

    inline SubdivisionNodeId<Dimensions> GetInnerNeighbor(uint axis) CPP_CONST {
		return depth == 0 ? THIS_REF : GetInnerNeighbor(axis, depth-1);
	}

    inline bool GetOuterNeighbor(uint axis, OUT_ARG(SubdivisionNodeId<Dimensions>, n)) CPP_CONST {
        n = THIS_REF;
		if (depth < 2) return false;
		bool dir = BF_GET(index[axis], 32 - depth, 1) == 1;
		for (int d = (int)depth; d > 0; d--) {
			n.index[axis] ^= 1 << (32 - d);
			if ((dir && n.index[axis] > index[axis]) || (!dir && n.index[axis] < index[axis]))
				return true;
		}

		return false;
	}

	#ifdef __cplusplus
	inline bool operator==(const SubdivisionNodeId<Dimensions>& rhs) const {
		bool r = rhs.depth == depth;
		for (uint i = 0; i < Dimensions; i++)
			r = r && (rhs.index[i] == index[i]);
		return r;
	}
	#endif
};

typedef SubdivisionNodeId<3> OctreeNodeId;

}