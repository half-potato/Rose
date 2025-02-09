#pragma once

// Ported from https://github.com/Tuntenfisch/Voxels

import Render.Terrain.Terrain;
import Scene.Transform;
import Core.Indirect;
import Render.cbt.cbt;
#include "SubdivisionNode.h"

using namespace RoseEngine;

// Ported from https://github.com/Tuntenfisch/Voxels

// Corner indices:
//       6---------7
//      /|        /|
//     / |       / |
//    /  |      /  |
//   2---------3   |
//   |   |     |   |
//   |   4-----|---5
// y |  /      |  /
//   | /       | /  z
//   |/        |/
//   0---------1
//       x
// Edge indices:
//     +---11----+
//    /|        /|
//   2 |       3 |
//  /  6      /  7
// +----9----+   |
// |   |     |   |
// |   +---10|---+
// 4  /      5  /
// | 0       | 1
// |/        |/
// +----8----+
uint3 GetCornerIndex(uint i) { return uint3(i, i >> 1, i >> 2) & 1; }
static const uint2 kCellEdgeIndices[12] = {
    uint2(0, 4),
    uint2(1, 5),
    uint2(2, 6),
    uint2(3, 7),

    uint2(0, 2),
    uint2(1, 3),
    uint2(4, 6),
    uint2(5, 7),

    uint2(0, 1),
    uint2(2, 3),
    uint2(4, 5),
    uint2(6, 7)
};

struct DualContourMesh {
	RWByteAddressBuffer      counters;
	RWStructuredBuffer<uint> cellVertexIds;

    RWByteAddressBuffer vertices;
    RWByteAddressBuffer connectedVertices;
    RWByteAddressBuffer triangles;

    RWStructuredBuffer<VkDrawIndexedIndirectCommand> drawIndirectArgs;
    RWStructuredBuffer<uint3> dispatchIndirectArgs;
    RWByteAddressBuffer avgError;

    uint IncrementVertexCounter() {
        uint v;
        counters.InterlockedAdd(0, 1, v);
        return v;
    }
    uint IncrementTriangleCounter() {
        uint v;
        counters.InterlockedAdd(4, 1, v);
        return v;
    }
    uint GetVertexCount() {
        return counters.Load(0);
    }
    uint GetTriangleCount() {
        return counters.Load(4);
	}
};

struct DualContourMesher {
	static const uint kNullVertexIndex = -1;

    Terrain terrain;

    DualContourMesh mesh;
    DualContourMesh neighborMesh0;
    DualContourMesh neighborMesh1;
    DualContourMesh neighborMesh2;

    OctreeNodeId neighborIds[3];
    OctreeNodeId nodeId;

    float3 gridWorldMin;
	uint   schmitzParticleIterations;
    float3 gridWorldMax;
    float  schmitzParticleStepSize;
    uint3  gridSize;

    uint FlattenCellIndex(uint3 coordinate) {
        return coordinate.x + (gridSize.x+1) * (coordinate.y + (gridSize.y+1) * coordinate.z);
    }

    float3 gridToWorld   (const float3 gridPos) { return ((gridPos-0.5)/float3(gridSize-1)) * (gridWorldMax - gridWorldMin) + gridWorldMin; }
    float  Sample        (const float3 gridPos) { return terrain.Sample(gridToWorld(gridPos)); }
    float3 SampleGradient(const float3 gridPos) { return terrain.SampleGradient(gridToWorld(gridPos)); }

    struct SchmitzOptimizer {
        float3 vertex;
        float3 forces[8];

        __init(const DualContourMesher dc, uint3 cellID, out bool valid) {
            // find intersections with boundary along cell edges
			vertex = 0;
            uint numIntersections = 0;
            struct PointAndNormal {
                float3 p;
                float3 n;
            };
            PointAndNormal intersections[12];
			for (uint i = 0; i < 12; i++) {
				const uint2 cellEdge = kCellEdgeIndices[i];
				const float sampleA = dc.Sample(float3(cellID + GetCornerIndex(cellEdge.x)));
				const float sampleB = dc.Sample(float3(cellID + GetCornerIndex(cellEdge.y)));
				if (sign(sampleA) == sign(sampleB))
					continue;

				// s0    s=0    s1
				// p0 ---- p -- p1
				// p = p0 + (p1 - p0)*t
				// 0 = s0 + (s1 - s0)*t
				// t=-s0/(s1-s0)

                const float t = abs(sampleB - sampleA) > 1e-6
					? -sampleA / (sampleB - sampleA)
					: 0.5;

                const float3 p = lerp(float3(GetCornerIndex(cellEdge.x)), float3(GetCornerIndex(cellEdge.y)), t);
                vertex += p;

                intersections[numIntersections].p = p;
                intersections[numIntersections].n = normalize(dc.SampleGradient(cellID + p));

                numIntersections++;
			}
            if (numIntersections > 0)
                vertex /= numIntersections;

			// calculate forces
            for (uint corner = 0; corner < 8; corner++) {
                forces[corner] = 0;

                [unroll(12)]
                for (uint i = 0; i < numIntersections; i++) {
                    const float dist = dot(intersections[i].n, GetCornerIndex(corner) - intersections[i].p);
                    const float3 voxelCornerToPlaneVector = -dist * intersections[i].n;
                    forces[corner] += voxelCornerToPlaneVector;
				}
			}

            valid = numIntersections > 0;
		}

		[mutating]
		void Step(const float step) {
            float3 force = lerp(
                lerp(
                    lerp(forces[0], forces[1], vertex.x),
                    lerp(forces[2], forces[3], vertex.x),
                    vertex.y),
                lerp(
                    lerp(forces[4], forces[5], vertex.x),
                    lerp(forces[6], forces[7], vertex.x),
                    vertex.y),
                vertex.z);

            vertex = saturate(vertex + force * step);
		}
	};

    void GenerateVertex(uint3 cellID) {
		uint vertexId = kNullVertexIndex;

        bool valid;
		SchmitzOptimizer s = SchmitzOptimizer(this, cellID, valid);
		if (valid) {
			for (uint i = 0; i < schmitzParticleIterations; i++)
                s.Step(schmitzParticleStepSize);

            float3 p = float3(cellID) + saturate(s.vertex);
            mesh.avgError.InterlockedAddF32(0, abs(Sample(p)) * length((gridWorldMax - gridWorldMin)/float3(gridSize-1)));

            vertexId = mesh.IncrementVertexCounter();
			mesh.vertices.Store<float3>(vertexId*sizeof(float3), gridToWorld(p));
		}

        mesh.cellVertexIds[FlattenCellIndex(cellID)] = vertexId;
	}
    void GenerateTriangles(uint3 cellID) {
        uint v0 = mesh.cellVertexIds[FlattenCellIndex(cellID)];
        if (v0 == kNullVertexIndex) return;

		static const uint3 corners[3] = {
			uint3(4, 6, 2), // -x
			uint3(1, 5, 4), // -y
			uint3(2, 3, 1), // -z
		};
		static const uint3 edgeIndices = uint3(11, 7, 3);

		for (uint index = 0; index < 3; index++) {
            if (cellID[(index + 1) % 3] + 1 >= gridSize[(index + 1) % 3])
                continue;
            if (cellID[(index + 2) % 3] + 1 >= gridSize[(index + 2) % 3])
                continue;
			const uint2 cellEdge = kCellEdgeIndices[edgeIndices[index]];
			const uint3 sampleIndexA = cellID + GetCornerIndex(cellEdge.x);
            const uint3 sampleIndexB = cellID + GetCornerIndex(cellEdge.y);
			const float sampleA = Sample(float3(sampleIndexA));
            const float sampleB = Sample(float3(sampleIndexB));
            if (sign(sampleA) == sign(sampleB))
				continue;

			uint3 neighborVertices;
			[unroll]
            for (uint iterator = 0; iterator < 3; iterator++)
                neighborVertices[iterator] = mesh.cellVertexIds[FlattenCellIndex(cellID + GetCornerIndex(corners[index][iterator]))];

			for (uint triangleIndex = 0; triangleIndex < 2; triangleIndex++) {
				const uint v1 = neighborVertices[sampleB > 0 ? triangleIndex     : triangleIndex + 1];
				const uint v2 = neighborVertices[sampleB > 0 ? triangleIndex + 1 : triangleIndex];
                if (v1 == kNullVertexIndex || v2 == kNullVertexIndex)
                    continue;
				if (v0 == v1 || v0 == v2 || v1 == v2)
                    continue;

                mesh.triangles.Store3(sizeof(uint3) * mesh.IncrementTriangleCounter(), uint3(v0, v1, v2));
			}
		}
    }

    void ConnectNeighbor(uint3 cellID, inout float3 vertex, uint dim, DualContourMesh neighborMesh) {
        OctreeNodeId neighborId = neighborIds[dim];
		if (neighborId.depth > 0) {
			bool dir = neighborId.index[dim] > nodeId.index[dim];
			if ((!dir && cellID[dim] == 0) || (dir && cellID[dim] == gridSize[dim] - 1)) {
				DualContourMesh neighbor = neighborMesh;

                uint dd = nodeId.depth - neighborId.depth;
                uint fac = 1 << dd;
                uint3 di = (nodeId.index>>(32-nodeId.depth)) - (neighborId.index>>(32-nodeId.depth));

                uint3 neighborCellID = cellID / fac;
                neighborCellID[dim] = dir ? 0 : (gridSize[dim] - 1);
                neighborCellID[(dim+1)%3] += (gridSize[(dim+1)%3]/fac) * di[(dim+1)%3];
                neighborCellID[(dim+2)%3] += (gridSize[(dim+2)%3]/fac) * di[(dim+2)%3];

                uint v = neighbor.cellVertexIds[FlattenCellIndex(neighborCellID)];
                if (v != kNullVertexIndex) {
                    vertex = neighbor.vertices.Load<float3>(v * sizeof(float3));
                }
			}
		}
	}

    void ConnectNeighbors(uint3 cellID) {
        uint vertexId = mesh.cellVertexIds[FlattenCellIndex(cellID)];
        uint addr = vertexId * sizeof(float3);

        float3 vertex = mesh.vertices.Load<float3>(addr);

        ConnectNeighbor(cellID, vertex, 0, neighborMesh0);
        ConnectNeighbor(cellID, vertex, 1, neighborMesh1);
        ConnectNeighbor(cellID, vertex, 2, neighborMesh2);

        mesh.connectedVertices.Store<float3>(addr, vertex);
	}
};

[[vk::binding(0,0)]] // for some reason, having push constants in the entry point arguments causes descriptors to start at set 1
ParameterBlock<DualContourMesher> mesher;

[shader("compute")]
[numthreads(4, 4, 4)]
void GenerateCellVertices(uint3 id: SV_DispatchThreadID) {
    if (any(id >= mesher.gridSize))
        return;

    mesher.GenerateVertex(id);
}
[shader("compute")]
[numthreads(4, 4, 4)]
void GenerateTriangles(uint3 id: SV_DispatchThreadID) {
    if (any(id >= mesher.gridSize))
        return;

    mesher.GenerateTriangles(id);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ConnectNeighbors(uint3 id: SV_DispatchThreadID) {
    if (any(id >= mesher.gridSize+1))
        return;

    mesher.ConnectNeighbors(id);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void CreateIndirectArgs(uint3 id: SV_DispatchThreadID, uniform uint dstGroupSize) {
    const uint numTris = mesher.mesh.GetTriangleCount();

    VkDrawIndexedIndirectCommand arg = {};
    arg.indexCount = 3 * numTris;
	arg.instanceCount = 1;
	arg.firstIndex = 0;
	arg.vertexOffset = 0;
    arg.firstInstance = 0;
    mesher.mesh.drawIndirectArgs[0] = arg;

    mesher.mesh.dispatchIndirectArgs[0] = uint3((numTris + dstGroupSize-1) / dstGroupSize, 1, 1);

    const uint numVertices = mesher.mesh.GetVertexCount();
    mesher.mesh.avgError.Store<float>(0, numVertices == 0 ? 0 : mesher.mesh.avgError.Load<float>(0) / numVertices);
}
