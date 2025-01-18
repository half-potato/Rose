import Render.GBuffer;
import Scene.Transform;
import Render.Terrain.Terrain;

using namespace RoseEngine;

uniform Transform worldToNDC;
uniform float3    lightDir;
uniform Terrain   terrain;

[shader("vertex")]
float4 vertexMain(in float3 vertex: POSITION, out float3 pos: TEXCOORD0) : SV_Position
{
    pos = vertex;

    float4 clip = worldToNDC.ProjectPointUnnormalized(vertex);
    clip.y = -clip.y;
    return clip;
}

[shader("fragment")]
GBuffer fragmentMain(float3 pos: TEXCOORD0)
{
    float3 normal = normalize(terrain.SampleGradient(pos));
	float3 brdf = max(0, dot(normal, lightDir));
    float3 Le = float3(.3, .8, .4);

    float3 c = Le * brdf + max(normal.y, 0) * float3(0.1, 0.2, 0.1);

    GBuffer o = {};
    o.color = float4(c, 1);
    o.visibility = uint4(-1, 0, 0, 0);
    return o;
}
