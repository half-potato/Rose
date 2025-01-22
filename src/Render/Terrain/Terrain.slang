#pragma once

import Scene.Transform;
import Render.Terrain.voronoise;

using namespace RoseEngine;

struct TerrainSampleArgs : IDifferentiable
{
	float3 position;
};
struct TerrainSample : IDifferentiable
{
	float density;
};

[Differentiable]
TerrainSample SampleTerrain(TerrainSampleArgs args) {
    float3 p = args.position + 10;
    float h = 0.5 + 0.5*map4(3*p + 3*map4(4*p));
	return { h - length(args.position) };
}

struct Terrain
{
    float Sample(float3 pos)
	{
        return SampleTerrain( { pos }).density;
    }
    float3 SampleGradient(float3 pos)
	{
        var arg = diffPair<TerrainSampleArgs>( { pos });
        __bwd_diff(SampleTerrain)(arg, { 1.0 });
        return -arg.d.position;
    }
};