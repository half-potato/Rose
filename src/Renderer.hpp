#pragma once

#include <Core/CommandContext.hpp>
#include <Scene/Transform.h>
#include <Scene/Mesh.hpp>

#include <ImGuizmo.h>

namespace RoseEngine {

class IRenderer {
public:
	virtual void Update(double dt) = 0;
	virtual void Render(CommandContext& context, const ImageView& renderTarget) = 0;
	virtual void InspectorGui() = 0;
};

bool TransformGizmoGui(
	Transform& transform,
	const Transform& view,
	const Transform& projection,
	ImGuizmo::OPERATION operation = ImGuizmo::OPERATION::TRANSLATE,
	bool local = false,
	std::optional<float3> snap = std::nullopt) {
	float4x4 t = transpose(transform.transform);
	float4x4 v = transpose(view.transform);
	float4x4 p = transpose(projection.transform);
	const bool changed = ImGuizmo::Manipulate(
		&v[0][0],
		&p[0][0],
		operation,
		local ? ImGuizmo::MODE::LOCAL : ImGuizmo::MODE::WORLD,
		&t[0][0],
		NULL,
		snap.has_value() ? &snap->x : NULL);
	if (changed) transform.transform = transpose(t);
	return changed;
}

bool InspectorGui(Transform& v) {
	bool changed = false;
	float4x4 tmp = transpose(v.transform);
	float matrixTranslation[3], matrixRotation[3], matrixScale[3];
	ImGuizmo::DecomposeMatrixToComponents(&tmp[0][0], matrixTranslation, matrixRotation, matrixScale);
	if (ImGui::InputFloat3("Translation", matrixTranslation)) changed = true;
	if (ImGui::InputFloat3("Rotation", matrixRotation)) changed = true;
	if (ImGui::InputFloat3("Scale", matrixScale)) changed = true;
	if (changed) {
		ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, &tmp[0][0]);
		v.transform = transpose(tmp);
	}
	return changed;
}

class Renderer : public IRenderer {
public:
	ref<Pipeline> pipeline = {};
	vk::Format    pipelineFormat = vk::Format::eUndefined;

	float3 cameraPos   = float3(0,0,1);
	float2 cameraAngle = float2(0,0);
	float  fovY  = 50.f; // in degrees
	float  nearZ = 0.01f;

	TransientResourceCache<std::pair<ImageView, ImageView>> cachedRenderTargets = {};
	uint2 cachedRenderTargetExtent = uint2(0,0);

	Mesh                   mesh = {};
	MeshLayout             meshLayout = {};
	uint32_t               selectedObject = -1;
	std::vector<Transform> objectTransforms = {};
	BufferRange<Transform> objectTransformsGpu = {};
	bool                   objectTransformsDirty = false;

	std::queue<std::pair<BufferRange<uint4>, uint64_t>> viewportPickerQueue = {};

	inline static ref<Renderer> Create(CommandContext& context) {
		ref<Renderer> renderer = make_ref<Renderer>();

		context.Begin();

		renderer->mesh = Mesh {
			.indexBuffer = context.UploadData(std::vector<uint16_t>{ 0, 1, 2, 1, 3, 2 }, vk::BufferUsageFlagBits::eIndexBuffer),
			.indexType = vk::IndexType::eUint16,
			.topology = vk::PrimitiveTopology::eTriangleList };
		renderer->mesh.vertexAttributes[MeshVertexAttributeType::ePosition].emplace_back(
			context.UploadData(std::vector<float3>{
					float3(-.25f, -.25f, 0), float3(.25f, -.25f, 0),
					float3(-.25f,  .25f, 0), float3(.25f,  .25f, 0),
				}, vk::BufferUsageFlagBits::eVertexBuffer),
			MeshVertexAttributeLayout{
				.stride = sizeof(float3),
				.format = vk::Format::eR32G32B32Sfloat,
				.offset = 0,
				.inputRate = vk::VertexInputRate::eVertex });
		renderer->mesh.vertexAttributes[MeshVertexAttributeType::eColor].emplace_back(
			context.UploadData(std::vector<float3>{
					float3(0.5f, 0.5f, 0), float3(1.0f, 0.5f, 0),
					float3(0.5f, 1.0f, 0), float3(1.0f, 1.0f, 0),
				}, vk::BufferUsageFlagBits::eVertexBuffer),
			MeshVertexAttributeLayout{
				.stride = sizeof(float3),
				.format = vk::Format::eR32G32B32Sfloat,
				.offset = 0,
				.inputRate = vk::VertexInputRate::eVertex });

		context.Submit();

		return renderer;
	}

	inline void InspectorGui() {
		if (ImGui::CollapsingHeader("Camera")) {
			ImGui::PushID("Camera");
			ImGui::DragFloat3("Position", &cameraPos.x);
			ImGui::DragFloat2("Angle", &cameraAngle.x);
			Gui::ScalarField("Vertical field of view", &fovY);
			Gui::ScalarField("Near Z", &nearZ);
			ImGui::PopID();
		}

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

	inline void CreatePipeline(Device& device, vk::Format format) {
		ref<const ShaderModule> vertexShader, fragmentShader;
		if (pipeline) {
			vertexShader   = pipeline->GetShader(vk::ShaderStageFlagBits::eVertex);
			fragmentShader = pipeline->GetShader(vk::ShaderStageFlagBits::eFragment);
		}
		if (!vertexShader || vertexShader->IsStale()) {
			vertexShader   = ShaderModule::Create(device, FindShaderPath("Test.3d.slang"), "vertexMain");
			meshLayout = mesh.GetLayout(*vertexShader);
		}
		if (!fragmentShader || fragmentShader->IsStale())
			fragmentShader = ShaderModule::Create(device, FindShaderPath("Test.3d.slang"), "fragmentMain");

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

	inline void Update(double dt) {
		if (ImGui::IsWindowHovered()) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
				cameraAngle += float2(-ImGui::GetIO().MouseDelta.y, ImGui::GetIO().MouseDelta.x) * float(M_PI) / 1920.f;
				cameraAngle.x = clamp(cameraAngle.x, -float(M_PI/2), float(M_PI/2));
			}
			quat rx = glm::angleAxis( cameraAngle.x, float3(1,0,0));
			quat ry = glm::angleAxis(-cameraAngle.y, float3(0,1,0));

			if (ImGui::IsWindowFocused()) {
				float3 move = float3(0,0,0);
				if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_W)) move += float3(0,0,-1);
				if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_S)) move += float3(0,0, 1);
				if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_D)) move += float3( 1,0,0);
				if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_A)) move += float3(-1,0,0);
				if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_Q)) move += float3(0,-1,0);
				if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_E)) move += float3(0, 1,0);
				if (move != float3(0,0,0)) {
					cameraPos += (ry * rx) * normalize(move) * float(dt);
				}
			}
		}
	}

	inline void Render(CommandContext& context, const ImageView& renderTarget) {
		if (renderTarget.GetImage()->Info().format != pipelineFormat || ImGui::IsKeyPressed(ImGuiKey_F5, false))
			CreatePipeline(context.GetDevice(), renderTarget.GetImage()->Info().format);

		const quat      rot = glm::angleAxis(-cameraAngle.y, float3(0,1,0)) * glm::angleAxis( cameraAngle.x, float3(1,0,0));
		const Transform view = inverse( Transform::Rotate(rot) * Transform::Translate(cameraPos) );
		const Transform projection = Transform::Perspective(glm::radians(fovY), (float)renderTarget.Extent().x / (float)renderTarget.Extent().y, nearZ);

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

		ShaderParameter params = {};
		params["objectTransforms"] = (BufferView)objectTransformsGpu;
		params["worldToCamera"] = view;
		params["projection"]    = projection;
		auto descriptorSets = context.GetDescriptorSets(*pipeline->Layout());
		context.UpdateDescriptorSets(*descriptorSets, params, *pipeline->Layout());

		if (cachedRenderTargetExtent != uint2(renderTarget.Extent())) {
			context.GetDevice().Wait();
			cachedRenderTargets.clear();
			cachedRenderTargetExtent = uint2(renderTarget.Extent());
		}
		auto[visibility, depth] = cachedRenderTargets.pop_or_create(context.GetDevice(), [&]() {
			auto visibility = ImageView::Create(
					Image::Create(context.GetDevice(), ImageInfo{
						.format = vk::Format::eR32G32B32A32Uint,
						.extent = renderTarget.Extent(),
						.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment,
						.queueFamilies = { context.QueueFamily() } }),
					vk::ImageSubresourceRange{
						.aspectMask = vk::ImageAspectFlagBits::eColor,
						.baseMipLevel = 0,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = 1 });
			auto depth = ImageView::Create(
					Image::Create(context.GetDevice(), ImageInfo{
						.format = vk::Format::eD32Sfloat,
						.extent = renderTarget.Extent(),
						.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eDepthStencilAttachment,
						.queueFamilies = { context.QueueFamily() } }),
					vk::ImageSubresourceRange{
						.aspectMask = vk::ImageAspectFlagBits::eDepth,
						.baseMipLevel = 0,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = 1 });
			return std::make_pair(visibility, depth);
		});

		context.AddBarrier(renderTarget, Image::ResourceState{
			.layout = vk::ImageLayout::eColorAttachmentOptimal,
			.stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			.access =  vk::AccessFlagBits2::eColorAttachmentRead|vk::AccessFlagBits2::eColorAttachmentWrite,
			.queueFamily = context.QueueFamily() });
		context.AddBarrier(visibility, Image::ResourceState{
			.layout = vk::ImageLayout::eColorAttachmentOptimal,
			.stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			.access =  vk::AccessFlagBits2::eColorAttachmentRead|vk::AccessFlagBits2::eColorAttachmentWrite,
			.queueFamily = context.QueueFamily() });
		context.AddBarrier(depth, Image::ResourceState{
			.layout = vk::ImageLayout::eDepthAttachmentOptimal,
			.stage  = vk::PipelineStageFlagBits2::eLateFragmentTests,
			.access =  vk::AccessFlagBits2::eDepthStencilAttachmentRead|vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			.queueFamily = context.QueueFamily() });
		context.ExecuteBarriers();

		vk::RenderingAttachmentInfo attachments[2] = {
			vk::RenderingAttachmentInfo {
				.imageView = *renderTarget,
				.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
				.resolveMode = vk::ResolveModeFlagBits::eNone,
				.resolveImageView = {},
				.resolveImageLayout = vk::ImageLayout::eUndefined,
				.loadOp  = vk::AttachmentLoadOp::eLoad,
				.storeOp = vk::AttachmentStoreOp::eStore,
				.clearValue = vk::ClearValue{vk::ClearColorValue{std::array<float,4>{}} } },
			vk::RenderingAttachmentInfo {
				.imageView = *visibility,
				.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
				.resolveMode = vk::ResolveModeFlagBits::eNone,
				.resolveImageView = {},
				.resolveImageLayout = vk::ImageLayout::eUndefined,
				.loadOp  = vk::AttachmentLoadOp::eClear,
				.storeOp = vk::AttachmentStoreOp::eStore,
				.clearValue = vk::ClearValue{vk::ClearColorValue{std::array<uint,4>{ ~0u, ~0u, ~0u, ~0u }} } } };
		vk::RenderingAttachmentInfo depthAttachment {
			.imageView = *depth,
			.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
			.resolveMode = vk::ResolveModeFlagBits::eNone,
			.resolveImageView = {},
			.resolveImageLayout = vk::ImageLayout::eUndefined,
			.loadOp  = vk::AttachmentLoadOp::eClear,
			.storeOp = vk::AttachmentStoreOp::eStore,
			.clearValue = vk::ClearValue{vk::ClearDepthStencilValue{1.f, 0}} };
		context->beginRendering(vk::RenderingInfo {
			.renderArea = vk::Rect2D{ vk::Offset2D{0, 0}, vk::Extent2D{ renderTarget.Extent().x, renderTarget.Extent().y } },
			.layerCount = 1,
			.viewMask = 0,
			.colorAttachmentCount = 2,
			.pColorAttachments = attachments,
			.pDepthAttachment = &depthAttachment });

		context->setViewport(0, vk::Viewport{ 0, 0, (float)renderTarget.Extent().x, (float)renderTarget.Extent().y, 0, 1 });
		context->setScissor(0, vk::Rect2D{ vk::Offset2D{0, 0}, vk::Extent2D{ renderTarget.Extent().x, renderTarget.Extent().y } } );

		if (!objectTransforms.empty()) {
			context->bindPipeline(vk::PipelineBindPoint::eGraphics, ***pipeline);
			context.BindParameters(params, *pipeline->Layout(), descriptorSets);
			context.PushConstants(params, *pipeline->Layout());
			mesh.Bind(context, meshLayout);
			context->drawIndexed(mesh.indexBuffer.size_bytes() / sizeof(uint16_t), (uint32_t)objectTransforms.size(), 0, 0, 0);
		}

		context->endRendering();
		cachedRenderTargets.push(std::make_pair(visibility, depth), context.GetDevice().NextTimelineSignal());

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowFocused() && !ImGuizmo::IsUsing()) {
			float4 rect;
			ImGuizmo::GetRect(&rect.x);
			float2 cursorScreen = std::bit_cast<float2>(ImGui::GetIO().MousePos);
			int2 cursor = int2(cursorScreen - float2(rect));
			if (cursor.x >= 0 && cursor.y >= 0 && cursor.x < int(rect.z) && cursor.y < int(rect.w)) {
				context.AddBarrier(visibility, Image::ResourceState{
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

				context->copyImageToBuffer(**visibility.GetImage(), vk::ImageLayout::eTransferSrcOptimal, **buf.mBuffer, vk::BufferImageCopy{
					.bufferOffset = 0,
					.bufferRowLength = 0,
					.bufferImageHeight = 0,
					.imageSubresource = visibility.GetSubresourceLayer(),
					.imageOffset = vk::Offset3D{ cursor.x, cursor.y, 0 },
					.imageExtent = vk::Extent3D{ 1, 1, 1 } });

				viewportPickerQueue.push({ buf, context.GetDevice().NextTimelineSignal() });
			}
		}

	}
};

}