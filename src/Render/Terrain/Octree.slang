#pragma once
#include <Core/RoseEngine.h>

/*
struct BitBuffer {
	RWByteAddressBuffer data;

    __subscript(uint2 startLen) -> uint
    {
        // |           bits.x               |              bits.y            |
	    // |                     _______________________________
        // |                    /              value            \
        // |-------------------|------------|--------------------|-----------|
        //  \_________________/              \__________________/
 		//       offset1                            offset2
        get {
            uint2 bits = data.Load2(startLen.x / 32);
            uint offset1 = startLen.x % 32;
            int offset2 = int(offset1 + startLen.y) - 32;

            uint v = BF_GET(bits[0], offset1, min(startLen.y, 32 - offset1));
            if (offset2 > 0)
                v |= BF_GET(bits[1], 0, offset2) << (32 - offset1);
            return v;
        }
        set {
            uint2 bits = data.Load2(startLen.x / 32);
            uint offset1 = startLen.x % 32;
            int offset2 = int(offset1 + startLen.y) - 32;

            BF_SET(bits[0], newValue, offset1, min(startLen.y, 32 - offset1));
            if (offset2 > 0)
                BF_SET(bits[1], newValue >> (32 - offset1), 0, offset2);
            data.Store2(startLen.x / 32, bits);
		}
	}
};
*/

struct OctreeNode {
    uint3 id;
    uint  depth; // 0=root

    __init(uint3 id_, uint depth_) {
        id = id_;
        depth = depth_;
	}

    inline OctreeNode GetParent() {
		if (depth == 0) return this; // root
        OctreeNode n;
		n.id = id >> 1;
		n.depth = depth - 1;
		return n;
	}

    inline OctreeNode GetChild(uint i) {
        OctreeNode n;
        n.id = (id << 1) | (uint3(i, i>>1, i>>2)&1);
		n.depth = depth + 1;
		return n;
	}

	// gets the neighbor along axis that shares the same parent
    inline OctreeNode GetInnerNeighbor(uint axis)  {
        OctreeNode n = this;
		if (depth > 0)
        	n.id[axis] ^= 1;
		return n;
	}

	// gets the neighbor along axis that does not share the same parent
	// n is always set to the same depth, regardless of whether or not the node exists
    inline bool GetOuterNeighbor(uint axis, out OctreeNode n) {
        n = this;
        if (depth < 1)
			return false;
        bool dir = id[axis] == 0;
		for (int d = 0; d < depth; d++) {
            n.id[axis] ^= (1 << d);
            if ((id[axis] == 0) && (n.id[axis] < id[axis]) ||
				(id[axis] == 1) && (n.id[axis] > id[axis]))
                return true;
		}
		return false;
	}
};

struct Octree {
    RWByteAddressBuffer heap;

    uint HeapRead(OctreeNode node) {

	}

    uint NodeCount() {
        return HeapRead(OctreeNode(1u, 0));
	}

	int MaxDepth() {
		return firstbitlow(heap.Load(0));
	}
    bool IsCeilNode(OctreeNode node)
	{
		return (node.depth == MaxDepth());
    }
    static bool IsRootNode(OctreeNode node)
	{
		return all(node.id == 1u);
    }
    static bool IsNullNode(OctreeNode node)
	{
		return all(node.id == 0u);
	}

    void SplitNode_Fast(OctreeNode node) {
		_HeapWrite_BitField(RightChildNode(node), 1u);
    }
    void SplitNode(OctreeNode node) {
		if (!IsCeilNode(node))
			SplitNode_Fast(node);
	}

    void MergeNode_Fast(OctreeNode node) {
		_HeapWrite_BitField(cbtID, RightSiblingNode(node), 0u);
    }
    void MergeNode(OctreeNode node) {
		if (!IsRootNode(node))
			MergeNode_Fast(cbtID, node);
	}

    OctreeNode DecodeNode(uint index) {
		OctreeNode node = OctreeNode(1u, 0);
        while (HeapRead(node) > 1u) {
            [unroll]
            for (uint i = 0; i < 8; i++) {
                OctreeNode child = node.GetChild(i);
                uint cmp = HeapRead(child);
                uint b = (index < cmp) ? 0u : 1u;

                node = child;
				node.id |= b;
				index -= cmp * b;
            }
		}

		return node;
	}
    uint EncodeNode(OctreeNode node) {
        uint index = 0;
        return index;
    }
};