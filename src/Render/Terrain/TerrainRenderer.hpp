#pragma once

#include <Render/ViewportWidget.hpp>
#include <Render/Procedural/ProceduralFunction.hpp>
#include <Render/DualContouring/DualContourMesher.hpp>

#include "SubdivisionTree.hpp"

#include <future>

namespace RoseEngine {

class TerrainRenderer : public IRenderer {
private:
	ref<Pipeline> drawPipeline = {};
	ref<Pipeline> drawNodePipeline = {};
	vk::Format    pipelineFormat = vk::Format::eUndefined;

	bool compiling = false;
	std::future<std::tuple<ref<Pipeline>, ref<Pipeline>, vk::Format, size_t>> compileJob;

	ProceduralFunction heightFunction = {};
	size_t nodeTreeHash = 0;
	std::string compiledHeightFunction = {};

	float3 lightDir = normalize(float3(0,1,1));
	bool   wire = false;
	bool   showBackfaces = false;

	uint3    gridSize = uint3(16,16,16);
	float    scale = 1.f;
	uint32_t dcIterations = 20;
	float    dcStepSize = 0.2f;
	float    splitFactor = 100.f;
	ref<DualContourMesher> mesher = {};

	uint32_t maxDepth = 0;
	OctreeNode octreeRoot = {};
	std::unordered_map<OctreeNode::NodeId, std::pair<DualContourMesher::ContourMesh, bool>> octreeMeshes = {};
	TransientResourceCache<DualContourMesher::ContourMesh> cachedMeshes = {};

	ShaderParameter     drawParameters = {};
	ref<DescriptorSets> descriptorSets = {};
	ref<DescriptorSets> nodeDescriptorSets = {};


	void CreatePipelines(Device& device, vk::Format format);
	bool CheckCompileStatus(CommandContext& context);

public:
	~TerrainRenderer();
	void Initialize(CommandContext& context) override;
	void PreRender(CommandContext& context, const GBuffer& gbuffer, const Transform& view, const Transform& projection) override;
	void Render(CommandContext& context) override;
	void InspectorGui(CommandContext& context) override;
	void NodeEditorWidget();
};

}