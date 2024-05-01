#include "ConcurrentBinaryTree.hpp"

#define CBT_IMPLEMENTATION
#include "cbt.h"

#define LEB_IMPLEMENTATION
#include "leb.h"

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

ref<ConcurrentBinaryTree> ConcurrentBinaryTree::Create(CommandContext& context, uint32_t depth, uint32_t arraySize, bool square) {
	auto cbt = make_ref<ConcurrentBinaryTree>();
	cbt->squareMode = square;
	cbt->maxDepth = depth;
	cbt->numTrees = arraySize;
	cbt->trees.resize(cbt->numTrees);
	cbt->buffers.resize(cbt->numTrees);
	for (uint32_t i = 0; i < cbt->numTrees; i++) {
		cbt->trees[i] = cbt_Create(cbt->maxDepth);
		cbt->buffers[i] = context.UploadData(std::span{ cbt_GetHeap(cbt->trees[i]), (size_t)cbt_HeapByteSize(cbt->trees[i]) }, vk::BufferUsageFlagBits::eStorageBuffer);
		context.GetDevice().SetDebugName(**cbt->buffers[i].mBuffer, "CBT Buffer " + std::to_string(i));
	}

	ShaderDefines defs { { "CBT_HEAP_BUFFER_COUNT", std::to_string(arraySize) } };

	auto cbtSrc    = FindShaderPath("cbt/cbt.cs.slang");
	cbt->cbtReducePrepassPipeline = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), cbtSrc, "SumReducePrepass", "sm_6_7", defs));
	cbt->cbtReducePipeline        = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), cbtSrc, "SumReduce", "sm_6_7", defs));
	cbt->dispatchArgsPipeline     = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), cbtSrc, "WriteIndirectDispatchArgs", "sm_6_7", defs));
	cbt->drawArgsPipeline         = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), cbtSrc, "WriteIndirectDrawArgs", "sm_6_7", defs));
	return cbt;
}
ConcurrentBinaryTree::~ConcurrentBinaryTree() {
	for (uint32_t i = 0; i < trees.size(); i++)
		cbt_Release(trees[i]);
	trees.clear();
}

void ConcurrentBinaryTree::Build(CommandContext& context) {
	ShaderParameter params = GetShaderParameter();

	auto descriptorSets = context.GetDescriptorSets(*cbtReducePipeline->Layout());
	context.UpdateDescriptorSets(*descriptorSets, params, *cbtReducePipeline->Layout());

	int it = maxDepth;
	{
		for (uint32_t i = 0; i < trees.size(); i++)
			context.AddBarrier(buffers[i], Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eComputeShader,
				.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
				.queueFamily = context.QueueFamily() });
		context.ExecuteBarriers();

		context->bindPipeline(vk::PipelineBindPoint::eCompute, ***cbtReducePrepassPipeline);
		context.BindDescriptors(*cbtReducePrepassPipeline->Layout(), *descriptorSets);
		context.ExecuteBarriers();
		params["u_PassID"] = it;
		for (uint32_t i = 0; i < trees.size(); i++) {
			params["u_CbtID"] = i;
			context.PushConstants(*cbtReducePrepassPipeline->Layout(), params);
			context->dispatch((((1 << it) >> 5) + 255) / 256, 1u, 1u);
		}
		it -= 5;
	}
	context->bindPipeline(vk::PipelineBindPoint::eCompute, ***cbtReducePipeline);
	context.BindDescriptors(*cbtReducePipeline->Layout(), *descriptorSets);
	while (--it >= 0) {
		for (uint32_t i = 0; i < trees.size(); i++)
			context.AddBarrier(buffers[i], Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eComputeShader,
				.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
				.queueFamily = context.QueueFamily() });
		context.ExecuteBarriers();

		params["u_PassID"] = it;
		for (uint32_t i = 0; i < trees.size(); i++) {
			params["u_CbtID"] = i;
			context.PushConstants(*cbtReducePipeline->Layout(), params);
			context->dispatch(((1u << it) + 255) / 256, 1u, 1u);
		}
	}
}

}