import Render.GBuffer;
import Render.Transform.Transform;
import Render.Scene.Material;

using namespace RoseEngine;

uniform Transform worldToCamera;
uniform Transform projection;

struct InstanceHeader {
    uint transformIndex;
    uint materialIndex;
};
StructuredBuffer<InstanceHeader> instances;
StructuredBuffer<Transform>      transforms;
StructuredBuffer<Material>       materials;
SamplerState sampler;
Texture2D<float4>                images[1024];

struct v2f {
    float4 pos: SV_POSITION;
	float3 normal: NORMAL;
	float2 uv: TEXCOORD0;
	uint instanceId: TEXCOORD0;
};

[shader("vertex")]
v2f vertexMain(float3 pos: POSITION, float3 normal: NORMAL, float2 uv: TEXCOORD0, uint instanceId: SV_InstanceID) {
    const float3 cameraPos = (worldToCamera * transforms[instances[instanceId].transformIndex]) * pos;

    v2f o = {};
    o.pos = projection * float4(cameraPos, 1);
    o.pos.y = -o.pos.y;
    o.normal = normal;
    o.instanceId = instanceId;
    o.uv = uv;
    return o;
}

[shader("fragment")]
GBuffer fragmentMain(float3 normal: NORMAL, float2 uv: TEXCOORD0, uint instanceId: TEXCOORD0) {
    const Material material = materials[instances[instanceId].materialIndex];

	float3 normal = normalize(normal) * .5 + .5;

    float3 color = material.baseColor;
    if (material.baseColorImage != -1) color *= images[material.baseColorImage].Sample(sampler, uv).rgb;

    GBuffer r = {};
    r.color      = float4(color, 1);
    r.visibility = uint4(instanceId, 0, 0, 0);
    return r;
}