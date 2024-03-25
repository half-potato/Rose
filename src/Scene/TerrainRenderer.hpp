#pragma once

#include "ViewportWidget.hpp"
#include "ConcurrentBinaryTree.hpp"

namespace RoseEngine {

class TerrainRenderer : public IRenderer {
private:
	ref<Pipeline> drawPipeline;
	vk::Format    pipelineFormat = vk::Format::eUndefined;

	Transform transform = Transform::Identity();
	bool split = true;

	ref<ConcurrentBinaryTree> cbt = {};

	TransientResourceCache<BufferView> cachedDrawArgs = {};
	BufferView          drawIndirectArgs = {};
	ShaderParameter     drawParameters = {};
	ref<DescriptorSets> descriptorSets = {};

	void CreatePipelines(Device& device, vk::Format format);

public:
	void Initialize(CommandContext& context) override;
	void PreRender(CommandContext& context, const GBuffer& gbuffer, const Transform& view, const Transform& projection) override;
	void Render(CommandContext& context) override;
	void InspectorGui() override;
};

}