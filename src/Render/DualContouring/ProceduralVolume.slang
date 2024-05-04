#pragma once

import Scene.Transform;
import Render.Terrain.voronoise;

using namespace RoseEngine;

/*
#ifdef PROCEDURAL_NODE_SRC
#define add(a, b) ((a) + (b))
#define sub(a, b) ((a) - (b))
PROCEDURAL_NODE_SRC
#else*/
struct ProceduralNodeArgs : IDifferentiable { float3 position; };
struct ProceduralEvalResult : IDifferentiable { float density; };
[Differentiable]
ProceduralEvalResult GetDensity(ProceduralNodeArgs args) {
    float vn = 0;//2*voronoise(3*args.position);

    float3 p = args.position + 10;
    float h = 1;// + map4(1.5*(p + 2*map4(2*p)));

	return { vn + h - length(args.position) };
}
//#endif

struct ProceduralVolume {
    float Sample(float3 pos) {
        return GetDensity( { pos }).density;
    }
    float3 SampleGradient(float3 pos) {
        var arg = diffPair<ProceduralNodeArgs>( { pos });
        __bwd_diff(GetDensity)(arg, { 1.0 });
        return -arg.d.position;
    }
};