#pragma once

#include <queue>

#include <Render/ViewportWidget.hpp>
#include "Mesh.hpp"

namespace RoseEngine {

class ObjectRenderer : public IRenderer {
private:
	ref<Pipeline> pipeline = {};
	vk::Format    pipelineFormat = vk::Format::eUndefined;

	Mesh                   mesh = {};
	MeshLayout             meshLayout = {};

	uint32_t               selectedObject = UINT_MAX;
	std::vector<Transform> objectTransforms = {};
	BufferRange<Transform> objectTransformsGpu = {};
	bool                   objectTransformsDirty = false;

	ShaderParameter        params = {};
	ref<DescriptorSets>    descriptorSets = {};

	std::queue<std::pair<BufferRange<uint4>, uint64_t>> viewportPickerQueue = {};

	inline void CreatePipeline(Device& device, vk::Format format) {
		device.Wait();

		ref<const ShaderModule> vertexShader, fragmentShader;
		if (pipeline) {
			vertexShader   = pipeline->GetShader(vk::ShaderStageFlagBits::eVertex);
			fragmentShader = pipeline->GetShader(vk::ShaderStageFlagBits::eFragment);
		}
		if (!vertexShader || vertexShader->IsStale()) {
			vertexShader   = ShaderModule::Create(device, FindShaderPath("Mesh.3d.slang"), "vertexMain");
			meshLayout = mesh.GetLayout(*vertexShader);
		}
		if (!fragmentShader || fragmentShader->IsStale())
			fragmentShader = ShaderModule::Create(device, FindShaderPath("Mesh.3d.slang"), "fragmentMain");

		// get vertex buffer bindings from the mesh layout

		GraphicsPipelineInfo pipelineInfo {
			.vertexInputState = VertexInputDescription{
				.bindings   = meshLayout.bindings,
				.attributes = meshLayout.attributes },
			.inputAssemblyState = vk::PipelineInputAssemblyStateCreateInfo{
				.topology = vk::PrimitiveTopology::eTriangleList },
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
		pipeline = Pipeline::CreateGraphics(device, vertexShader, fragmentShader, pipelineInfo);

		pipelineFormat = format;
	}

public:
	inline void Initialize(CommandContext& context) override {
		mesh = Mesh {
			.indexBuffer = context.UploadData(std::vector<uint16_t>{ 0, 1, 2, 1, 3, 2 }, vk::BufferUsageFlagBits::eIndexBuffer),
			.indexType = vk::IndexType::eUint16,
			.topology = vk::PrimitiveTopology::eTriangleList };
		mesh.vertexAttributes[MeshVertexAttributeType::ePosition].emplace_back(
			context.UploadData(std::vector<float3>{
					float3(-.25f, -.25f, 0), float3(.25f, -.25f, 0),
					float3(-.25f,  .25f, 0), float3(.25f,  .25f, 0),
				}, vk::BufferUsageFlagBits::eVertexBuffer),
			MeshVertexAttributeLayout{
				.stride = sizeof(float3),
				.format = vk::Format::eR32G32B32Sfloat,
				.offset = 0,
				.inputRate = vk::VertexInputRate::eVertex });
		mesh.vertexAttributes[MeshVertexAttributeType::eColor].emplace_back(
			context.UploadData(std::vector<float3>{
					float3(0.5f, 0.5f, 0), float3(1.0f, 0.5f, 0),
					float3(0.5f, 1.0f, 0), float3(1.0f, 1.0f, 0),
				}, vk::BufferUsageFlagBits::eVertexBuffer),
			MeshVertexAttributeLayout{
				.stride = sizeof(float3),
				.format = vk::Format::eR32G32B32Sfloat,
				.offset = 0,
				.inputRate = vk::VertexInputRate::eVertex });
	}

	inline void InspectorGui(CommandContext& context) override {
		static Transform tmp = Transform::Identity();

		if (ImGui::Button("Add instance")) {
			ImGui::OpenPopup("Add instance");
			tmp = Transform::Identity();
		}

		// add child dialog
		if (ImGui::BeginPopup("Add instance")) {
			RoseEngine::InspectorGui(tmp);
			if (ImGui::Button("Done")) {
				objectTransforms.emplace_back(tmp);
				objectTransformsDirty = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		for (auto it = objectTransforms.begin(); it != objectTransforms.end();) {
			ImGui::PushID(&*it);
			if (ImGui::CollapsingHeader("Mesh")) {
				if (ImGui::Button("Delete")) {
					it = objectTransforms.erase(it);
					ImGui::PopID();
					continue;
				}
				objectTransformsDirty |= RoseEngine::InspectorGui(*it);
			}
			ImGui::PopID();
			it++;
		}
	}

	inline void PreRender(CommandContext& context, const GBuffer& gbuffer, const Transform& view, const Transform& projection) override {
		if (!pipeline
			|| gbuffer.renderTarget.GetImage()->Info().format != pipelineFormat
			|| ImGui::IsKeyPressed(ImGuiKey_F5, false))
			CreatePipeline(context.GetDevice(), gbuffer.renderTarget.GetImage()->Info().format);

		if (!viewportPickerQueue.empty()) {
			auto[buf, value] = viewportPickerQueue.front();
			if (context.GetDevice().CurrentTimelineValue() >= value) {
				selectedObject = buf[0].x;
				viewportPickerQueue.pop();
			}
		}
		if (selectedObject < objectTransforms.size())
			objectTransformsDirty |= TransformGizmoGui(objectTransforms[selectedObject], view, projection);

		if (objectTransformsDirty && !objectTransforms.empty()) {
			if (!objectTransformsGpu || objectTransformsGpu.size() < objectTransforms.size())
				objectTransformsGpu = context.UploadData(objectTransforms, vk::BufferUsageFlagBits::eStorageBuffer);
			else
				context.UploadData(objectTransforms, objectTransformsGpu);
			objectTransformsDirty = false;
		}

		params["objectTransforms"] = (BufferView)objectTransformsGpu;
		params["worldToCamera"] = view;
		params["projection"]    = projection;
		descriptorSets = context.GetDescriptorSets(*pipeline->Layout());
		context.UpdateDescriptorSets(*descriptorSets, params, *pipeline->Layout());
	}

	inline void Render(CommandContext& context) override {
		if (!objectTransforms.empty()) {
			context->bindPipeline(vk::PipelineBindPoint::eGraphics, ***pipeline);
			context.BindDescriptors(*pipeline->Layout(), *descriptorSets);
			context.PushConstants(*pipeline->Layout(), params);
			mesh.Bind(context, meshLayout);
			context->drawIndexed(mesh.indexBuffer.size_bytes() / sizeof(uint16_t), (uint32_t)objectTransforms.size(), 0, 0, 0);
		}
	}

	inline void PostRender(CommandContext& context, const GBuffer& gbuffer) override {
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowFocused() && !ImGuizmo::IsUsing()) {
			float4 rect;
			ImGuizmo::GetRect(&rect.x);
			float2 cursorScreen = std::bit_cast<float2>(ImGui::GetIO().MousePos);
			int2 cursor = int2(cursorScreen - float2(rect));
			if (cursor.x >= 0 && cursor.y >= 0 && cursor.x < int(rect.z) && cursor.y < int(rect.w)) {
				context.AddBarrier(gbuffer.visibility, Image::ResourceState{
					.layout = vk::ImageLayout::eTransferSrcOptimal,
					.stage  = vk::PipelineStageFlagBits2::eTransfer,
					.access = vk::AccessFlagBits2::eTransferRead,
					.queueFamily = context.QueueFamily() });
				context.ExecuteBarriers();

				BufferRange<uint4> buf = Buffer::Create(
					context.GetDevice(),
					sizeof(uint4),
					vk::BufferUsageFlagBits::eTransferDst,
					vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
					VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

				context->copyImageToBuffer(**gbuffer.visibility.GetImage(), vk::ImageLayout::eTransferSrcOptimal, **buf.mBuffer, vk::BufferImageCopy{
					.bufferOffset = 0,
					.bufferRowLength = 0,
					.bufferImageHeight = 0,
					.imageSubresource = gbuffer.visibility.GetSubresourceLayer(),
					.imageOffset = vk::Offset3D{ cursor.x, cursor.y, 0 },
					.imageExtent = vk::Extent3D{ 1, 1, 1 } });

				viewportPickerQueue.push({ buf, context.GetDevice().NextTimelineSignal() });
			}
		}
	}
};

}