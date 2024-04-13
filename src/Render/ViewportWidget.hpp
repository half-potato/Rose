#pragma once

#include <Core/CommandContext.hpp>
#include <Core/TransientResourceCache.hpp>
#include <Core/Gui.hpp>
#include "Transform.hpp"

namespace RoseEngine {

struct GBuffer {
	ImageView renderTarget;
	ImageView visibility;
	ImageView depth;
};

struct EditorCamera {
	float3 cameraPos   = float3(0, 2, 2);
	float2 cameraAngle = float2(-float(M_PI)/4,0);
	float  fovY  = 50.f; // in degrees
	float  nearZ = 0.01f;

	float moveSpeed = 1.f;

	inline void InspectorGui() {
		ImGui::PushID("Camera");
		ImGui::DragFloat3("Position", &cameraPos.x);
		ImGui::DragFloat2("Angle", &cameraAngle.x);
		Gui::ScalarField("Vertical field of view", &fovY);
		Gui::ScalarField("Near Z", &nearZ);
		ImGui::PopID();
	}

	inline void Update(double dt) {
		if (ImGui::IsWindowHovered()) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
				cameraAngle += float2(-ImGui::GetIO().MouseDelta.y, ImGui::GetIO().MouseDelta.x) * float(M_PI) / 1920.f;
				cameraAngle.x = clamp(cameraAngle.x, -float(M_PI/2), float(M_PI/2));
			}
		}
		quat rx = glm::angleAxis( cameraAngle.x, float3(1,0,0));
		quat ry = glm::angleAxis(-cameraAngle.y, float3(0,1,0));

		if (ImGui::IsWindowFocused()) {
			if (ImGui::GetIO().MouseWheel != 0) {
				moveSpeed *= (1 + ImGui::GetIO().MouseWheel / 8);
				moveSpeed = std::max(moveSpeed, .05f);
			}

			float3 move = float3(0,0,0);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_W)) move += float3( 0, 0,-1);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_S)) move += float3( 0, 0, 1);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_D)) move += float3( 1, 0, 0);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_A)) move += float3(-1, 0, 0);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_Q)) move += float3( 0,-1, 0);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_E)) move += float3( 0, 1, 0);
			if (move != float3(0,0,0)) {
				move = (ry * rx) * normalize(move);
				if (ImGui::IsKeyDown(ImGuiKey_LeftShift))
					move *= 3.f;
				cameraPos += move * moveSpeed * float(dt);
			}
		}
	}

	inline std::pair<Transform,Transform> GetViewProjection(float aspect) const {
		const quat      rot = glm::angleAxis(-cameraAngle.y, float3(0,1,0)) * glm::angleAxis( cameraAngle.x, float3(1,0,0));
		const Transform view = inverse( Transform::Rotate(rot) * Transform::Translate(cameraPos) );
		const Transform projection = Transform::Perspective(glm::radians(fovY), aspect, nearZ);
		return { view, projection };
	}
};

class IRenderer {
public:
	virtual void Initialize(CommandContext& context) {}
	virtual void InspectorGui(CommandContext& context) {}
	virtual void PreRender(CommandContext& context, const GBuffer& gbuffer, const Transform& view, const Transform& projection) {}
	virtual void Render(CommandContext& context) = 0;
	virtual void PostRender(CommandContext& context, const GBuffer& gbuffer) {}
};

class ViewportWidget {
private:
	EditorCamera camera = {};
	std::vector<ref<IRenderer>> renderers = {};

	TransientResourceCache<GBuffer> cachedGbuffers = {};
	uint2 cachedGbufferExtent = uint2(0,0);

public:
	inline ViewportWidget(CommandContext& context, const std::vector<ref<IRenderer>>& renderers_) : renderers(renderers_) {
		context.Begin();
		for (const auto& r : renderers)
			r->Initialize(context);
		context.Submit();
		camera = {};
	}

	inline void InspectorGui(CommandContext& context) {
		if (ImGui::CollapsingHeader("Camera")) {
			camera.InspectorGui();
		}

		for (const auto& r : renderers)
			r->InspectorGui(context);
	}

	inline void Render(CommandContext& context, double dt) {
		const float2 extentf = std::bit_cast<float2>(ImGui::GetWindowContentRegionMax()) - std::bit_cast<float2>(ImGui::GetWindowContentRegionMin());
		const uint2 extent = uint2(extentf);

		camera.Update(dt);

		if (cachedGbufferExtent != extent) {
			context.GetDevice().Wait();
			cachedGbuffers.clear();
			cachedGbufferExtent = extent;
		}
		auto gbuffer = cachedGbuffers.pop_or_create(context.GetDevice(), [&]() {
			auto renderTarget = ImageView::Create(
					Image::Create(context.GetDevice(), ImageInfo{
						.format = vk::Format::eR8G8B8A8Unorm,
						.extent = uint3(extent, 1),
						.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment,
						.queueFamilies = { context.QueueFamily() } }),
					vk::ImageSubresourceRange{
						.aspectMask = vk::ImageAspectFlagBits::eColor,
						.baseMipLevel = 0,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = 1 });
			auto visibility = ImageView::Create(
					Image::Create(context.GetDevice(), ImageInfo{
						.format = vk::Format::eR32G32B32A32Uint,
						.extent = uint3(extent, 1),
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
						.extent = uint3(extent, 1),
						.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eDepthStencilAttachment,
						.queueFamilies = { context.QueueFamily() } }),
					vk::ImageSubresourceRange{
						.aspectMask = vk::ImageAspectFlagBits::eDepth,
						.baseMipLevel = 0,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = 1 });
			return GBuffer{
				.renderTarget = renderTarget,
				.visibility = visibility,
				.depth = depth };
		});
		cachedGbuffers.push(gbuffer, context.GetDevice().NextTimelineSignal());

		ImGui::Image(Gui::GetTextureID(gbuffer.renderTarget, vk::Filter::eNearest), std::bit_cast<ImVec2>(extentf));

		const float2 viewportMin = std::bit_cast<float2>(ImGui::GetItemRectMin());
		const float2 viewportMax = std::bit_cast<float2>(ImGui::GetItemRectMax());
		ImGuizmo::SetRect(viewportMin.x, viewportMin.y, viewportMax.x - viewportMin.x, viewportMax.y - viewportMin.y);
		ImGuizmo::SetID(0);

		const auto [view,projection] = camera.GetViewProjection(extentf.x / extentf.y);

		for (const auto& r : renderers)
			r->PreRender(context, gbuffer, view, projection);

		context.AddBarrier(gbuffer.renderTarget, Image::ResourceState{
			.layout = vk::ImageLayout::eColorAttachmentOptimal,
			.stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			.access =  vk::AccessFlagBits2::eColorAttachmentRead|vk::AccessFlagBits2::eColorAttachmentWrite,
			.queueFamily = context.QueueFamily() });
		context.AddBarrier(gbuffer.visibility, Image::ResourceState{
			.layout = vk::ImageLayout::eColorAttachmentOptimal,
			.stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			.access =  vk::AccessFlagBits2::eColorAttachmentRead|vk::AccessFlagBits2::eColorAttachmentWrite,
			.queueFamily = context.QueueFamily() });
		context.AddBarrier(gbuffer.depth, Image::ResourceState{
			.layout = vk::ImageLayout::eDepthAttachmentOptimal,
			.stage  = vk::PipelineStageFlagBits2::eLateFragmentTests,
			.access =  vk::AccessFlagBits2::eDepthStencilAttachmentRead|vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			.queueFamily = context.QueueFamily() });
		context.ExecuteBarriers();

		vk::RenderingAttachmentInfo attachments[2] = {
			vk::RenderingAttachmentInfo {
				.imageView = *gbuffer.renderTarget,
				.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
				.resolveMode = vk::ResolveModeFlagBits::eNone,
				.resolveImageView = {},
				.resolveImageLayout = vk::ImageLayout::eUndefined,
				.loadOp  = vk::AttachmentLoadOp::eClear,
				.storeOp = vk::AttachmentStoreOp::eStore,
				.clearValue = vk::ClearValue{vk::ClearColorValue{std::array<float,4>{ .5f, .6f, .7f, 1.f }} } },
			vk::RenderingAttachmentInfo {
				.imageView = *gbuffer.visibility,
				.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
				.resolveMode = vk::ResolveModeFlagBits::eNone,
				.resolveImageView = {},
				.resolveImageLayout = vk::ImageLayout::eUndefined,
				.loadOp  = vk::AttachmentLoadOp::eClear,
				.storeOp = vk::AttachmentStoreOp::eStore,
				.clearValue = vk::ClearValue{vk::ClearColorValue{std::array<uint,4>{ ~0u, ~0u, ~0u, ~0u }} } } };
		vk::RenderingAttachmentInfo depthAttachment {
			.imageView = *gbuffer.depth,
			.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
			.resolveMode = vk::ResolveModeFlagBits::eNone,
			.resolveImageView = {},
			.resolveImageLayout = vk::ImageLayout::eUndefined,
			.loadOp  = vk::AttachmentLoadOp::eClear,
			.storeOp = vk::AttachmentStoreOp::eStore,
			.clearValue = vk::ClearValue{vk::ClearDepthStencilValue{1.f, 0}} };
		context->beginRendering(vk::RenderingInfo {
			.renderArea = vk::Rect2D{ vk::Offset2D{0, 0}, vk::Extent2D{ extent.x, extent.y } },
			.layerCount = 1,
			.viewMask = 0,
			.colorAttachmentCount = 2,
			.pColorAttachments = attachments,
			.pDepthAttachment = &depthAttachment });

		context->setViewport(0, vk::Viewport{ 0, 0, extentf.x, extentf.y, 0, 1 });
		context->setScissor(0, vk::Rect2D{ vk::Offset2D{0, 0}, vk::Extent2D{ extent.x, extent.y } } );

		for (const auto& r : renderers)
			r->Render(context);

		context->endRendering();

		for (const auto& r : renderers)
			r->PostRender(context, gbuffer);
	}
};

}