#pragma once

import Render.Terrain.Terrain;
import Render.Terrain.Octree;

ParameterBlock<Octree> lod;

[shader("compute")]
[numthreads(32, 1, 1)]
void computeLod(uint3 index: SV_DispatchThreadID) {
    if (index.x >= lod.NodeCount())
        return;
    OctreeNode node = lod.DecodeNode(index.x);
}