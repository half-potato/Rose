#pragma once

#include <Core/CommandContext.hpp>

namespace RoseEngine {

class DualContourMesher {
public:
	inline DualContourMesher(Device& device, const std::string& densityFn) {
		auto srcFile = FindShaderPath("Contouring.cs.slang");
		ShaderDefines defs {
			{ "PROCEDURAL_NODE_SRC", densityFn }
		};
		mGenerateCellVertices = Pipeline::CreateCompute(device, ShaderModule::Create(device, srcFile, "GenerateCellVertices", "sm_6_7", defs, {}, false));
		mGenerateTriangles    = Pipeline::CreateCompute(device, ShaderModule::Create(device, srcFile, "GenerateTriangles"   , "sm_6_7", defs, {}, false));
		mCreateIndirectArgs   = Pipeline::CreateCompute(device, ShaderModule::Create(device, srcFile, "CreateIndirectArgs"  , "sm_6_7", defs, {}, false));
	}

	struct ContourMesh {
		BufferRange<float3> vertices = {};
		BufferRange<uint3>  triangles = {};
		BufferRange<VkDrawIndexedIndirectCommand> drawIndirectArgs = {};
		// 1 thread per triangle
		BufferRange<uint3> dispatchIndirectArgs = {};

		ContourMesh() = default;
		ContourMesh(const ContourMesh&) = default;
		ContourMesh(ContourMesh&&) = default;
		ContourMesh& operator=(const ContourMesh&) = default;
		ContourMesh& operator=(ContourMesh&&) = default;
		inline ContourMesh(Device& device, const uint3 gridSize) {
			const size_t maxVertices = size_t(gridSize.x) * size_t(gridSize.y) * size_t(gridSize.z);
			const size_t maxTriangles = 6 * (size_t(gridSize.x - 1) * size_t(gridSize.y - 1) * size_t(gridSize.z - 1));
			vertices  = Buffer::Create(device, sizeof(float3)*maxVertices, vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eVertexBuffer);
			triangles = Buffer::Create(device, sizeof(uint3)*maxTriangles, vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eIndexBuffer);
			drawIndirectArgs = Buffer::Create(device, sizeof(VkDrawIndexedIndirectCommand), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eIndirectBuffer);
			dispatchIndirectArgs = Buffer::Create(device, sizeof(uint3), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eIndirectBuffer);
			device.SetDebugName(**vertices.mBuffer,             "ContourMesh::vertices");
			device.SetDebugName(**triangles.mBuffer,            "ContourMesh::triangles");
			device.SetDebugName(**drawIndirectArgs.mBuffer,     "ContourMesh::drawIndirectArgs");
			device.SetDebugName(**dispatchIndirectArgs.mBuffer, "ContourMesh::dispatchIndirectArgs");
		}
	};

	struct GenerateMeshArgs {
		uint32_t optimizerIterations = 20;
		float    optimizerStepSize = 0.2f;
		uint32_t indirectDispatchGroupSize = 256;
	};

	inline void GenerateMesh(CommandContext& context, const ContourMesh& mesh, const uint3 gridSize, const float3 gridScale, const float3 gridOffset, const GenerateMeshArgs& args = {}) {
		context.PushDebugLabel("DualContourMesher::GenerateMesh");

		BuildData meshData = cached.pop_or_create(context.GetDevice(), [&]() { return BuildData(context.GetDevice(), mesh.vertices.size()); });
		cached.push(meshData, context.GetDevice().NextTimelineSignal());

		context.Fill(meshData.counters, 0u);

		ShaderParameter params = {};

		params["drawIndirectArgs"]     = (BufferView)mesh.drawIndirectArgs;
		params["dispatchIndirectArgs"] = (BufferView)mesh.dispatchIndirectArgs;

		ShaderParameter& dc = params["dualContouring"];
		dc["cellVertexIds"] = (BufferView)meshData.cellVertexIds;
		dc["counters"]      = (BufferView)meshData.counters;
		dc["vertices"]      = (BufferView)mesh.vertices;
		dc["triangles"]     = (BufferView)mesh.triangles;
		dc["gridSize"]   = gridSize;
		dc["gridScale"]  = gridScale;
		dc["gridOffset"] = gridOffset;
		dc["cellStride"] = 1;
		dc["schmitzParticleIterations"] = args.optimizerIterations;
		dc["schmitzParticleStepSize"]   = args.optimizerStepSize;

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

		context.AddBarrier(meshData.cellVertexIds, Buffer::ResourceState{
			.stage  = vk::PipelineStageFlagBits2::eComputeShader,
			.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
			.queueFamily = context.QueueFamily()
		});
		context.ExecuteBarriers();

		// Connect vertices in cells to form triangles.

		{
			context->bindPipeline(vk::PipelineBindPoint::eCompute, ***mGenerateTriangles);
			context.BindDescriptors(*mGenerateTriangles->Layout(), *descriptorSets);
			auto dim = GetDispatchDim(mGenerateTriangles->GetShader()->WorkgroupSize(), gridSize - 1u);
			context->dispatch(dim.x, dim.y, dim.z);
		}

		context.AddBarrier(meshData.counters, Buffer::ResourceState{
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

		context.PopDebugLabel();
	}

	inline bool IsStale() const {
		// all shaders share the same source file
		return mGenerateCellVertices->GetShader()->IsStale();
	}

private:
	ref<Pipeline> mGenerateCellVertices, mGenerateTriangles, mCreateIndirectArgs;

	struct BuildData {
		BufferRange<uint> cellVertexIds = {};
		BufferRange<uint> counters = {};
		BuildData() = default;
		BuildData(const BuildData&) = default;
		BuildData(BuildData&&) = default;
		BuildData& operator=(const BuildData&) = default;
		BuildData& operator=(BuildData&&) = default;
		inline BuildData(Device& device, size_t maxVertices) {
			cellVertexIds = Buffer::Create(device, sizeof(uint)*maxVertices);
			counters      = Buffer::Create(device, sizeof(uint)*4);
			device.SetDebugName(**cellVertexIds.mBuffer, "ContourMesh::BuildData::cellVertexIds");
			device.SetDebugName(**counters.mBuffer,      "ContourMesh::BuildData::counters");
		}
	};
	TransientResourceCache<BuildData> cached = {};

};

}
