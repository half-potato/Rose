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
TerrainSample SampleTerrain(TerrainSampleArgs args)
{
	float vn = 0;//2*voronoise(3*args.position);

	float3 p = args.position + 10;
	float h = 1;// + map4(1.5*(p + 2*map4(2*p)));

	return { vn + h - length(args.position) };
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