#pragma once

#include <Core/CommandContext.hpp>

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
		uint64_t cpuArgsReady = 0;
		uint3 gridSize = uint3(0);

		ContourMesh() = default;
		ContourMesh(const ContourMesh&) = default;
		ContourMesh(ContourMesh&&) = default;
		ContourMesh& operator=(const ContourMesh&) = default;
		ContourMesh& operator=(ContourMesh&&) = default;
		inline ContourMesh(Device& device, const uint3 gridSize) : gridSize(gridSize) {
			const size_t maxVertices = size_t(gridSize.x) * size_t(gridSize.y) * size_t(gridSize.z);
			const size_t maxTriangles = 6 * (size_t(gridSize.x - 1) * size_t(gridSize.y - 1) * size_t(gridSize.z - 1));
			vertices  = Buffer::Create(device, sizeof(float3)*maxVertices, vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eVertexBuffer);
			triangles = Buffer::Create(device, sizeof(uint3)*maxTriangles, vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eIndexBuffer);
			cellVertexIds = Buffer::Create(device, sizeof(uint)*maxVertices);
			counters      = Buffer::Create(device, sizeof(uint)*4);
			drawIndirectArgs = Buffer::Create(device, sizeof(VkDrawIndexedIndirectCommand), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eIndirectBuffer|vk::BufferUsageFlagBits::eTransferSrc);
			dispatchIndirectArgs = Buffer::Create(device, sizeof(uint3), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eIndirectBuffer);
			drawIndirectArgsCpu  = Buffer::Create(device, sizeof(VkDrawIndexedIndirectCommand), vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT);
			device.SetDebugName(**vertices.mBuffer,             "ContourMesh::vertices");
			device.SetDebugName(**triangles.mBuffer,            "ContourMesh::triangles");
			device.SetDebugName(**cellVertexIds.mBuffer,        "ContourMesh::cellVertexIds");
			device.SetDebugName(**counters.mBuffer,             "ContourMesh::counters");
			device.SetDebugName(**drawIndirectArgs.mBuffer,     "ContourMesh::drawIndirectArgs");
			device.SetDebugName(**dispatchIndirectArgs.mBuffer, "ContourMesh::dispatchIndirectArgs");
			device.SetDebugName(**drawIndirectArgsCpu.mBuffer,  "ContourMesh::drawIndirectArgsCpu");
		}

		inline void BindShaderParameters(ShaderParameter& params) const {
			params["cellVertexIds"]     = (BufferView)cellVertexIds;
			params["counters"]          = (BufferView)counters;
			params["vertices"]          = (BufferView)vertices;
			if (connectedVertices) params["connectedVertices"] = (BufferView)connectedVertices;
			params["triangles"]         = (BufferView)triangles;
			params["gridSize"]          = gridSize;
			params["drawIndirectArgs"]     = (BufferView)drawIndirectArgs;
			params["dispatchIndirectArgs"] = (BufferView)dispatchIndirectArgs;
		}
	};

	struct GenerateMeshArgs {
		uint32_t optimizerIterations = 20;
		float    optimizerStepSize = 0.2f;
		uint32_t indirectDispatchGroupSize = 256;
	};

	inline void GenerateMesh(CommandContext& context, ContourMesh& mesh, const float3 gridScale, const float3 gridOffset, const GenerateMeshArgs& args = {}) {
		context.PushDebugLabel("DualContourMesher::GenerateMesh");

		context.Fill(mesh.counters, 0u);

		ShaderParameter params = {};

		ShaderParameter& dc = params["mesher"];
		mesh.BindShaderParameters(dc["mesh"]);
		dc["gridScale"]  = gridScale;
		dc["gridOffset"] = gridOffset;
		dc["schmitzParticleIterations"] = args.optimizerIterations;
		dc["schmitzParticleStepSize"]   = args.optimizerStepSize;

		auto descriptorSets = context.GetDescriptorSets(*mGenerateCellVertices->Layout());
		context.UpdateDescriptorSets(*descriptorSets, params, *mGenerateCellVertices->Layout());
		context.ExecuteBarriers();

		// Generate vertices in cells.

		{
			context->bindPipeline(vk::PipelineBindPoint::eCompute, ***mGenerateCellVertices);
			context.BindDescriptors(*mGenerateCellVertices->Layout(), *descriptorSets);
			auto dim = GetDispatchDim(mGenerateCellVertices->GetShader()->WorkgroupSize(), mesh.gridSize);
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
			auto dim = GetDispatchDim(mGenerateTriangles->GetShader()->WorkgroupSize(), mesh.gridSize - 1u);
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

		context.Copy(mesh.drawIndirectArgs, mesh.drawIndirectArgsCpu);
		mesh.cpuArgsReady = context.GetDevice().NextTimelineSignal();

		context.PopDebugLabel();
	}

	inline void UpdateMesh(CommandContext& context, ContourMesh& mesh, const float3 gridScale, const float3 gridOffset, std::array<const ContourMesh*, 6> neighbors, const GenerateMeshArgs& args = {}) {
		context.PushDebugLabel("DualContourMesher::UpdateMesh");

		ShaderParameter params = {};
		ShaderParameter& dc = params["mesher"];

		uint8_t neighborMask = 0;
		for (uint8_t i = 0; i < 6; i++)
		{
			if (neighbors[i])
			{
				neighbors[i]->BindShaderParameters(dc["neighborMeshes"][i]);
				neighborMask &= 1 << i;
			}
		}

		if (neighborMask == 0)
			return;

		if (!mesh.connectedVertices)
		{
			mesh.connectedVertices = Buffer::Create(context.GetDevice(), mesh.vertices.size_bytes(), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eVertexBuffer);
			context.GetDevice().SetDebugName(**mesh.connectedVertices.mBuffer, "ContourMesh::connectedVertices");
		}
		else
			mesh.connectedVertices = {};

		mesh.BindShaderParameters(dc["mesh"]);

		dc["neighborMask"]  = (uint32_t)neighborMask;
		dc["gridScale"]  = gridScale;
		dc["gridOffset"] = gridOffset;
		dc["schmitzParticleIterations"] = args.optimizerIterations;
		dc["schmitzParticleStepSize"]   = args.optimizerStepSize;

		auto descriptorSets = context.GetDescriptorSets(*mConnectNeighbors->Layout());
		context.UpdateDescriptorSets(*descriptorSets, params, *mConnectNeighbors->Layout());
		context.ExecuteBarriers();

		// Merge vertices on LoD borders

		{
			context->bindPipeline(vk::PipelineBindPoint::eCompute, ***mConnectNeighbors);
			context.BindDescriptors(*mConnectNeighbors->Layout(), *descriptorSets);
			auto dim = GetDispatchDim(mConnectNeighbors->GetShader()->WorkgroupSize(), mesh.gridSize);
			context->dispatch(dim.x, dim.y, dim.z);
		}
	}

	inline bool IsStale() const {
		// all shaders share the same source file
		return mGenerateCellVertices->GetShader()->IsStale();
	}

private:
	ref<Pipeline> mGenerateCellVertices, mConnectNeighbors, mGenerateTriangles, mCreateIndirectArgs;
};

}
