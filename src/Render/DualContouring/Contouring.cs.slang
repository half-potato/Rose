#pragma once

// Ported from https://github.com/Tuntenfisch/Voxels

import Cell;
import ProceduralVolume;
import Render.Transform.Transform;
import Core.Indirect;

using namespace RoseEngine;

struct DualContouring {
	static const uint nullVertexIndex = -1;

	RWByteAddressBuffer      counters;
	RWStructuredBuffer<uint> cellVertexIds;

    RWByteAddressBuffer vertices;
    RWByteAddressBuffer triangles;

    ProceduralVolume volume;

    uint3  gridSize;
	uint   cellStride;
    float3 gridScale;
	uint   schmitzParticleIterations;
    float3 gridOffset;
	float  schmitzParticleStepSize;

    uint FlattenCellIndex(uint3 coordinate, uint stride = 1) {
        coordinate = ((coordinate - 1) / stride) * stride + 1;
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

    float3 gridToWorld   (const float3 gridPos) { return gridPos * gridScale + gridOffset; }
    float  Sample        (const float3 gridPos) { return volume.Sample(gridToWorld(gridPos)); }
    float3 SampleGradient(const float3 gridPos) { return volume.SampleGradient(gridToWorld(gridPos)); }

    struct SchmitzOptimizer {
        float3 vertex;
        float3 forces[numberOfCellCorners];

        __init(const DualContouring dc, uint3 cellID, out bool valid) {
            // find intersections with boundary along cell edges
            vertex = 0;
            uint numIntersections = 0;
            struct PointAndNormal {
                float3 p;
                float3 n;
            };
            PointAndNormal intersections[12];
			for (uint i = 0; i < numberOfCellEdges; i++) {
				const uint2 cellEdge = gCellEdgeIndices[i];
				const uint3 cellCornerA = gCellCornerIndices[cellEdge.x];
				const uint3 cellCornerB = gCellCornerIndices[cellEdge.y];

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
            for (uint corner = 0; corner < numberOfCellCorners; corner++) {
                forces[corner] = 0;

                [unroll(12)]
                for (uint i = 0; i < numIntersections; i++) {
                    const float dist = dot(intersections[i].n, gCellCornerIndices[corner] - intersections[i].p);
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
		uint vertexId = nullVertexIndex;

        bool valid;
		var s = SchmitzOptimizer(this, cellID, valid);

        if (valid) {
            for (uint i = 0; i < schmitzParticleIterations; i++)
                s.Step(schmitzParticleStepSize);

            vertexId = IncrementVertexCounter();
            vertices.Store<float3>(vertexId*sizeof(float3), gridToWorld(float3(cellID) + s.vertex));
		}

		cellVertexIds[FlattenCellIndex(cellID)] = vertexId;
	}

    void GenerateTriangles(uint3 cellID) {
        uint v0 = cellVertexIds[FlattenCellIndex(cellID, cellStride)];
        if (v0 == nullVertexIndex) return;

		for (uint index = 0; index < 3; index++) {
			static const uint3 edgeIndices = uint3(5, 6, 10);
			const uint2 cellEdge = gCellEdgeIndices[edgeIndices[index]];
			const float sampleA = Sample(float3(cellID + gCellCornerIndices[cellEdge.x]));
            const float sampleB = Sample(float3(cellID + gCellCornerIndices[cellEdge.y]));

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
				neighborVertices[iterator] = cellVertexIds[FlattenCellIndex(cellID + gCellCornerIndices[corners[index][iterator]], cellStride)];

			for (uint triangleIndex = 0; triangleIndex < 2; triangleIndex++) {
				const uint v1 = neighborVertices[sampleB > 0 ? triangleIndex     : triangleIndex + 1];
				const uint v2 = neighborVertices[sampleB > 0 ? triangleIndex + 1 : triangleIndex];
                if (v1 == nullVertexIndex || v2 == nullVertexIndex)
                    continue;
				if (v0 == v1 || v0 == v2 || v1 == v2)
                    continue;

                triangles.Store3(sizeof(uint3) * IncrementTriangleCounter(), uint3(v0, v1, v2));
			}
		}
    }
};

ParameterBlock<DualContouring>                   dualContouring;
RWStructuredBuffer<VkDrawIndexedIndirectCommand> drawIndirectArgs;
RWStructuredBuffer<uint3>                        dispatchIndirectArgs;

[shader("compute")]
[numthreads(4, 4, 4)]
void GenerateCellVertices(uint3 id: SV_DispatchThreadID) {
    if (any(id >= dualContouring.gridSize))
        return;

    dualContouring.GenerateVertex(id);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void GenerateTriangles(uint3 id: SV_DispatchThreadID) {
    if (any(id >= dualContouring.gridSize - 1))
        return;

    dualContouring.GenerateTriangles(id);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void CreateIndirectArgs(uint3 id: SV_DispatchThreadID, uniform uint dstGroupSize) {
    const uint numTris = dualContouring.GetTriangleCount();

    VkDrawIndexedIndirectCommand arg = {};
    arg.indexCount = 3 * numTris;
	arg.instanceCount = 1;
	arg.firstIndex = 0;
	arg.vertexOffset = 0;
    arg.firstInstance = 0;
    drawIndirectArgs[0] = arg;

    dispatchIndirectArgs[0] = uint3((numTris + dstGroupSize-1) / dstGroupSize, 1, 1);
}
