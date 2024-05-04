import Scene.Scene;
import Core.Math;
import Core.Random;

using namespace RoseEngine;

ParameterBlock<Scene> scene;
RWTexture2D<float4> renderTarget;
Texture2D<uint4>    visibility;

uniform Transform worldToCamera;
uniform Transform cameraToWorld;
uniform Transform projection;
uniform Transform inverseProjection;
uniform uint2     imageSize;
uniform uint      seed;

bool LoadHit(uint4 hit, out Scene::Vertex vertex, out Material material) {
    if (hit.x == -1) {
        vertex = {};
        material = {};
        return false;
    }

    const InstanceHeader instance = scene.instances[hit.x];
    vertex = scene.LoadVertex(scene.meshes[instance.meshIndex], hit.y, asfloat(hit.zw));
    vertex = scene.TransformVertex(instance, vertex);
    material = scene.materials[instance.materialIndex];

    // Sample material images

    if (material.baseColorImage < scene.imageCount) {
        float4 rgba = scene.SampleImage(material.baseColorImage, vertex.texcoord);
        if (rgba.a < material.GetAlphaCutoff() && material.HasFlag(MaterialFlags::eAlphaCutoff))
            return false;
        material.SetBaseColor(material.GetBaseColor() * rgba.rgb);
    }

    if (material.emissionImage < scene.imageCount)
        material.SetEmission(material.GetEmission() * scene.SampleImage(material.emissionImage, vertex.texcoord).rgb);

	if (material.bumpMap < scene.imageCount) {
        float3 bump = scene.SampleImage(material.bumpMap, vertex.texcoord).rgb;
        bump = float3(bump.x*2-1, bump.y*2-1, bump.z);
        vertex.shadingNormal = normalize(
            bump.x * vertex.tangent.xyz +
            bump.y * vertex.bitangent +
            bump.z * vertex.shadingNormal);
        vertex.tangent.xyz = normalize(vertex.tangent.xyz - vertex.shadingNormal * dot(vertex.tangent.xyz, vertex.shadingNormal));
    }

    return true;
}

bool SampleLight(inout RandomSampler rng, const Scene::Vertex vertex, out float3 le, out float pdf, out float3 dir) {
    le = scene.backgroundColor;
    if (scene.backgroundImage < scene.imageCount) {
        const float2 uv = SampleTexel(scene.images[scene.backgroundImage], rng.NextFloat().xy, pdf);
        dir = sph2xyz(uv);
        pdf /= (2 * M_PI * M_PI * sqrt(1 - dir.y * dir.y));
        le *= scene.SampleImageUniform(scene.backgroundImage, uv).rgb;
    } else {
        dir = SampleUniformSphere(rng.NextFloat().xy);
        pdf = 1 / (4 * M_PI);
    }

    RayDesc ray = {};
    ray.Origin = vertex.position;
    ray.Direction = dir;
    ray.TMin = 1e-6;
    ray.TMax = 1e9;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rq;
    rq.TraceRayInline(scene.accelerationStructure, 0, 1, ray);
    while (rq.Proceed()) {}
    return rq.CommittedStatus() == COMMITTED_NOTHING;
}

float3 Evaluate(int2 id, uint4 hit) {
    Scene::Vertex vertex;
    Material material;
    if (!LoadHit(hit, vertex, material)) {
        float3 bg = scene.backgroundColor;
        if (scene.backgroundImage < scene.imageCount) {
            const float2 clip = 2 * (id + .5) / float2(imageSize) - 1;
            const float3 dir = ((cameraToWorld * float4(normalize(inverseProjection * float3(clip.x, -clip.y, 1)), 0)).xyz);
            bg *= scene.SampleImageUniform(scene.backgroundImage, xyz2sph(dir)).rgb;
        }
        return bg;
	}

    RandomSampler rng = RandomSampler(seed, id);

    float3 le = material.GetEmission();

    float3 lightColor;
    float3 lightDir;
    float pdf;
    if (SampleLight(rng, vertex, lightColor, pdf, lightDir))
        le += material.GetBaseColor() * abs(dot(vertex.shadingNormal, lightDir)) * lightColor / pdf;

    return le;
}

[shader("compute")]
[numthreads(8,4,1)]
void main(uint3 index: SV_DispatchThreadID) {
    if (any(index.xy >= imageSize)) return;
    const float3 r = Evaluate(index.xy, visibility[index.xy]);
    renderTarget[index.xy] = float4(r, 1);
}