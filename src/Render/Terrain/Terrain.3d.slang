import Render.GBuffer;
import Scene.Transform;
import Core.PackedTypes;
import cbt.leb;
import voronoise;

[Differentiable]
float GetHeight(float3 position)
{
	return 1;
}


using namespace RoseEngine;

uniform Transform worldToNDC;

uniform float3 lightDir;
uniform float targetSize;
uniform uint2 screenSize;

[[vk::push_constant]]
cbuffer PushConstants {
    uint cbtID;
    uint split;
};

#if 1
[Differentiable]
float3 GetPosition(float2 uv) {
    uv -= 0.5;

    float3 p = float3(0.5, uv.y, uv.x);
    p = normalize(p);

    return p * GetHeight(p);
}
#else
[Differentiable]
float3 GetPosition(float2 uv) {
    float3 p = FromOctahedral(float2(2*uv.x-1, -2*uv.y+1));
	return p * GetHeight(p);
}
#endif

float3x2 GetNodeUvs(const cbt_Node node) {
    float3x2 faceVertices = float3x2(1, 0, 0, 0, 0, 1);

    int bitID = max(0, node.depth - 1);
    float3x3 xf;
    {
        uint quadBit = leb__GetBitValue(node.id, bitID);
        float b = float(quadBit);
        float c = 1.0f - b;
        xf = float3x3(
            c, 0.0f, b,
            b, c, b,
            b, 0.0f, c);
    }
    for (bitID = node.depth - 2; bitID >= 0; --bitID) {
        uint splitBit = leb__GetBitValue(node.id, bitID);
        float b = float(splitBit);
        float c = 1 - b;
        xf = mul(float3x3(
                       c, b, 0,
                       0.5f, 0, 0.5f,
                       0, c, b),
                   xf);

	}
    xf = mul(leb__WindingMatrix((node.depth ^ 1) & 1), xf);

    return mul(xf, faceVertices);
}

float3x4 GetProjectedNodeVertices(const cbt_Node node, const float3x2 uvs) {
    float3x4 verts;
    for (uint i = 0; i < 3; i++) {
		verts[i] = worldToNDC.ProjectPointUnnormalized(GetPosition(uvs[i]));
        verts[i].y = -verts[i].y;
    }
    return verts;
}

bool ShouldSplit(const cbt_Node node) {
    const float3x2 uvs = GetNodeUvs(node);
    const float3x4 clipPos = GetProjectedNodeVertices(node, uvs);
	int2 minv =  int2(screenSize);
	int2 maxv = -int2(screenSize);
    int2 verts[3];
    for (uint i = 0; i < 3; i++) {
        const float4 v = clipPos[i];
		verts[i] = int2(screenSize * (v.xy / v.w * .5 + .5));
        minv = min(minv, verts[i]);
        maxv = max(maxv, verts[i]);
    }

    if (any(maxv < 0) || any(minv >= screenSize))
        return false;

    const float2 extent = maxv - minv;
    float m = max(extent.x, extent.y) / targetSize;
    if (m <= 1)
        return false;
	if (m > 20)
		return true;
	return cross(float3(verts[0] - verts[1], 0), float3(verts[2] - verts[1], 0)).z > 0;
}

[shader("compute")]
[numthreads(32, 1, 1)]
void Subdivide(uint3 threadId: SV_DispatchThreadID) {
    uint threadID = threadId.x;

    uint count = cbt_NodeCount(cbtID);
    if (threadID >= count) return;

	let node = cbt_DecodeNode(cbtID, threadID);
	if (split == 0) {
        const leb_DiamondParent diamondParent = leb_DecodeDiamondParent_Square(node);
        const bool parentsSplit = ShouldSplit(diamondParent.base) || ShouldSplit(diamondParent.top);
        if (!parentsSplit) {
			leb_MergeNode_Square(cbtID, node, diamondParent);
		}
	} else {
		if (ShouldSplit(node)) {
			leb_SplitNode_Square(cbtID, node);
		}
	}
}

[shader("vertex")]
float4 vertexMain(uint instanceId: SV_InstanceID, uint vertexId: SV_VertexID, out float2 uv: TEXCOORD0) : SV_Position  {
    let node = cbt_DecodeNode(cbtID, instanceId);
    const float3x2 uvs = GetNodeUvs(node);
    uv = uvs[vertexId];
    return GetProjectedNodeVertices(node, uvs)[vertexId];
}

[shader("fragment")]
GBuffer fragmentMain(float2 uv : TEXCOORD0) {
    float3 dpdu = __fwd_diff(GetPosition)(diffPair(uv, float2(1, 0))).d;
    float3 dpdv = __fwd_diff(GetPosition)(diffPair(uv, float2(0, 1))).d;
    float3 normal = normalize(cross(dpdv, dpdu));
	float3 brdf = max(0, dot(normal, lightDir));
    float3 Le = float3(.3, .8, .4);

    float3 c = Le * brdf + max(normal.y, 0) * float3(0.1, 0.2, 0.1);

    GBuffer o = {};
    o.color = float4(c, 1);
    o.visibility = uint4(0, 0, 0, 0);
    return o;
}
