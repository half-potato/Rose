#pragma once

#include <Render/ViewportWidget.hpp>
#include <Render/Procedural/ProceduralNodeTree.hpp>
#include "ConcurrentBinaryTree.hpp"

namespace RoseEngine {

class TerrainRenderer : public IRenderer {
private:
	ref<Pipeline> subdividePipeline;
	ref<Pipeline> drawPipeline;
	vk::Format    pipelineFormat = vk::Format::eUndefined;

	float targetTriangleSize = 10;
	float3 lightDir = float3(0,1,0);
	bool wire = false;
	bool split = true;

	ref<ProceduralNodeTree> nodeTree = {};
	size_t nodeTreeHash = 0;

	ref<ConcurrentBinaryTree> cbt = {};

	TransientResourceCache<std::pair<BufferView, BufferView>> cachedIndirectArgs = {};
	BufferView          drawIndirectArgs = {};
	ShaderParameter     drawParameters = {};
	ref<DescriptorSets> descriptorSets = {};

	void CreatePipelines(Device& device, vk::Format format);

public:
	void Initialize(CommandContext& context) override;
	void PreRender(CommandContext& context, const GBuffer& gbuffer, const Transform& view, const Transform& projection) override;
	void Render(CommandContext& context) override;
	void InspectorGui(CommandContext& context) override;
	void NodeGui();
};

}