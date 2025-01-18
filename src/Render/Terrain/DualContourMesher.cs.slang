#pragma once

// Ported from https://github.com/Tuntenfisch/Voxels

import Render.Terrain.Terrain;
import Scene.Transform;
import Core.Indirect;

using namespace RoseEngine;

// Ported from https://github.com/Tuntenfisch/Voxels

//     5---------6
//    /|        /|
//   / |       / |
//  /  |      /  |
// 1---------2
// |   |     |   |
// |   4-----|---7
// |  /      |  /
// | /       | /
// |/        |/
// 0---------3
static const uint3 kCellCornerIndices[8] = {
    uint3(0, 0, 0),
    uint3(0, 1, 0),
    uint3(1, 1, 0),
    uint3(1, 0, 0),
    uint3(0, 0, 1),
    uint3(0, 1, 1),
    uint3(1, 1, 1),
    uint3(1, 0, 1)
};

//     +----6----+
//    /|        /|
//   7 |       5 |
//  /  11     / 10
// +----4----+   |
// |   |     |   |
// |   +----2|---+
// 8  /      9  /
// | 3       | 1
// |/        |/
// +----0----+
static const uint2 kCellEdgeIndices[12] = {
    uint2(0, 3),
    uint2(3, 7),
    uint2(7, 4),
    uint2(4, 0),
    uint2(1, 2),
    uint2(2, 6),
    uint2(5, 6),
    uint2(5, 1),
    uint2(0, 1),
    uint2(3, 2),
    uint2(7, 6),
    uint2(4, 5)
};

struct DualContourMesh {
	RWByteAddressBuffer      counters;
	RWStructuredBuffer<uint> cellVertexIds;

    RWByteAddressBuffer vertices;
    RWByteAddressBuffer connectedVertices;
    RWByteAddressBuffer triangles;

    RWStructuredBuffer<VkDrawIndexedIndirectCommand> drawIndirectArgs;
    RWStructuredBuffer<uint3> dispatchIndirectArgs;

    uint3 gridSize;

    uint FlattenCellIndex(uint3 coordinate) {
        return coordinate.x + gridSize.x * (coordinate.y + gridSize.y * coordinate.z);
    }

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
    DualContourMesh neighborMeshes[6];

    float3 gridScale;
	uint   schmitzParticleIterations;
    float3 gridOffset;
    float  schmitzParticleStepSize;

    float3 gridToWorld   (const float3 gridPos) { return gridPos * gridScale + gridOffset; }
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
				const uint3 cellCornerA = kCellCornerIndices[cellEdge.x];
				const uint3 cellCornerB = kCellCornerIndices[cellEdge.y];

				const float sampleA = dc.Sample(cellID + cellCornerA);
				const float sampleB = dc.Sample(cellID + cellCornerB);

				if (sign(sampleA) == sign(sampleB))
					continue;

                const float t = abs(sampleB - sampleA) > 1e-6
					? -sampleA / (sampleB - sampleA)
					: 0.5;

                const float3 p = lerp(cellCornerA, cellCornerB, t);
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
                    const float dist = dot(intersections[i].n, kCellCornerIndices[corner] - intersections[i].p);
                    const float3 voxelCornerToPlaneVector = -dist * intersections[i].n;
                    forces[corner] += voxelCornerToPlaneVector;
				}
			}

            valid = numIntersections > 0;
		}

		[mutating]
		void Step(const float step) {
			const float alpha = vertex.x;

			const float3 force03 = alpha * forces[3] + (1.0f - alpha) * forces[0];
			const float3 force47 = alpha * forces[7] + (1.0f - alpha) * forces[4];
			const float3 force12 = alpha * forces[2] + (1.0f - alpha) * forces[1];
			const float3 force56 = alpha * forces[6] + (1.0f - alpha) * forces[5];

			const float beta = vertex.z;

			const float3 force0347 = beta * force47 + (1.0f - beta) * force03;
			const float3 force1256 = beta * force56 + (1.0f - beta) * force12;

			const float gamma = vertex.y;

			const float3 force = gamma * force1256 + (1.0f - gamma) * force0347;

            vertex = saturate(vertex + force * step);
		}
	};

    void GenerateVertex(uint3 cellID) {
		uint vertexId = kNullVertexIndex;

        bool valid;
		var s = SchmitzOptimizer(this, cellID, valid);

        if (valid) {
            for (uint i = 0; i < schmitzParticleIterations; i++)
                s.Step(schmitzParticleStepSize);

            vertexId = mesh.IncrementVertexCounter();
            mesh.vertices.Store<float3>(vertexId*sizeof(float3), gridToWorld(float3(cellID) + saturate(s.vertex)));
		}

        mesh.cellVertexIds[mesh.FlattenCellIndex(cellID)] = vertexId;
	}
    void GenerateTriangles(uint3 cellID) {
        uint v0 = mesh.cellVertexIds[mesh.FlattenCellIndex(cellID)];
        if (v0 == kNullVertexIndex) return;

		for (uint index = 0; index < 3; index++) {
			static const uint3 edgeIndices = uint3(5, 6, 10);
			const uint2 cellEdge = kCellEdgeIndices[edgeIndices[index]];
			const float sampleA = Sample(float3(cellID + kCellCornerIndices[cellEdge.x]));
            const float sampleB = Sample(float3(cellID + kCellCornerIndices[cellEdge.y]));

            if (sign(sampleA) == sign(sampleB))
				continue;

			static const uint3 corners[3] = {
				uint3(1, 2, 3),
				uint3(4, 5, 1),
				uint3(3, 7, 4)
			};
			uint3 neighborVertices;
			[unroll]
            for (uint iterator = 0; iterator < 3; iterator++)
                neighborVertices[iterator] = mesh.cellVertexIds[mesh.FlattenCellIndex(cellID + kCellCornerIndices[corners[index][iterator]])];

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

    void ConnectNeighbors(uint3 cellID, uint neighborMask) {
        uint vertexId = mesh.cellVertexIds[mesh.FlattenCellIndex(cellID)];
        uint addr = vertexId * sizeof(float3);

        float3 vertex = mesh.vertices.Load<float3>(addr);

		// TODO: move vertex to connect with to neighbor meshes

        mesh.connectedVertices.Store<float3>(addr, vertex);
	}
};

[[vk::binding(0,0)]] // for some reason, having push constants in the entry point arguments causes descriptors to start at set 1
ParameterBlock<DualContourMesher> mesher;

[shader("compute")]
[numthreads(4, 4, 4)]
void GenerateCellVertices(uint3 id: SV_DispatchThreadID) {
    if (any(id >= mesher.mesh.gridSize))
        return;

    mesher.GenerateVertex(id);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ConnectNeighbors(uint3 id: SV_DispatchThreadID, uniform uint neighborMask) {
    if (any(id >= mesher.mesh.gridSize))
        return;

    mesher.ConnectNeighbors(id, neighborMask);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void GenerateTriangles(uint3 id: SV_DispatchThreadID) {
    if (any(id >= mesher.mesh.gridSize - 1))
        return;

    mesher.GenerateTriangles(id);
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
}
