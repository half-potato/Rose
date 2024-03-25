#pragma once

#include <Core/CommandContext.hpp>
#include <Core/TransientResourceCache.hpp>

struct cbt_Tree;

namespace RoseEngine {

class ConcurrentBinaryTree {
private:
	std::vector<cbt_Tree*>  trees = {};
	std::vector<BufferView> buffers = {};

	bool split = true;

	TransientResourceCache<BufferView> cachedIndirectArgs = {};

	ref<Pipeline> cbtReducePrepassPipeline = {};
	ref<Pipeline> cbtReducePipeline = {};
	ref<Pipeline> dispatchArgsPipeline = {};
	ref<Pipeline> drawArgsPipeline = {};
	ref<Pipeline> lebSplitPipeline = {};
	ref<Pipeline> lebMergePipeline = {};

public:
	uint32_t maxDepth = 6;
	uint32_t numTrees = 1;
	bool     useCpu = false;

	inline const BufferView& GetBuffer(uint32_t i) { return buffers[i]; }

	~ConcurrentBinaryTree();

	size_t NodeCount();

	void Initialize(CommandContext& context);
	void CreatePipelines(Device& device);
	ShaderParameter Update(CommandContext& context, const BufferView& outDrawIndirectArgs);
};

}