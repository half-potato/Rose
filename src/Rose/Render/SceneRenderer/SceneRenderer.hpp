#pragma once

#include <stack>
#include <Rose/Render/ViewportWidget.hpp>
#include <Rose/Scene/SceneNode.hpp>
#include <Rose/Scene/AccelerationStructure.hpp>

namespace RoseEngine {

ref<SceneNode> LoadGLTF(CommandContext& context, const std::filesystem::path& filename);

class SceneRenderer {
private:
	struct CachedPipeline {
		ref<Pipeline> pipeline = {};
		vk::Format    renderTargetFormat = {};
	};
	TupleMap<CachedPipeline, MeshLayout, MaterialFlags, bool> cachedPipelines = {};
	TransientResourceCache<AccelerationStructure> cachedAccelerationStructures = {};
	ref<vk::raii::Sampler> cachedSampler = nullptr;
	ref<const ShaderModule> vertexShader, vertexShaderTextured, fragmentShader, fragmentShaderTextured, fragmentShaderTexturedAlphaCutoff;
	ref<Pipeline> pathTracer = nullptr;

	struct DrawBatch {
		const Pipeline* pipeline = nullptr;
		const Mesh*     mesh = nullptr;
		MeshLayout      meshLayout = {};
		std::vector<std::pair<uint32_t/*firstInstance*/, uint32_t/*instanceCount*/>> draws = {};
	};
	std::vector<std::vector<DrawBatch>> drawLists = {};
	AccelerationStructure               accelerationStructure = {};
	std::vector<weak_ref<SceneNode>>    instanceNodes = {};
	ref<DescriptorSets>                 descriptorSets = {};
	ShaderParameter                     sceneParameters = {};
	bool                                updateScene = false;

	ref<SceneNode> scene = nullptr;
	ImageView backgroundImage = {};
	float3    backgroundColor = float3(0);

	inline const auto& GetPipeline(Device& device, vk::Format format, const Mesh& mesh, const Material<ImageView>& material) {
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
			const auto& [pipeline, format_] = it->second;
			if (pipeline->GetShader(vk::ShaderStageFlagBits::eVertex) != vs || pipeline->GetShader(vk::ShaderStageFlagBits::eFragment) != fs || format_ != format)
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
			.dynamicRenderingState = DynamicRenderingState{
				.colorFormats = { format, vk::Format::eR32G32B32A32Uint },
				.depthFormat = { vk::Format::eD32Sfloat } } };
		PipelineLayoutInfo layoutInfo {
			.descriptorBindingFlags = {
				{ "scene.meshBuffers", vk::DescriptorBindingFlagBits::ePartiallyBound },
				{ "scene.images",      vk::DescriptorBindingFlagBits::ePartiallyBound } },
			.immutableSamplers      = { { "scene.sampler", { cachedSampler } } } };
		auto pipeline = Pipeline::CreateGraphics(device, { vs, fs }, pipelineInfo, layoutInfo);
		return *cachedPipelines.emplace(key, CachedPipeline{ .pipeline = pipeline, .renderTargetFormat = format }).first;
	}

public:
	inline void Initialize(CommandContext& context) {}
	inline void InspectorWidget(CommandContext& context) {}

	inline const ref<SceneNode>& GetSceneRoot() const { return scene; }
	inline const ImageView& GetBackgroundImage() const { return backgroundImage; }
	inline const float3& GetBackgroundColor() const { return backgroundColor; }

	inline const auto& GetInstanceNodes() const { return instanceNodes; }
	inline const auto& GetAccelerationStructure() const { return accelerationStructure.accelerationStructure; }

	inline const ShaderParameter& GetSceneParameters() const { return sceneParameters; }

	inline void SetDirty() { updateScene = true; }

	inline void SetScene(const ref<SceneNode>& s) { scene = s; SetDirty(); }
	inline void SetBackgroundImage(const ImageView& v) { backgroundImage = v; SetDirty(); }
	inline void SetBackgroundColor(const float3& v) { backgroundColor = v; SetDirty(); }

	inline void PreRender(CommandContext& context, const RenderData& renderData) {
		if (!scene) return;

		if (updateScene) {
			drawLists.clear();
			instanceNodes.clear();

			// collect renderables and their transforms from the scene graph

			std::unordered_map<const Pipeline*,
				std::pair<
					MeshLayout,
					std::unordered_map<Mesh*,
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
					const auto& [key, cachedPipeline] = GetPipeline(context.GetDevice(), renderData.gbuffer.renderTarget.GetImage()->Info().format, *n->mesh, *n->material);
					auto&[meshLayout_, meshes] = renderables[cachedPipeline.pipeline.get()];
					meshLayout_ = std::get<0>(key);
					meshes[n->mesh.get()][n->material.get()].emplace_back(std::pair{n, t});
				}

				for (const ref<SceneNode>& c : *n)
					todo.push({c.get(), c->transform.has_value() ? t * c->transform.value() : t});
			}

			// create instances and draw calls from renderables
			drawLists.resize(3);

			std::vector<vk::AccelerationStructureInstanceKHR> instances;
			std::vector<InstanceHeader>     instanceHeaders;
			std::vector<Transform>          transforms;

			std::vector<Material<uint32_t>> materials;
			std::unordered_map<const Material<ImageView>*, size_t> materialMap;
			std::unordered_map<ImageView, uint32_t> imageMap;

			std::vector<MeshHeader> meshes;
			std::unordered_map<const Mesh*, size_t> meshMap;
			std::unordered_map<ref<Buffer>, uint32_t> meshBufferMap;

			for (const auto&[pipeline, meshes__] : renderables) {
				const auto& [meshLayout, meshes_] = meshes__;
				for (const auto&[mesh, materials_] : meshes_) {
					if (!mesh->blas.accelerationStructure || mesh->lastUpdateTime > mesh->blasUpdateTime) {
						mesh->blas = AccelerationStructure::Create(context, *mesh);
						mesh->blasUpdateTime = context.GetDevice().NextTimelineSignal();
					}

					size_t meshId = meshes.size();
					if (auto it = meshMap.find(mesh); it != meshMap.end())
						meshId = it->second;
					else {
						meshMap.emplace(mesh, meshId);
						meshes.emplace_back(PackMesh(*mesh, meshBufferMap));
					}

					for (const auto&[material, nt_] : materials_) {
						size_t materialId = materials.size();
						if (auto it = materialMap.find(material); it != materialMap.end())
							materialId = it->second;
						else {
							materialMap.emplace(material, materialId);
							materials.emplace_back(PackMaterial(*material, imageMap));
						}

						auto& drawList = drawLists[material->HasFlag(MaterialFlags::eAlphaBlend) ? 2 : material->HasFlag(MaterialFlags::eAlphaCutoff) ? 1 : 0];
						auto& draws = drawList.emplace_back(DrawBatch{
							.pipeline   = pipeline,
							.mesh       = mesh,
							.meshLayout = meshLayout,
							.draws = {}
						}).draws;

						size_t start = instanceHeaders.size();
						for (const auto&[n,t] : nt_) {
							size_t instanceId = instanceHeaders.size();
							instanceHeaders.emplace_back(InstanceHeader{
								.transformIndex = (uint32_t)transforms.size(),
								.materialIndex  = (uint32_t)materialId,
								.meshIndex = (uint32_t)meshId,
								.triangleCount = uint32_t(mesh->indexBuffer.size_bytes()/mesh->indexSize)/3 });
							transforms.emplace_back(t);
							instanceNodes.emplace_back(n->shared_from_this());

							vk::GeometryInstanceFlagsKHR flags = material->HasFlag(MaterialFlags::eDoubleSided) ? vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable : vk::GeometryInstanceFlagBitsKHR{};

							instances.emplace_back(vk::AccelerationStructureInstanceKHR{
								.transform = std::bit_cast<vk::TransformMatrixKHR>((float3x4)transpose(t.transform)),
								.instanceCustomIndex = (uint32_t)instanceId,
								.mask = 1,
								.flags = (VkGeometryInstanceFlagsKHR)flags,
								.accelerationStructureReference = context.GetDevice()->getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR{
									.accelerationStructure = **mesh->blas.accelerationStructure })
							});
						}
						draws.emplace_back(std::pair{(uint32_t)start, (uint32_t)(instanceHeaders.size() - start)});
					}
				}
			}

			if (cachedAccelerationStructures.can_pop(context.GetDevice())) cachedAccelerationStructures.pop();

			accelerationStructure = AccelerationStructure::Create(context, instances);

			cachedAccelerationStructures.push(accelerationStructure, context.GetDevice().NextTimelineSignal());

			sceneParameters["backgroundColor"] = backgroundColor;
			uint32_t backgroundImageIndex = -1;
			if (backgroundImage) {
				backgroundImageIndex = (uint32_t)imageMap.size();
				imageMap.emplace(backgroundImage, backgroundImageIndex);
			}
			sceneParameters["backgroundImage"] = backgroundImageIndex;

			sceneParameters["instanceCount"]   = (uint32_t)instanceHeaders.size();
			sceneParameters["meshBufferCount"] = (uint32_t)meshBufferMap.size();
			sceneParameters["materialCount"]   = (uint32_t)materials.size();
			sceneParameters["imageCount"]      = (uint32_t)imageMap.size();

			std::vector<Transform> invTransforms(transforms.size());
			std::ranges::transform(transforms, invTransforms.begin(), [](const Transform& t) { return inverse(t); });

			sceneParameters["instances"]         = (BufferView)context.UploadData(instanceHeaders, vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eVertexBuffer);
			sceneParameters["transforms"]        = (BufferView)context.UploadData(transforms,      vk::BufferUsageFlagBits::eStorageBuffer);
			sceneParameters["inverseTransforms"] = (BufferView)context.UploadData(invTransforms,   vk::BufferUsageFlagBits::eStorageBuffer);
			sceneParameters["materials"]         = (BufferView)context.UploadData(materials,       vk::BufferUsageFlagBits::eStorageBuffer);
			sceneParameters["meshes"]            = (BufferView)context.UploadData(meshes,          vk::BufferUsageFlagBits::eStorageBuffer);
			sceneParameters["accelerationStructure"] = accelerationStructure.accelerationStructure;
			for (const auto& [buf, idx] : meshBufferMap) sceneParameters["meshBuffers"][idx] = BufferView{buf, 0, buf->Size()};
			for (const auto& [img, idx] : imageMap)      sceneParameters["images"][idx] = ImageParameter{ .image = img, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

			updateScene = false;
		}

		ShaderParameter params = {};
		params["scene"]         = sceneParameters;
		params["worldToCamera"] = renderData.worldToCamera;
		params["projection"]    = renderData.projection;

		// all pipelines should have the same descriptor set layouts
		descriptorSets = context.GetDescriptorSets(*cachedPipelines.begin()->second.pipeline->Layout());
		context.UpdateDescriptorSets(*descriptorSets, params, *cachedPipelines.begin()->second.pipeline->Layout());
	}

	inline void Render(CommandContext& context, const RenderData& renderData) {
		const Pipeline* p = nullptr;
		for (const auto& drawList : drawLists) {
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

	inline void PostRender(CommandContext& context, const RenderData& renderData) {
		if (!scene || drawLists.empty()) return;
		if (!pathTracer || (ImGui::IsKeyPressed(ImGuiKey_F5, false) && pathTracer->GetShader()->IsStale())) {
			if (pathTracer) context.GetDevice().Wait();
			pathTracer = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), FindShaderPath("PathTracer.cs.slang")), {},
				PipelineLayoutInfo{
					.descriptorBindingFlags = {
						{ "scene.meshBuffers", vk::DescriptorBindingFlagBits::ePartiallyBound },
						{ "scene.images",      vk::DescriptorBindingFlagBits::ePartiallyBound } },
					.immutableSamplers      = { { "scene.sampler", { cachedSampler } } } });
		}
		ShaderParameter params = {};
		params["scene"] = sceneParameters;
		params["renderTarget"] = ImageParameter{ .image = renderData.gbuffer.renderTarget, .imageLayout = vk::ImageLayout::eGeneral };
		params["visibility"]   = ImageParameter{ .image = renderData.gbuffer.visibility  , .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
		params["worldToCamera"] = renderData.worldToCamera;
		params["cameraToWorld"] = renderData.cameraToWorld;
		params["projection"]    = renderData.projection;
		params["inverseProjection"] = inverse(renderData.projection);
		params["imageSize"] = uint2(renderData.gbuffer.renderTarget.Extent());
		params["seed"] = (uint32_t)context.GetDevice().NextTimelineSignal();
		context.Dispatch(*pathTracer, renderData.gbuffer.renderTarget.Extent(), params);
	}
};

}