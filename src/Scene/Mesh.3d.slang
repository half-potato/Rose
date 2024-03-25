import GBuffer;
import Transform;

using namespace RoseEngine;

StructuredBuffer<Transform> objectTransforms;

uniform Transform worldToCamera;
uniform Transform projection;

struct v2f {
    float4 pos: SV_POSITION;
    float3 color: COLOR0;
	uint instanceId: TEXCOORD0;
};

[shader("vertex")]
v2f vertexMain(float3 pos: POSITION, float3 color: COLOR0, uint instanceId: SV_InstanceID) {
    v2f o = {};

    const float3 cameraPos = (worldToCamera * objectTransforms[instanceId]) * pos;

    o.pos = projection * float4(cameraPos, 1);
    o.pos.y = -o.pos.y;
    o.color = color;
    o.instanceId = instanceId;
    return o;
}

[shader("fragment")]
GBuffer fragmentMain(v2f i) {
    GBuffer r = {};
    r.color = float4(i.color, 1);
    r.visibility = uint4(i.instanceId, 0, 0, 0);
    return r;
}