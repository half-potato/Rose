#include "ConcurrentBinaryTree.hpp"

#include <future>

#define CBT_IMPLEMENTATION
#include "cbt/cbt.h"

#define LEB_IMPLEMENTATION
#include "cbt/leb.h"

#include <imgui/imgui.h>
#include <ImGuizmo.h>

namespace RoseEngine {

float Wedge(const float *a, const float *b) {
	return a[0] * b[1] - a[1] * b[0];
}
bool IsInside(const float2 target, const float faceVertices[][3]) {
	float v1[2] = {faceVertices[0][0], faceVertices[1][0]};
	float v2[2] = {faceVertices[0][1], faceVertices[1][1]};
	float v3[2] = {faceVertices[0][2], faceVertices[1][2]};
	float x1[2] = {v2[0] - v1[0], v2[1] - v1[1]};
	float x2[2] = {v3[0] - v2[0], v3[1] - v2[1]};
	float x3[2] = {v1[0] - v3[0], v1[1] - v3[1]};
	float y1[2] = {target[0] - v1[0], target[1] - v1[1]};
	float y2[2] = {target[0] - v2[0], target[1] - v2[1]};
	float y3[2] = {target[0] - v3[0], target[1] - v3[1]};
	float w1 = Wedge(x1, y1);
	float w2 = Wedge(x2, y2);
	float w3 = Wedge(x3, y3);
	return (w1 >= 0.0f) && (w2 >= 0.0f) && (w3 >= 0.0f);
}

struct CallbackData { float2 target; };

void UpdateSubdivisionCpuCallback_Split(cbt_Tree *cbt, const cbt_Node node, const float2 target) {
	float faceVertices[][3] = {
		{0.0f, 0.0f, 1.0f},
		{1.0f, 0.0f, 0.0f}
	};

	leb_DecodeNodeAttributeArray(node, 2, faceVertices);

	if (IsInside(target, faceVertices)) {
		leb_SplitNode(cbt, node);
	}
}
void UpdateSubdivisionCpuCallback_Merge(cbt_Tree *cbt, const cbt_Node node, const float2 target) {
	float baseFaceVertices[][3] = {
		{0.0f, 0.0f, 1.0f},
		{1.0f, 0.0f, 0.0f}
	};
	float topFaceVertices[][3] = {
		{0.0f, 0.0f, 1.0f},
		{1.0f, 0.0f, 0.0f}
	};

	leb_DiamondParent diamondParent = leb_DecodeDiamondParent(node);

	leb_DecodeNodeAttributeArray(diamondParent.base, 2, baseFaceVertices);
	leb_DecodeNodeAttributeArray(diamondParent.top, 2, topFaceVertices);

	if (!IsInside(target, baseFaceVertices) && !IsInside(target, topFaceVertices)) {
		leb_MergeNode(cbt, node, diamondParent);
	}
}

void ConcurrentBinaryTree::Initialize(CommandContext& context) {
	trees.resize(numTrees);
	buffers.resize(numTrees);
	for (uint32_t i = 0; i < numTrees; i++) {
		trees[i] = cbt_Create(maxDepth);
		buffers[i] = context.UploadData(std::span{ cbt_GetHeap(trees[i]), (size_t)cbt_HeapByteSize(trees[i]) }, vk::BufferUsageFlagBits::eStorageBuffer);
	}
}
ConcurrentBinaryTree::~ConcurrentBinaryTree() {
	for (uint32_t i = 0; i < trees.size(); i++) {
		cbt_Release(trees[i]);
	}
	trees.clear();
}

size_t ConcurrentBinaryTree::NodeCount() {
	size_t c = 0;
	for (uint32_t i = 0; i < trees.size(); i++)
		c += cbt_NodeCount(trees[i]);
	return c;
}

void ConcurrentBinaryTree::CreatePipelines(Device& device) {
	auto cbtSrc    = FindShaderPath("cbt/cbt.cs.slang");
	auto subdivSrc = FindShaderPath("cbt/leb.cs.slang");
	dispatchArgsPipeline     = Pipeline::CreateCompute(device, ShaderModule::Create(device, cbtSrc, "WriteIndirectDispatchArgs"));
	drawArgsPipeline         = Pipeline::CreateCompute(device, ShaderModule::Create(device, cbtSrc, "WriteIndirectDrawArgs"));
	cbtReducePrepassPipeline = Pipeline::CreateCompute(device, ShaderModule::Create(device, cbtSrc, "SumReducePrepass"));
	cbtReducePipeline        = Pipeline::CreateCompute(device, ShaderModule::Create(device, cbtSrc, "SumReduce"));
	lebSplitPipeline         = Pipeline::CreateCompute(device, ShaderModule::Create(device, subdivSrc, "Split"));
	lebMergePipeline         = Pipeline::CreateCompute(device, ShaderModule::Create(device, subdivSrc, "Merge"));
}

ShaderParameter ConcurrentBinaryTree::Update(CommandContext& context, const BufferView& outDrawIndirectArgs) {
	if (maxDepth < 5) maxDepth = 5;
	if (maxDepth > 31) maxDepth = 31;
	for (uint32_t i = 0; i < buffers.size(); i++) {
		if (cbt_MaxDepth(trees[i]) != maxDepth) {
			cbt_Release(trees[i]);
			trees[i] = cbt_CreateAtDepth(maxDepth, 1);
			buffers[i] = context.UploadData(std::span{ cbt_GetHeap(trees[i]), (size_t)cbt_HeapByteSize(trees[i]) }, vk::BufferUsageFlagBits::eStorageBuffer);
			context.GetDevice().Wait();
		}
	}

	float4 rect;
	ImGuizmo::GetRect(&rect.x);
	float2 cursorScreen = std::bit_cast<float2>(ImGui::GetIO().MousePos);
	const float2 target = (cursorScreen - float2(rect.x, rect.y)) / float2(rect.z, rect.w);

	ShaderParameter cbtParams = {};
	for (uint32_t i = 0; i < buffers.size(); i++)
		cbtParams["u_CbtBuffers"][i] = buffers[i];

	if (useCpu) {
		for (uint32_t i = 0; i < trees.size(); i++) {
			int64_t nodeCount = cbt_NodeCount(trees[i]);
			std::vector<std::future<void>> jobs(nodeCount);
			for (int64_t handle = 0; handle < nodeCount; ++handle)
				jobs[handle] = std::async(
					std::launch::async,
					split ? &UpdateSubdivisionCpuCallback_Split : &UpdateSubdivisionCpuCallback_Merge,
					trees[i],
					cbt_DecodeNode(trees[i], handle),
					target);
			for (auto& t : jobs)
				t.wait();
			cbt__ComputeSumReduction(trees[i]);
			context.UploadData(std::span{ cbt_GetHeap(trees[i]), (size_t)cbt_HeapByteSize(trees[i]) }, buffers[i]);
		}
	} else {
		auto dispatchArgs = cachedIndirectArgs.pop_or_create(context.GetDevice(), [&]() {
			auto buf = Buffer::Create(context.GetDevice(), sizeof(uint4) * trees.size(), vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer);
			context.GetDevice().SetDebugName( **buf.mBuffer, "CBT Indirect Args" );
			return buf;
		});
		cachedIndirectArgs.push(dispatchArgs, context.GetDevice().NextTimelineSignal());

		{
			ShaderParameter params = cbtParams;
			params["output"] = dispatchArgs;
			params["u_CbtID"] = 0;
			params["blockDim"] = lebSplitPipeline->GetShader(vk::ShaderStageFlagBits::eCompute)->WorkgroupSize().x;
			context.Dispatch(*dispatchArgsPipeline, 1, params);

			vk::MemoryBarrier2 memoryBarrier {
				.srcStageMask  = vk::PipelineStageFlagBits2::eComputeShader,
				.srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
				.dstStageMask  = vk::PipelineStageFlagBits2::eDrawIndirect,
				.dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead };
			context->pipelineBarrier2( vk::DependencyInfo{
				.dependencyFlags = vk::DependencyFlagBits::eByRegion,
				.memoryBarrierCount = 1,
				.pMemoryBarriers = &memoryBarrier } );
		}

		{
			ShaderParameter params = cbtParams;
			params["u_CbtID"] = 0;
			params["u_TargetPosition"] = target;
			if (split)
				context.DispatchIndirect(*lebSplitPipeline, dispatchArgs, params);
			else
				context.DispatchIndirect(*lebMergePipeline, dispatchArgs, params);

			vk::MemoryBarrier2 memoryBarrier {
				.srcStageMask  = vk::PipelineStageFlagBits2::eComputeShader,
				.srcAccessMask = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
				.dstStageMask  = vk::PipelineStageFlagBits2::eComputeShader,
				.dstAccessMask = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite };
			context->pipelineBarrier2( vk::DependencyInfo{
				.dependencyFlags = vk::DependencyFlagBits::eByRegion,
				.memoryBarrierCount = 1,
				.pMemoryBarriers = &memoryBarrier } );
		}

		{
			auto descriptorSets = context.GetDescriptorSets(*cbtReducePipeline->Layout());
			context.UpdateDescriptorSets(*descriptorSets, cbtParams, *cbtReducePipeline->Layout());

			ShaderParameter params = cbtParams;

			int it = maxDepth;
			{
				int cnt = ((1 << it) >> 5);
				context->bindPipeline(vk::PipelineBindPoint::eCompute, ***cbtReducePrepassPipeline);
				context.BindDescriptors(*cbtReducePrepassPipeline->Layout(), *descriptorSets);
				params["u_PassID"] = it;
				params["u_CbtID"] = 0;
				context.PushConstants(*cbtReducePrepassPipeline->Layout(), params);
				context->dispatch(cnt, 1u, 1u);
				it -= 5;

				vk::MemoryBarrier2 memoryBarrier {
					.srcStageMask  = vk::PipelineStageFlagBits2::eComputeShader,
					.srcAccessMask = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
					.dstStageMask  = vk::PipelineStageFlagBits2::eComputeShader,
					.dstAccessMask = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite };
				context->pipelineBarrier2( vk::DependencyInfo{
					.dependencyFlags = vk::DependencyFlagBits::eByRegion,
					.memoryBarrierCount = 1,
					.pMemoryBarriers = &memoryBarrier } );
			}
			context->bindPipeline(vk::PipelineBindPoint::eCompute, ***cbtReducePipeline);
			context.BindDescriptors(*cbtReducePipeline->Layout(), *descriptorSets);
			while (--it >= 0) {
				params["u_PassID"] = it;
				params["u_CbtID"] = 0;
				context.PushConstants(*cbtReducePipeline->Layout(), params);
				context->dispatch(1u << it, 1u, 1u);

				vk::MemoryBarrier2 memoryBarrier {
					.srcStageMask  = vk::PipelineStageFlagBits2::eComputeShader,
					.srcAccessMask = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
					.dstStageMask  = vk::PipelineStageFlagBits2::eComputeShader,
					.dstAccessMask = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite };
				context->pipelineBarrier2( vk::DependencyInfo{
					.dependencyFlags = vk::DependencyFlagBits::eByRegion,
					.memoryBarrierCount = 1,
					.pMemoryBarriers = &memoryBarrier } );
			}
		}
	}

	split = !split;

	{
		ShaderParameter params = cbtParams;
		params["output"] = outDrawIndirectArgs;
		params["u_CbtID"] = 0;
		context.Dispatch(*drawArgsPipeline, 1, params);
		context.AddBarrier(outDrawIndirectArgs, Buffer::ResourceState{
			.stage = vk::PipelineStageFlagBits2::eDrawIndirect,
			.access = vk::AccessFlagBits2::eIndirectCommandRead,
			.queueFamily = context.QueueFamily() });

		vk::MemoryBarrier2 memoryBarrier {
			.srcStageMask  = vk::PipelineStageFlagBits2::eComputeShader,
			.srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
			.dstStageMask  = vk::PipelineStageFlagBits2::eDrawIndirect,
			.dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead };
		context->pipelineBarrier2( vk::DependencyInfo{
			.dependencyFlags = vk::DependencyFlagBits::eByRegion,
			.memoryBarrierCount = 1,
			.pMemoryBarriers = &memoryBarrier } );
	}

	return cbtParams;
}

}