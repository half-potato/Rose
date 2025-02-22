import Rose.Scene.Scene;
import Rose.Core.Random;
#include <Core/RoseEngine.h>

using namespace RoseEngine;

float cross2(float2 v0, float2 v1) { return v0.x * v1.y - v0.y * v1.x; }

struct AABB
{
    float2 min;
    float2 max;

    __subscript(int index) -> float2 {
        get { return index == 0 ? min : max; }
        set { if (index == 0) min = newValue; else max = newValue; }
	}
};

bool Intersect(const AABB b0, const AABB b1) { return !(b0.min.x > b1.max.x || b1.min.x > b0.max.x || b0.min.y > b1.max.y || b1.min.y > b0.max.y); }

enum IntersectionResult
{
    eDisjoint,
    eIntersect,
    eContain
};

struct TriBoundHelper {
    __init(float2 tri[3]) {
        for (uint32_t i = 0; i < 3; ++i) {
            bound.expand(tri[i]);
            dir[i] = tri[(i + 1) % 3] - tri[i];
            axis[i] = float2(dir[i].y, -dir[i].x);
            axisNegDir[i][0] = axis[i].x < 0.0f;
            axisNegDir[i][1] = axis[i].y < 0.0f;
            axisPosDir[i][0] = !axisNegDir[i][0];
            axisPosDir[i][1] = !axisNegDir[i][1];
            tmin[i] =  ((1 << 31) - 1);
            tmax[i] = -((1 << 31) - 1);
            for (uint32_t j = 0; j < 3; ++j) {
                float p = dot(axis[i], tri[j]);
                tmin[i] = min(tmin[i], p);
                tmax[i] = max(tmax[i], p);
            }
        }
    }

    AABB bound;
    float2 dir[3];
    float2 axis[3];
    float tmin[3];
    float tmax[3];
    bool axisNegDir[3][2];
    bool axisPosDir[3][2];
};
IntersectionResult Intersect(float2 tri[3], TriBoundHelper help, AABB bound) {
    if (!Intersect(help.bound, bound))
		return IntersectionResult::eDisjoint;

	for (uint i = 0; i < 3; ++i) {
		float tmin = help.axis[i].x * bound[help.axisNegDir[i][0]].x + help.axis[i].y * bound[help.axisNegDir[i][1]].y;
		float tmax = help.axis[i].x * bound[help.axisPosDir[i][0]].x + help.axis[i].y * bound[help.axisPosDir[i][1]].y;
		if (help.tmin[i] >= tmax || help.tmax[i] <= tmin)
			return IntersectionResult::eDisjoint;

	}

	for (uint i = 0; i < 4; ++i) {
		float2 corner = float2(bound[i >> 1].x, bound[i & 1].y);
		for (uint32_t j = 0; j < 3; ++j)
			if (cross2(corner - tri[j], help.dir[j]) < 0)
                return IntersectionResult::eIntersect;
    }

    return IntersectionResult::eContain;
}

struct VisibilityBVH
{
    static const uint kLabelBits = 16;
    static const uint kInvalidLabel = 0xffff;
    static const uint kInvalidNode = -1;

    struct Polygon {
        uint startVertex = 0;
        uint bits = kInvalidLabel;

        property uint label {
            get { return BF_GET(bits, 0, kLabelBits); }
            set { BF_SET(bits, newValue, 0, kLabelBits)}
		}
        property uint vertexCount {
            get { return BF_GET(bits, kLabelBits, 32-kLabelBits); }
            set { BF_SET(bits, newValue, kLabelBits, 32-kLabelBits)}
		}
    };

    enum NodeFlags {
		eNone = 0,
        eLeaf = 1,
    };

    struct InternalNodeData {
        uint children[2];
    };
    struct LeafNodeData {
        Polygon polygon;
    };
    struct Node {
		AABB  aabb;
        uint  parentNode;
        uint  flag;
        uint2 data; // InternalNodeData or LeafNodeData

        bool isLeaf() { return (flag & NodeFlags::eLeaf) != 0; }

        property LeafNodeData leaf {
			get { return reinterpret<LeafNodeData>(data); }
			set { data = reinterpret<uint2>(newValue); }
		}
        property InternalNodeData internal {
			get { return reinterpret<InternalNodeData>(data); }
			set { data = reinterpret<uint2>(newValue); }
		}
    };

    RWStructuredBuffer<Node> nodeBuffer;
    RWStructuredBuffer<int2> vertexBuffer;

    Polygon Intersect(Polygon p, int2 tri[3]) {

	}

	uint TraverseLeft(float2 tri[3], uint n) {
		Node node = nodeBuffer[n];
		if (!node.aabb.Intersect(tri))
			return kInvalidNode;
		while (!node.isLeaf()) {
			n = node.internal.children[0];
			node = nodeBuffer[n];
			if (node.aabb.Intersect(tri))
				continue;
			// left leaf doesnt overlap tri
			// try right leaf
			n = node.internal.children[1];
			node = nodeBuffer[n];
			if (!node.aabb.Intersect(tri))
				return kInvalidNode; // neither leaf overlaps tri
		}
		return n;
	}

    struct VbvhIterator
    {
		uint cur;

        __init(VisibilityBVH vbvh, float2 tri[3]) {
            cur = vbvh.TraverseLeft(tri, 0);
		}

        [mutating]
        void Next(VisibilityBVH vbvh, float2 tri[3]) {
            Node node = vbvh.nodeBuffer[cur];

			if (node.parentNode == kInvalidNode) {
				// cur is root (should this happen?)
				cur = kInvalidNode;
				return;
			}

			Node parent = vbvh.nodeBuffer[node.parentNode];

            // traverse up until cur is a left child of its parent
            while (cur == parent.internal.children[1]) {
				// node is right child
                if (node.parentNode == kInvalidNode) {
                    // can't move up
                    cur = kInvalidNode;
                    return;
                }
				// move up a level
                cur = node.parentNode;
                node = parent;
                parent = vbvh.nodeBuffer[node.parentNode];
            }

            // find the leftmost leaf under the right child of parent
            cur = vbvh.TraverseLeft(tri, parent.internal.children[1]);
		}
    };

    void Insert(float2 tri[3], uint label) {
        int2 triI32[3];
        triI32[0] = int2(((1 << 31) - 1) * tri[0].xy);
        triI32[1] = int2(((1 << 31) - 1) * tri[1].xy);
        triI32[2] = int2(((1 << 31) - 1) * tri[2].xy);

		for (VbvhIterator it = VbvhIterator(this, tri); it.cur != kInvalidNode; it.Next(this, tri)) {
            Node pk = nodeBuffer[it.cur];
            Polygon rk = Intersect(pk.leaf.polygon, tri);
            if (rk.label == label)
                continue;

			// decompose rk into convex regions

		}
	}

    void Insert(float3 v0, float3 v1, float3 v2, uint label) {
        Insert(tri, label);
	}

    uint Query(float2 p) {
        return kInvalidLabel;
	}
};

ParameterBlock<Scene> scene;
ParameterBlock<VisibilityBVH> vbvh;

RWTexture2D<float4> color;
uniform uint selected;
uniform uint2 imageSize;
uniform Transform worldToCamera;
uniform Transform projection;

[shader("compute")]
[numthreads(1, 1, 1)]
void build(uint3 index: SV_DispatchThreadID) {
    InstanceHeader  instance  = scene.instances[selected];
    VertexAttribute triAttrib = scene.meshes[instance.meshIndex].triangles;
    VertexAttribute posAttrib = scene.meshes[instance.meshIndex].positions;
    Transform       mvp = projection * worldToCamera * scene.transforms[instance.transformIndex];
    for (uint primitiveIndex = 0; primitiveIndex < instance.triangleCount; primitiveIndex++) {
        uint3 tri = scene.LoadTriangleIndicesUniform(triAttrib, primitiveIndex);

        float3 lv0, lv1, lv2;
        scene.LoadTriangleAttributeUniform(posAttrib, tri, lv0, lv1, lv2);

        float4 v0 = mvp.ProjectPointUnnormalized(lv0);
        float4 v1 = mvp.ProjectPointUnnormalized(lv1);
        float4 v2 = mvp.ProjectPointUnnormalized(lv2);

        v0.y = -v0.y;
        v1.y = -v1.y;
        v2.y = -v2.y;

        if (v0.w == 0 || v1.w == 0 || v2.w == 0)
            continue;

        v0.xyz /= v0.w;
        v1.xyz /= v1.w;
        v2.xyz /= v2.w;

        vbvh.Insert(
			v0.xyz,
			v1.xyz,
			v2.xyz,
			primitiveIndex);
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void render(uint3 index: SV_DispatchThreadID) {
    if (any(index.xy >= imageSize))
        return;

    float2 ndc = ((index.xy + float2(0.5)) / float2(imageSize)) * 2 - 1;

    uint primitiveId = vbvh.Query(ndc);

    if (primitiveId != VisibilityBVH::kInvalidLabel)
        color[index.xy] = float4((uint3(pcg(0 + pcg(primitiveId)), pcg(1 + pcg(primitiveId)), pcg(2 + pcg(primitiveId)))&0xff)/float(0xff), 1);
}
