#pragma once

#include <Core/CommandContext.hpp>
#include "SubdivisionTree.hpp"

namespace RoseEngine {

class DualContourMesher {
public:
	inline DualContourMesher(Device& device, const ShaderDefines& defs = {}) {
		auto srcFile = FindShaderPath("DualContourMesher.cs.slang");
		mGenerateCellVertices = Pipeline::CreateCompute(device, ShaderModule::Create(device, srcFile, "GenerateCellVertices", "sm_6_7", defs));
		mConnectNeighbors     = Pipeline::CreateCompute(device, ShaderModule::Create(device, srcFile, "ConnectNeighbors",     "sm_6_7", defs));
		mGenerateTriangles    = Pipeline::CreateCompute(device, ShaderModule::Create(device, srcFile, "GenerateTriangles",    "sm_6_7", defs));
		mCreateIndirectArgs   = Pipeline::CreateCompute(device, ShaderModule::Create(device, srcFile, "CreateIndirectArgs",   "sm_6_7", defs));
	}

	struct ContourMesh {
		BufferRange<float3> vertices = {};
		BufferRange<float3> connectedVertices = {};
		BufferRange<uint3>  triangles = {};
		BufferRange<uint> cellVertexIds = {};
		BufferRange<uint> counters = {};
		BufferRange<VkDrawIndexedIndirectCommand> drawIndirectArgs = {};
		// 1 thread per triangle
		BufferRange<uint3> dispatchIndirectArgs = {};
		BufferRange<VkDrawIndexedIndirectCommand> drawIndirectArgsCpu = {};
		BufferRange<float> avgError = {};
		BufferRange<float> avgErrorCpu = {};
		uint64_t cpuArgsReady = 0;

		ContourMesh() = default;
		ContourMesh(const ContourMesh&) = default;
		ContourMesh(ContourMesh&&) = default;
		ContourMesh& operator=(const ContourMesh&) = default;
		ContourMesh& operator=(ContourMesh&&) = default;
		inline ContourMesh(Device& device, const uint3 gridSize) {
			const size_t maxVertices = size_t(gridSize.x+1) * size_t(gridSize.y+1) * size_t(gridSize.z+1);
			const size_t maxTriangles = 6 * maxVertices;
			vertices  = Buffer::Create(device, sizeof(float3)*maxVertices, vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eVertexBuffer);
			triangles = Buffer::Create(device, sizeof(uint3)*maxTriangles, vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eIndexBuffer);
			cellVertexIds = Buffer::Create(device, sizeof(uint)*maxVertices);
			counters      = Buffer::Create(device, sizeof(uint)*4);
			dispatchIndirectArgs = Buffer::Create(device, sizeof(uint3), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eIndirectBuffer);
			drawIndirectArgs     = Buffer::Create(device, sizeof(VkDrawIndexedIndirectCommand), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eIndirectBuffer|vk::BufferUsageFlagBits::eTransferSrc);
			drawIndirectArgsCpu  = Buffer::Create(device, sizeof(VkDrawIndexedIndirectCommand), vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT);
			avgError    = Buffer::Create(device, sizeof(float), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst);
			avgErrorCpu = Buffer::Create(device, sizeof(float), vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT);
			device.SetDebugName(**vertices.mBuffer,             "ContourMesh::vertices");
			device.SetDebugName(**triangles.mBuffer,            "ContourMesh::triangles");
			device.SetDebugName(**cellVertexIds.mBuffer,        "ContourMesh::cellVertexIds");
			device.SetDebugName(**counters.mBuffer,             "ContourMesh::counters");
			device.SetDebugName(**drawIndirectArgs.mBuffer,     "ContourMesh::drawIndirectArgs");
			device.SetDebugName(**dispatchIndirectArgs.mBuffer, "ContourMesh::dispatchIndirectArgs");
			device.SetDebugName(**drawIndirectArgsCpu.mBuffer,  "ContourMesh::drawIndirectArgsCpu");
			device.SetDebugName(**avgError.mBuffer,             "ContourMesh::avgError");
			device.SetDebugName(**avgErrorCpu.mBuffer,          "ContourMesh::avgErrorCpu");
		}

		inline void BindShaderParameters(ShaderParameter& params) const {
			params["cellVertexIds"]     = (BufferView)cellVertexIds;
			params["counters"]          = (BufferView)counters;
			params["vertices"]          = (BufferView)vertices;
			if (connectedVertices) params["connectedVertices"] = (BufferView)connectedVertices;
			params["triangles"]         = (BufferView)triangles;
			params["drawIndirectArgs"]     = (BufferView)drawIndirectArgs;
			params["dispatchIndirectArgs"] = (BufferView)dispatchIndirectArgs;
			params["avgError"] = (BufferView)avgError;
		}
	};

	struct GenerateMeshArgs {
		uint32_t optimizerIterations = 20;
		float    optimizerStepSize = 0.2f;
		uint32_t indirectDispatchGroupSize = 256;
	};

	inline void GenerateMesh(CommandContext& context, ContourMesh& mesh, const uint3 gridSize, const float3 gridWorldMin, const float3 gridWorldMax, const GenerateMeshArgs& args = {}) {
		context.PushDebugLabel("DualContourMesher::GenerateMesh");

		context.Fill(mesh.counters, 0u);
		context.Fill(mesh.avgError, 0.f);

		ShaderParameter params = {};

		ShaderParameter& dc = params["mesher"];
		mesh.BindShaderParameters(dc["mesh"]);
		dc["gridWorldMin"]  = gridWorldMin;
		dc["gridWorldMax"] = gridWorldMax;
		dc["gridSize"] = gridSize;
		dc["schmitzParticleIterations"] = args.optimizerIterations;
		dc["schmitzParticleStepSize"]   = args.optimizerStepSize;

		#if 0

		context.Dispatch(*mGenerateCellVertices, gridSize, params);
		context.Dispatch(*mGenerateTriangles, gridSize, params);

		#else

		auto descriptorSets = context.GetDescriptorSets(*mGenerateCellVertices->Layout());
		context.UpdateDescriptorSets(*descriptorSets, params, *mGenerateCellVertices->Layout());
		context.ExecuteBarriers();

		// Generate vertices in cells.

		{
			context->bindPipeline(vk::PipelineBindPoint::eCompute, ***mGenerateCellVertices);
			context.BindDescriptors(*mGenerateCellVertices->Layout(), *descriptorSets);
			auto dim = GetDispatchDim(mGenerateCellVertices->GetShader()->WorkgroupSize(), gridSize);
			context->dispatch(dim.x, dim.y, dim.z);
		}

		context.AddBarrier(mesh.cellVertexIds, Buffer::ResourceState{
			.stage  = vk::PipelineStageFlagBits2::eComputeShader,
			.access = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
			.queueFamily = context.QueueFamily()
		});
		context.ExecuteBarriers();


		// Connect vertices in cells to form triangles.

		{
			context->bindPipeline(vk::PipelineBindPoint::eCompute, ***mGenerateTriangles);
			context.BindDescriptors(*mGenerateTriangles->Layout(), *descriptorSets);
			auto dim = GetDispatchDim(mGenerateTriangles->GetShader()->WorkgroupSize(), gridSize);
			context->dispatch(dim.x, dim.y, dim.z);
		}

		context.AddBarrier(mesh.counters, Buffer::ResourceState{
			.stage  = vk::PipelineStageFlagBits2::eComputeShader,
			.access = vk::AccessFlagBits2::eShaderRead,
			.queueFamily = context.QueueFamily()
		});
		context.ExecuteBarriers();

		// copy triangle count to indirect argument buffers
		{
			context->bindPipeline(vk::PipelineBindPoint::eCompute, ***mCreateIndirectArgs);
			context.BindDescriptors(*mCreateIndirectArgs->Layout(), *descriptorSets);
			context->pushConstants<uint32_t>(***mCreateIndirectArgs->Layout(), vk::ShaderStageFlagBits::eCompute, 0, args.indirectDispatchGroupSize);
			context->dispatch(1, 1, 1);
		}

		mesh.vertices.SetState(Buffer::ResourceState{
			.stage  = vk::PipelineStageFlagBits2::eComputeShader,
			.access = vk::AccessFlagBits2::eShaderWrite,
			.queueFamily = context.QueueFamily()
		});
		mesh.triangles.SetState(Buffer::ResourceState{
			.stage  = vk::PipelineStageFlagBits2::eComputeShader,
			.access = vk::AccessFlagBits2::eShaderWrite,
			.queueFamily = context.QueueFamily()
		});
		mesh.drawIndirectArgs.SetState(Buffer::ResourceState{
			.stage  = vk::PipelineStageFlagBits2::eComputeShader,
			.access = vk::AccessFlagBits2::eShaderWrite,
			.queueFamily = context.QueueFamily()
		});
		mesh.dispatchIndirectArgs.SetState(Buffer::ResourceState{
			.stage  = vk::PipelineStageFlagBits2::eComputeShader,
			.access = vk::AccessFlagBits2::eShaderWrite,
			.queueFamily = context.QueueFamily()
		});
		#endif

		context.Copy(mesh.drawIndirectArgs, mesh.drawIndirectArgsCpu);
		context.Copy(mesh.avgError, mesh.avgErrorCpu);
		mesh.cpuArgsReady = context.GetDevice().NextTimelineSignal();

		context.PopDebugLabel();
	}

	inline void Stitch(CommandContext& context, ContourMesh& mesh, const uint3 gridSize, const float3 gridWorldMin, const float3 gridWorldMax, const std::array<const ContourMesh*,3>& neighbors, const std::array<OctreeNodeId,3>& neighborIds, const OctreeNodeId& nodeId, const GenerateMeshArgs& args = {}) {
		context.PushDebugLabel("DualContourMesher::Stitch");

		if (!mesh.connectedVertices) {
			mesh.connectedVertices = Buffer::Create(context.GetDevice(), mesh.vertices.size_bytes(), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eVertexBuffer);
			context.GetDevice().SetDebugName(**mesh.connectedVertices.mBuffer, "ContourMesh::connectedVertices");
		}

		ShaderParameter params = {};
		ShaderParameter& dc = params["mesher"];

		mesh.BindShaderParameters(dc["mesh"]);
		if (neighbors[0]) neighbors[0]->BindShaderParameters(dc["neighborMesh0"]);
		if (neighbors[1]) neighbors[1]->BindShaderParameters(dc["neighborMesh1"]);
		if (neighbors[2]) neighbors[2]->BindShaderParameters(dc["neighborMesh2"]);

		dc["gridWorldMin"]  = gridWorldMin;
		dc["gridWorldMax"] = gridWorldMax;
		dc["schmitzParticleIterations"] = args.optimizerIterations;
		dc["schmitzParticleStepSize"]   = args.optimizerStepSize;
		dc["gridSize"] = gridSize;
		dc["neighborIds"] = neighborIds;
		dc["nodeId"] = nodeId;

		#if 1

		context.Dispatch(*mConnectNeighbors, gridSize, params);

		#else

		auto descriptorSets = context.GetDescriptorSets(*mConnectNeighbors->Layout());
		context.UpdateDescriptorSets(*descriptorSets, params, *mConnectNeighbors->Layout());
		context.ExecuteBarriers();

		// Merge vertices on LoD borders

		{
			context->bindPipeline(vk::PipelineBindPoint::eCompute, ***mConnectNeighbors);
			context.BindDescriptors(*mConnectNeighbors->Layout(), *descriptorSets);
			auto dim = GetDispatchDim(mConnectNeighbors->GetShader()->WorkgroupSize(), gridSize);
			context->dispatch(dim.x, dim.y, dim.z);
		}
		#endif
	}

	inline bool IsStale() const {
		// all shaders share the same source file
		return mGenerateCellVertices->GetShader()->IsStale();
	}

private:
	ref<Pipeline> mGenerateCellVertices, mConnectNeighbors, mGenerateTriangles, mCreateIndirectArgs;
};

}
