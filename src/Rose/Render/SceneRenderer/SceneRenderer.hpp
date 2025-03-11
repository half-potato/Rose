#pragma once

#include <stack>

#include <Rose/Render/ViewportWidget.hpp>
#include <Rose/Scene/Scene.hpp>

namespace RoseEngine {

ref<SceneNode> LoadGLTF(CommandContext& context, const std::filesystem::path& filename);

struct SceneRendererArgs {
	std::vector<std::pair<std::string, ImageView>> attachments;

	Transform cameraToWorld;
	Transform worldToCamera;
	Transform projection;

	uint2  renderExtent;
	bool   viewportFocused;
	float4 viewportRect;

	inline const ImageView& GetAttachment(const std::string& name) const {
		return std::ranges::find(attachments, name, &std::pair<std::string, ImageView>::first)->second;
	}
};

class SceneRenderer {
private:
	TupleMap<ref<Pipeline>, MeshLayout, MaterialFlags, bool> cachedPipelines = {};
	ref<vk::raii::Sampler> cachedSampler = nullptr;
	ref<const ShaderModule> vertexShader, vertexShaderTextured, fragmentShader, fragmentShaderTextured, fragmentShaderTexturedAlphaCutoff;
	ref<Pipeline> pathTracer = nullptr;

	ref<DescriptorSets> descriptorSets = {};

	ref<Scene> scene = nullptr;

	inline const auto& GetPipeline(Device& device, const SceneRendererArgs& renderData, const Mesh& mesh, const Material<ImageView>& material) {
		if (!vertexShader || (ImGui::IsKeyPressed(ImGuiKey_F5, false) && vertexShader->IsStale())) {
			if (vertexShader) device.Wait();
			vertexShader                      = ShaderModule::Create(device, FindShaderPath("Visibility.3d.slang"), "vertexMain");
			vertexShaderTextured              = ShaderModule::Create(device, FindShaderPath("Visibility.3d.slang"), "vertexMain", "sm_6_7", ShaderDefines{ { "HAS_TEXCOORD", "1" } });
			fragmentShader                    = ShaderModule::Create(device, FindShaderPath("Visibility.3d.slang"), "fragmentMain");
			fragmentShaderTextured            = ShaderModule::Create(device, FindShaderPath("Visibility.3d.slang"), "fragmentMain", "sm_6_7", ShaderDefines{ { "HAS_TEXCOORD", "1" } });
			fragmentShaderTexturedAlphaCutoff = ShaderModule::Create(device, FindShaderPath("Visibility.3d.slang"), "fragmentMain", "sm_6_7", ShaderDefines{ { "HAS_TEXCOORD", "1" }, { "USE_ALPHA_CUTOFF", "1" } });
		}

		bool textured = mesh.vertexAttributes.contains(MeshVertexAttributeType::eTexcoord) && mesh.vertexAttributes.at(MeshVertexAttributeType::eTexcoord).size() > 0;
		auto vs = textured ? vertexShaderTextured : vertexShader;
		auto fs = textured ? (material.HasFlag(MaterialFlags::eAlphaCutoff) ? fragmentShaderTexturedAlphaCutoff : fragmentShaderTextured) : fragmentShader;
		const bool alphaBlend = material.HasFlag(MaterialFlags::eAlphaBlend);

		auto key = std::tuple{ mesh.GetLayout(*vs), (MaterialFlags)material.GetFlags(), textured };

		if (auto it = cachedPipelines.find(key); it != cachedPipelines.end()) {
			const auto& pipeline = it->second;
			if (pipeline->GetShader(vk::ShaderStageFlagBits::eVertex) != vs || pipeline->GetShader(vk::ShaderStageFlagBits::eFragment) != fs)
				cachedPipelines.erase(it);
			else
				return *it;
		}

		if (!cachedSampler) {
			cachedSampler = make_ref<vk::raii::Sampler>(*device, vk::SamplerCreateInfo{
				.magFilter  = vk::Filter::eLinear,
				.minFilter  = vk::Filter::eLinear,
				.mipmapMode = vk::SamplerMipmapMode::eLinear,
				.minLod = 0,
				.maxLod = 12 });
		}

		// get vertex buffer bindings from the mesh layout

		DynamicRenderingState renderState;
		for (const auto&[name, attachment] : renderData.attachments) {
			const vk::Format format = attachment.GetImage()->Info().format;
			if (IsDepthStencil(format))
				renderState.depthFormat = format;
			else
				renderState.colorFormats.emplace_back(format);
		}

		GraphicsPipelineInfo pipelineInfo {
			.vertexInputState = VertexInputDescription{
				.bindings   = std::get<0>(key).bindings,
				.attributes = std::get<0>(key).attributes },
			.inputAssemblyState = vk::PipelineInputAssemblyStateCreateInfo{
				.topology = std::get<0>(key).topology },
			.rasterizationState = vk::PipelineRasterizationStateCreateInfo{
				.depthClampEnable = false,
				.rasterizerDiscardEnable = false,
				.polygonMode = vk::PolygonMode::eFill,
				.cullMode = material.HasFlag(MaterialFlags::eDoubleSided) ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack,
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
				.attachments = std::vector<vk::PipelineColorBlendAttachmentState>(2, vk::PipelineColorBlendAttachmentState {
						.blendEnable         = alphaBlend,
						.srcColorBlendFactor = alphaBlend ? vk::BlendFactor::eOneMinusDstAlpha : vk::BlendFactor::eZero,
						.dstColorBlendFactor = alphaBlend ? vk::BlendFactor::eDstAlpha         : vk::BlendFactor::eOne,
						.colorBlendOp        = alphaBlend ? vk::BlendOp::eAdd                  : vk::BlendOp::eAdd,
						.srcAlphaBlendFactor = alphaBlend ? vk::BlendFactor::eOne              : vk::BlendFactor::eZero,
						.dstAlphaBlendFactor = alphaBlend ? vk::BlendFactor::eZero             : vk::BlendFactor::eOne,
						.alphaBlendOp        = alphaBlend ? vk::BlendOp::eAdd                  : vk::BlendOp::eAdd,
						.colorWriteMask      = vk::ColorComponentFlags{vk::FlagTraits<vk::ColorComponentFlagBits>::allFlags} }) },
			.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor },
			.dynamicRenderingState = renderState
		};
		PipelineLayoutInfo layoutInfo {
			.descriptorBindingFlags = {
				{ "scene.meshBuffers", vk::DescriptorBindingFlagBits::ePartiallyBound },
				{ "scene.images",      vk::DescriptorBindingFlagBits::ePartiallyBound } },
			.immutableSamplers      = { { "scene.sampler", { cachedSampler } } } };
		auto pipeline = Pipeline::CreateGraphics(device, { vs, fs }, pipelineInfo, layoutInfo);
		return *cachedPipelines.emplace(key, pipeline).first;
	}

public:
	inline void SetScene(const ref<Scene>& s) { scene = s; }

	inline void PreRender(CommandContext& context, SceneRendererArgs& renderData) {
		if (!scene || !scene->sceneRoot) return;

		scene->PreRender(context, [&](Device& device, const Mesh& mesh, const Material<ImageView>& material) {
			return GetPipeline(device, renderData, mesh, material);
		});

		ShaderParameter params = {};
		params["scene"]         = scene->renderData.sceneParameters;
		params["worldToCamera"] = renderData.worldToCamera;
		params["projection"]    = renderData.projection;

		// all pipelines should have the same descriptor set layouts
		descriptorSets = context.GetDescriptorSets(*cachedPipelines.begin()->second->Layout());
		context.UpdateDescriptorSets(*descriptorSets, params, *cachedPipelines.begin()->second->Layout());
	}

	inline void Render(CommandContext& context, const SceneRendererArgs& renderData) {
		if (!scene || !scene->sceneRoot) return;

		const Pipeline* p = nullptr;
		for (const auto& drawList : scene->renderData.drawLists) {
			for (const auto&[pipeline, mesh, meshLayout, draws] : drawList) {
				if (p != pipeline) {
					context->bindPipeline(vk::PipelineBindPoint::eGraphics, ***pipeline);
					context.BindDescriptors(*pipeline->Layout(), *descriptorSets);
					p = pipeline;
				}

				mesh->Bind(context, meshLayout);

				const uint32_t indexCount = mesh->indexBuffer.size_bytes() / mesh->indexSize;
				for (const auto&[firstInstance, instanceCount] : draws) {
					context->drawIndexed(indexCount, instanceCount, 0, 0, firstInstance);
				}
			}
		}
	}

	inline void PostRender(CommandContext& context, const SceneRendererArgs& renderData) {
		if (!scene || !scene->sceneRoot || scene->renderData.drawLists.empty()) return;
		if (!pathTracer || (ImGui::IsKeyPressed(ImGuiKey_F5, false) && pathTracer->GetShader()->IsStale())) {
			if (pathTracer) context.GetDevice().Wait();
			pathTracer = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), FindShaderPath("PathTracer.cs.slang")), {},
				PipelineLayoutInfo{
					.descriptorBindingFlags = {
						{ "scene.meshBuffers", vk::DescriptorBindingFlagBits::ePartiallyBound },
						{ "scene.images",      vk::DescriptorBindingFlagBits::ePartiallyBound } },
					.immutableSamplers = { { "scene.sampler", { cachedSampler } } } });
		}

		ImageView renderTarget = renderData.GetAttachment("renderTarget");

		ShaderParameter params = {};
		params["scene"] = scene->renderData.sceneParameters;
		params["renderTarget"] = ImageParameter{ .image = renderTarget                          , .imageLayout = vk::ImageLayout::eGeneral };
		params["visibility"]   = ImageParameter{ .image = renderData.GetAttachment("visibility"), .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
		params["worldToCamera"] = renderData.worldToCamera;
		params["cameraToWorld"] = renderData.cameraToWorld;
		params["projection"]    = renderData.projection;
		params["inverseProjection"] = inverse(renderData.projection);
		params["imageSize"] = uint2(renderTarget.Extent());
		params["seed"] = (uint32_t)context.GetDevice().NextTimelineSignal();
		context.Dispatch(*pathTracer, renderTarget.Extent(), params);
	}
};

}