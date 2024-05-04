import Render.GBuffer;
import Scene.Scene;

using namespace RoseEngine;

uniform Transform worldToCamera;
uniform Transform projection;

ParameterBlock<Scene> scene;

struct v2f {
    float4 pos: SV_Position;
	nointerpolation uint instanceId: TEXCOORD0;
    float2 uv: TEXCOORD1;
};

[shader("vertex")]
v2f vertexMain(float3 pos: POSITION, float2 uv: TEXCOORD0, uint instanceId: SV_InstanceID) {
    v2f o = {};
    o.pos = projection * float4((worldToCamera * scene.transforms[scene.instances[instanceId].transformIndex]) * pos, 1);
    o.pos.y = -o.pos.y;
    o.instanceId = instanceId;
	o.uv = uv;
    return o;
}

[shader("fragment")]
GBuffer fragmentMain(v2f i, uint primId: SV_PrimitiveID, float3 bary: SV_Barycentrics) {
	#ifdef USE_ALPHA_CUTOFF
    const Material m = scene.materials[scene.instances[i.instanceId].materialIndex];
    if (m.baseColorImage < scene.imageCount && m.HasFlag(MaterialFlags::eAlphaCutoff)) {
        if (scene.images[m.baseColorImage].Sample(scene.sampler, i.uv).a < m.GetAlphaCutoff())
            discard;
	}
	#endif
    GBuffer r = {};
    r.color      = float4(0, 0, 0, 1);
    r.visibility = uint4(i.instanceId, primId, asuint(bary.yz));
    return r;
}