#pragma once

#include <queue>
#include <stack>
#include <portable-file-dialogs.h>

#include "SceneNode.hpp"
#include <Render/ViewportWidget.hpp>

namespace RoseEngine {

ref<SceneNode> LoadGLTF(CommandContext& context, const std::filesystem::path& filename);

class SceneRenderer : public IRenderer {
private:
	std::unordered_map<MeshLayout, std::pair<ref<Pipeline>, vk::Format>> cachedPipelines = {};
	ref<vk::raii::Sampler> cachedSampler;
	ref<const ShaderModule> vertexShader, fragmentShader;

	struct DrawBatch {
		const Pipeline* pipeline = nullptr;
		const Mesh*     mesh = nullptr;
		MeshLayout      meshLayout = {};
		std::vector<std::pair<uint32_t/*firstInstance*/, uint32_t/*instanceCount*/>> draws = {};
	};
	std::vector<DrawBatch>           drawList = {};
	std::vector<weak_ref<SceneNode>> instanceNodes = {};
	ref<DescriptorSets>              descriptorSets = {};

	ref<SceneNode> scene = nullptr;

	inline const auto& GetPipeline(Device& device, vk::Format format, const Mesh& mesh) {
		if (!vertexShader || (ImGui::IsKeyPressed(ImGuiKey_F5, false) && vertexShader->IsStale()))
			vertexShader = ShaderModule::Create(device, FindShaderPath("Scene.3d.slang"), "vertexMain");
		if (!fragmentShader || (ImGui::IsKeyPressed(ImGuiKey_F5, false) && fragmentShader->IsStale()))
			fragmentShader = ShaderModule::Create(device, FindShaderPath("Scene.3d.slang"), "fragmentMain");

		MeshLayout meshLayout = mesh.GetLayout(*vertexShader);

		auto it = cachedPipelines.find(meshLayout);
		if (it != cachedPipelines.end()) {
			const auto& [pipeline, format] = it->second;
			if (pipeline->GetShader(vk::ShaderStageFlagBits::eVertex) != vertexShader || pipeline->GetShader(vk::ShaderStageFlagBits::eFragment) != fragmentShader || format != format)
				cachedPipelines.erase(it);
			else
				return *it;
		}

		if (!cachedSampler)
			cachedSampler = make_ref<vk::raii::Sampler>(*device, vk::SamplerCreateInfo{
				.magFilter  = vk::Filter::eLinear,
				.minFilter  = vk::Filter::eLinear,
				.mipmapMode = vk::SamplerMipmapMode::eLinear,
				.minLod = 0,
				.maxLod = 12 });

		// get vertex buffer bindings from the mesh layout

		GraphicsPipelineInfo pipelineInfo {
			.vertexInputState = VertexInputDescription{
				.bindings   = meshLayout.bindings,
				.attributes = meshLayout.attributes },
			.inputAssemblyState = vk::PipelineInputAssemblyStateCreateInfo{
				.topology = meshLayout.topology },
			.rasterizationState = vk::PipelineRasterizationStateCreateInfo{
				.depthClampEnable = false,
				.rasterizerDiscardEnable = false,
				.polygonMode = vk::PolygonMode::eFill,
				.cullMode = vk::CullModeFlagBits::eBack,
				.frontFace = vk::FrontFace::eCounterClockwise,
				.depthBiasEnable = false },
			.multisampleState = vk::PipelineMultisampleStateCreateInfo{},
			.depthStencilState = vk::PipelineDepthStencilStateCreateInfo{
				.depthTestEnable = true,
				.depthWriteEnable = true,
				.depthCompareOp = vk::CompareOp::eLess,
				.depthBoundsTestEnable = false,
				.stencilTestEnable = false },
			.viewports = { vk::Viewport{} },
			.scissors = { vk::Rect2D{} },
			.colorBlendState = ColorBlendState{
				.attachments = std::vector<vk::PipelineColorBlendAttachmentState>(2, vk::PipelineColorBlendAttachmentState{
					.blendEnable         = false,
					.srcColorBlendFactor = vk::BlendFactor::eZero,
					.dstColorBlendFactor = vk::BlendFactor::eOne,
					.colorBlendOp        = vk::BlendOp::eAdd,
					.srcAlphaBlendFactor = vk::BlendFactor::eZero,
					.dstAlphaBlendFactor = vk::BlendFactor::eOne,
					.alphaBlendOp        = vk::BlendOp::eAdd,
					.colorWriteMask      = vk::ColorComponentFlags{vk::FlagTraits<vk::ColorComponentFlagBits>::allFlags} }) },
			.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor },
			.dynamicRenderingState = DynamicRenderingState{
				.colorFormats = { format, vk::Format::eR32G32B32A32Uint },
				.depthFormat = { vk::Format::eD32Sfloat } } };
		PipelineLayoutInfo layoutInfo {
			.descriptorBindingFlags = { { "images", vk::DescriptorBindingFlagBits::ePartiallyBound } },
			.immutableSamplers      = { { "sampler", { cachedSampler } } } };
		auto pipeline = Pipeline::CreateGraphics(device, vertexShader, fragmentShader, pipelineInfo, layoutInfo);
		return *cachedPipelines.emplace(std::move(meshLayout), std::pair{pipeline, format}).first;
	}

public:
	inline void SetScene(const ref<SceneNode>& scene_) { scene = scene_; }
	inline const ref<SceneNode>& GetScene() const { return scene; }

	inline const auto& GetInstanceNodes() const { return instanceNodes; }

	inline void PreRender(CommandContext& context, const GBuffer& gbuffer, const Transform& view, const Transform& projection) override {
		drawList.clear();
		instanceNodes.clear();

		if (!scene) return;

		// collect renderables and their transforms from the scene graph

		std::unordered_map<const Pipeline*,
			std::pair<
				MeshLayout,
				std::unordered_map<const Mesh*,
					std::unordered_map<const Material<ImageView>*,
						std::vector<
							std::pair<SceneNode*, Transform> >>>>>
			renderables;

		std::stack<std::pair<SceneNode*, Transform>> todo;
		todo.push({scene.get(), Transform::Identity()});
		while (!todo.empty()) {
			auto [n, t] = todo.top();
			todo.pop();

			if (n->mesh && n->material) {
				const auto& [meshLayout, pipeline_] = GetPipeline(context.GetDevice(), gbuffer.renderTarget.GetImage()->Info().format, *n->mesh);
				auto&[meshLayout_, meshes] = renderables[pipeline_.first.get()];
				meshLayout_ = meshLayout;
				meshes[n->mesh.get()][n->material.get()].emplace_back(std::pair{n, t});
			}

			for (const ref<SceneNode>& c : *n)
				todo.push({c.get(), c->transform.has_value() ? t * c->transform.value() : t});
		}

		if (renderables.empty())
			return;

		// create instances and draw calls from renderables
		struct InstanceHeader {
			uint transformIndex;
			uint materialIndex;
		};
		std::vector<Transform>          transforms;
		std::vector<Material<uint32_t>> materials;
		std::vector<InstanceHeader>     instanceHeaders;
		std::unordered_map<ImageView, uint32_t> imageMap;
		std::unordered_map<const Material<ImageView>*, size_t> materialMap;
		for (const auto&[pipeline, meshes__] : renderables) {
			const auto& [meshLayout, meshes_] = meshes__;
			for (const auto&[mesh, materials_] : meshes_) {
				for (const auto&[material, nt_] : materials_) {
					size_t materialId = materials.size();
					if (auto it = materialMap.find(material); it != materialMap.end())
						materialId = it->second;
					else
						materials.emplace_back(PackMaterial(*material, imageMap));

					auto& draws = drawList.emplace_back(DrawBatch{
						.pipeline   = pipeline,
						.mesh       = mesh,
						.meshLayout = meshLayout,
						.draws = {}
					}).draws;

					size_t start = instanceHeaders.size();
					for (const auto&[n,t] : nt_) {
						instanceHeaders.emplace_back(InstanceHeader{
							.transformIndex = (uint32_t)transforms.size(),
							.materialIndex  = (uint32_t)materialId,
						});
						transforms.emplace_back(t);
						instanceNodes.emplace_back(n->shared_from_this());
					}
					draws.emplace_back(std::pair{(uint32_t)start, (uint32_t)(instanceHeaders.size() - start)});
				}
			}
		}

		ShaderParameter params = {};
		params["transforms"] = (BufferView)context.UploadData(transforms     , vk::BufferUsageFlagBits::eStorageBuffer);
		params["materials"]  = (BufferView)context.UploadData(materials      , vk::BufferUsageFlagBits::eStorageBuffer);
		params["instances"]  = (BufferView)context.UploadData(instanceHeaders, vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eVertexBuffer);
		params["worldToCamera"] = view;
		params["projection"]    = projection;
		for (const auto& [img, idx] : imageMap)
			params["images"][idx] = ImageParameter{
				.image = img,
				.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				.sampler = {} };

		// all pipelines should have the same descriptor set layouts
		descriptorSets = context.GetDescriptorSets(*cachedPipelines.begin()->second.first->Layout());
		context.UpdateDescriptorSets(*descriptorSets, params, *cachedPipelines.begin()->second.first->Layout());
	}

	inline void Render(CommandContext& context) override {
		const Pipeline* p = nullptr;
		for (const auto&[pipeline, mesh, meshLayout, draws] : drawList) {
			if (p != pipeline) {
				context->bindPipeline(vk::PipelineBindPoint::eGraphics, ***pipeline);
				context.BindDescriptors(*pipeline->Layout(), *descriptorSets);
				p = pipeline;
			}
			mesh->Bind(context, meshLayout);
			for (const auto&[firstInstance, instanceCount] : draws)
				context->drawIndexed(mesh->indexBuffer.size_bytes() / sizeof(uint16_t), instanceCount, 0, 0, firstInstance);
		}
	}};

}