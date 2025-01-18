import Render.GBuffer;
import Scene.Transform;
import Render.Terrain.Terrain;

using namespace RoseEngine;

ByteAddressBuffer aabbs;

uniform Transform worldToNDC;

[shader("vertex")]
float4 vertexMain(uint vertexId: SV_VertexID, uint instanceId: SV_InstanceID, out float3 color : COLOR0) : SV_Position {
    const uint address = instanceId * (sizeof(float3) * 3);
    const float3 aabbMin = aabbs.Load<float3>(address);
    const float3 aabbMax = aabbs.Load<float3>(address + sizeof(float3));

    uint indices[] = {
		// bottom
        0, 1,
        0, 2,
        1, 3,
        2, 3,

		// top
        4, 5,
        4, 6,
        5, 7,
        6, 7,

		// bottom -> top
        0, 4,
        1, 5,
        2, 6,
        3, 7,
    };
    const uint v = indices[vertexId];

    color = aabbs.Load<float3>(address + sizeof(float3) * 2);

    float4 clip = worldToNDC.ProjectPointUnnormalized( lerp(aabbMin, aabbMax, float3(v & 1, (v >> 1) & 1, (v >> 2) & 1)) );
    clip.y = -clip.y;
    return clip;
}

[shader("fragment")]
GBuffer fragmentMain(float3 color: COLOR0) {
    GBuffer o = {};
    o.color = float4(color, 1);
    o.visibility = uint4(0, 0, 0, 0);
    return o;
}
