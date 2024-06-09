#pragma once

#include <Core/CommandContext.hpp>
#include <Core/TransientResourceCache.hpp>
#include <Core/Gui.hpp>
#include <Scene/Transform.hpp>

namespace RoseEngine {

struct GBuffer {
	ImageView renderTarget;
	ImageView visibility;
	ImageView depth;
};
struct RenderData {
	GBuffer   gbuffer;
	Transform cameraToWorld;
	Transform worldToCamera;
	Transform projection;
};

struct EditorCamera {
	float3 cameraPos   = float3(0, 2, 2);
	float2 cameraAngle = float2(-float(M_PI)/4,0);
	float  fovY  = 50.f; // in degrees
	float  nearZ = 0.01f;

	float moveSpeed = 1.f;

	inline quat Rotation() const {
		quat rx = glm::angleAxis(cameraAngle.x, float3(1,0,0));
		quat ry = glm::angleAxis(cameraAngle.y, float3(0,1,0));
		return ry * rx;
	}

	inline void Update(double dt) {
		if (ImGui::IsWindowHovered()) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
				cameraAngle += -float2(ImGui::GetIO().MouseDelta.y, ImGui::GetIO().MouseDelta.x) * float(M_PI) / 1920.f;
				cameraAngle.x = clamp(cameraAngle.x, -float(M_PI/2), float(M_PI/2));
			}
		}

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
				move = Rotation() * normalize(move);
				if (ImGui::IsKeyDown(ImGuiKey_LeftShift))
					move *= 3.f;
				cameraPos += move * moveSpeed * float(dt);
			}
		}
	}

	inline void UpdateMatrices(RenderData& renderData) const {
		renderData.cameraToWorld = Transform::Translate(cameraPos) * Transform::Rotate(Rotation());
		renderData.worldToCamera = inverse( renderData.cameraToWorld );

		float aspect = renderData.gbuffer.renderTarget.Extent().x / (float)renderData.gbuffer.renderTarget.Extent().y;
		renderData.projection = Transform::Perspective(glm::radians(fovY), aspect, nearZ);
	}
};
inline void InspectorGui(EditorCamera& camera) {
	ImGui::PushID("Camera");
	ImGui::DragFloat3("Position", &camera.cameraPos.x);
	ImGui::DragFloat2("Angle", &camera.cameraAngle.x);
	Gui::ScalarField("Vertical field of view", &camera.fovY);
	Gui::ScalarField("Near Z", &camera.nearZ);
	ImGui::PopID();
}

/*
class IRenderer {
public:
	virtual void Initialize(CommandContext& context) = 0;
	virtual void InspectorWidget(CommandContext& context) = 0;

	virtual void PreRender (CommandContext& context, const RenderData& renderData) = 0;
	virtual void Render    (CommandContext& context, const RenderData& renderData) = 0;
	virtual void PostRender(CommandContext& context, const RenderData& renderData) = 0;
};
*/

template<typename...RendererTypes>
class ViewportWidget {
public:
	using RendererRef = std::variant<ref<RendererTypes>...>;

	inline ViewportWidget(CommandContext& context, const std::vector<RendererRef>& renderers_) : renderers(renderers_) {
		context.Begin();
		for (const auto& rv : renderers)
			std::visit([&](const auto& r) { r->Initialize(context); }, rv);
		context.Submit();
	}
	inline ViewportWidget(CommandContext& context, const ref<RendererTypes>&... renderers_) : renderers({ renderers_... }) {
		context.Begin();
		for (const auto& rv : renderers)
			std::visit([&](const auto& r) { r->Initialize(context); }, rv);
		context.Submit();
	}

	inline void InspectorWidget(CommandContext& context) {
		if (ImGui::CollapsingHeader("Camera")) {
			InspectorGui(camera);
		}

		for (const auto& rv : renderers)
			std::visit([&](const auto& r) { r->InspectorWidget(context); }, rv);
	}

	inline void Render(CommandContext& context, double dt) {
		const float2 extentf = std::bit_cast<float2>(ImGui::GetWindowContentRegionMax()) - std::bit_cast<float2>(ImGui::GetWindowContentRegionMin());
		const uint2 extent = uint2(extentf);

		camera.Update(dt);

		if (extent.x == 0 || extent.y == 0) return;

		RenderData renderData = GetRenderData(context, extent);

		ImGui::Image(Gui::GetTextureID(renderData.gbuffer.renderTarget, vk::Filter::eNearest), std::bit_cast<ImVec2>(extentf));

		{ // ImGuizmo viewport
			const float2 viewportMin = std::bit_cast<float2>(ImGui::GetItemRectMin());
			const float2 viewportMax = std::bit_cast<float2>(ImGui::GetItemRectMax());
			ImGuizmo::SetRect(viewportMin.x, viewportMin.y, viewportMax.x - viewportMin.x, viewportMax.y - viewportMin.y);
			ImGuizmo::SetID(0);
		}

		{ // pre render
			context.PushDebugLabel("ViewportWidget::PreRender");
			for (const auto& rv : renderers)
				std::visit([&](const auto& r) { r->PreRender(context, renderData); }, rv);
			context.PopDebugLabel(); // ViewportWidget::PreRender
		}

		{ // barriers & beginRendering
			context.AddBarrier(renderData.gbuffer.renderTarget, Image::ResourceState{
				.layout = vk::ImageLayout::eColorAttachmentOptimal,
				.stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
				.access =  vk::AccessFlagBits2::eColorAttachmentRead|vk::AccessFlagBits2::eColorAttachmentWrite,
				.queueFamily = context.QueueFamily() });
			context.AddBarrier(renderData.gbuffer.visibility, Image::ResourceState{
				.layout = vk::ImageLayout::eColorAttachmentOptimal,
				.stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
				.access =  vk::AccessFlagBits2::eColorAttachmentRead|vk::AccessFlagBits2::eColorAttachmentWrite,
				.queueFamily = context.QueueFamily() });
			context.AddBarrier(renderData.gbuffer.depth, Image::ResourceState{
				.layout = vk::ImageLayout::eDepthAttachmentOptimal,
				.stage  = vk::PipelineStageFlagBits2::eLateFragmentTests,
				.access =  vk::AccessFlagBits2::eDepthStencilAttachmentRead|vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
				.queueFamily = context.QueueFamily() });
			context.ExecuteBarriers();

			context.PushDebugLabel("ViewportWidget::Render");
			vk::RenderingAttachmentInfo attachments[2] = {
				vk::RenderingAttachmentInfo {
					.imageView = *renderData.gbuffer.renderTarget,
					.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
					.resolveMode = vk::ResolveModeFlagBits::eNone,
					.resolveImageView = {},
					.resolveImageLayout = vk::ImageLayout::eUndefined,
					.loadOp  = vk::AttachmentLoadOp::eClear,
					.storeOp = vk::AttachmentStoreOp::eStore,
					.clearValue = vk::ClearValue{vk::ClearColorValue{std::array<float,4>{ 0, 0, 0, 0 }} } },
				vk::RenderingAttachmentInfo {
					.imageView = *renderData.gbuffer.visibility,
					.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
					.resolveMode = vk::ResolveModeFlagBits::eNone,
					.resolveImageView = {},
					.resolveImageLayout = vk::ImageLayout::eUndefined,
					.loadOp  = vk::AttachmentLoadOp::eClear,
					.storeOp = vk::AttachmentStoreOp::eStore,
					.clearValue = vk::ClearValue{vk::ClearColorValue{std::array<uint,4>{ ~0u, ~0u, ~0u, ~0u }} } } };
			vk::RenderingAttachmentInfo depthAttachment {
				.imageView = *renderData.gbuffer.depth,
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
		}

		{ // render
			context->setViewport(0, vk::Viewport{ 0, 0, extentf.x, extentf.y, 0, 1 });
			context->setScissor(0, vk::Rect2D{ vk::Offset2D{0, 0}, vk::Extent2D{ extent.x, extent.y } } );

			for (const auto& rv : renderers)
				std::visit([&](const auto& r) { r->Render(context, renderData); }, rv);

			context->endRendering();
			context.PopDebugLabel(); // ViewportWidget::Render
		}

		{ // post render
			context.PushDebugLabel("ViewportWidget::PostRender");
			for (const auto& rv : renderers)
				std::visit([&](const auto& r) { r->PostRender(context, renderData); }, rv);
			context.PopDebugLabel(); // ViewportWidget::PostRender
		}
	}

private:
	EditorCamera camera = {};
	std::vector<RendererRef> renderers = {};

	TransientResourceCache<GBuffer> cachedGBuffers = {};
	uint2 cachedGbufferExtent = uint2(0,0);

	inline RenderData GetRenderData(CommandContext& context, const uint2 extent) {
		RenderData renderData = {};

		if (cachedGbufferExtent != extent) {
			context.GetDevice().Wait();
			cachedGBuffers.clear();
			cachedGbufferExtent = extent;
		}
		renderData.gbuffer = cachedGBuffers.pop_or_create(context.GetDevice(), [&]() {
			auto renderTarget = ImageView::Create(
					Image::Create(context.GetDevice(), ImageInfo{
						.format = vk::Format::eR8G8B8A8Unorm,
						.extent = uint3(extent, 1),
						.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eColorAttachment,
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
				.visibility   = visibility,
				.depth        = depth };
		});
		cachedGBuffers.push(renderData.gbuffer, context.GetDevice().NextTimelineSignal());

		camera.UpdateMatrices(renderData);

		return renderData;
	}
};

}