#pragma once

#include <Rose/Core/CommandContext.hpp>
#include <Rose/Core/TransientResourceCache.hpp>
#include <Rose/Core/Gui.hpp>
#include <Rose/Scene/Transform.hpp>

namespace RoseEngine {

struct ViewportCamera {
	float3 cameraPos   = float3(0, 2, 2);
	float2 cameraAngle = float2(-float(M_PI)/4,0);
	float  fovY  = 50.f; // in degrees
	float  nearZ = 0.01f;

	float moveSpeed = 1.f;

	inline void DrawInspectorGui() {
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

	inline quat Rotation() const {
		quat rx = glm::angleAxis(cameraAngle.x, float3(1,0,0));
		quat ry = glm::angleAxis(cameraAngle.y, float3(0,1,0));
		return ry * rx;
	}

	inline Transform GetCameraToWorld() const {
		return Transform::Translate(cameraPos) * Transform::Rotate(Rotation());
	}

	// aspect = width / height
	inline Transform GetProjection(float aspect) const {
		return Transform::Perspective(glm::radians(fovY), aspect, nearZ);
	}
};

struct ViewportAttachmentInfo {
	std::string    name;
	vk::Format     format;
	vk::ClearValue clearValue;
};

template<typename T> concept has_member_InspectorWidget = requires(T r, CommandContext& context) { r.InspectorWidget(context); };
template<typename T, typename ArgType> concept has_member_PreRender  = requires(T r, CommandContext& context, ArgType& renderData) { r.PreRender (context, renderData); };
template<typename T, typename ArgType> concept has_member_Render     = requires(T r, CommandContext& context, ArgType& renderData) { r.Render    (context, renderData); };
template<typename T, typename ArgType> concept has_member_PostRender = requires(T r, CommandContext& context, ArgType& renderData) { r.PostRender(context, renderData); };

// Each RendererTypes can implement the following functions:
// void InspectorWidget(CommandContext& context)
// void PreRender (CommandContext& context, const RenderArgType& renderData);
// void Render    (CommandContext& context, const RenderArgType& renderData);
// void PostRender(CommandContext& context, const RenderArgType& renderData);
template<typename RenderArgType, typename...RendererTypes>
class ViewportWidget {
public:
	using RendererRef = std::variant<ref<RendererTypes>...>;

private:
	std::vector<ViewportAttachmentInfo> attachmentInfos = {};
	std::vector<RendererRef> renderers = {};
	ViewportCamera camera = {};

	template<has_member_InspectorWidget T>
	inline static void RendererDrawGui(T& r, CommandContext& context) { r.InspectorWidget(context); }
	template<typename T>
	inline static void RendererDrawGui(T& r, CommandContext& context) {}

	template<has_member_PreRender<RenderArgType> T>
	inline static void RendererPreRender (T& r, CommandContext& context, RenderArgType& renderData) { r.PreRender(context, renderData); }
	template<typename T>
	inline static void RendererPreRender(T& r, CommandContext& context, RenderArgType& renderData) {}

	template<has_member_Render<RenderArgType> T>
	inline static void RendererRender    (T& r, CommandContext& context, RenderArgType& renderData) { r.Render(context, renderData); }
	template<typename T>
	inline static void RendererRender(T& r, CommandContext& context, RenderArgType& renderData) {}

	template<has_member_PostRender<RenderArgType> T>
	inline static void RendererPostRender(T& r, CommandContext& context, RenderArgType& renderData) { r.PostRender(context, renderData); }
	template<typename T>
	inline static void RendererPostRender(T& r, CommandContext& context, RenderArgType& renderData) {}

	inline void BeginRendering(CommandContext& context, auto getImageFn) const {
		uint2 imageExtent;

		std::vector<vk::RenderingAttachmentInfo> attachments;
		vk::RenderingAttachmentInfo depthAttachmentInfo;
		bool hasDepthAttachment = false;
		attachments.reserve(attachmentInfos.size());
		for (const auto& [name, format, clearValue] : attachmentInfos) {
			const ImageView& attachment = getImageFn(name);
			imageExtent = attachment.Extent();
			if (IsDepthStencil(format)) {
				context.AddBarrier(attachment, Image::ResourceState{
					.layout = vk::ImageLayout::eDepthAttachmentOptimal,
					.stage  = vk::PipelineStageFlagBits2::eLateFragmentTests,
					.access =  vk::AccessFlagBits2::eDepthStencilAttachmentRead|vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
					.queueFamily = context.QueueFamily()
				});

				depthAttachmentInfo = vk::RenderingAttachmentInfo {
					.imageView = *attachment,
					.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
					.resolveMode = vk::ResolveModeFlagBits::eNone,
					.resolveImageView = {},
					.resolveImageLayout = vk::ImageLayout::eUndefined,
					.loadOp  = vk::AttachmentLoadOp::eClear,
					.storeOp = vk::AttachmentStoreOp::eStore,
					.clearValue = clearValue
				};

				hasDepthAttachment = true;
			} else {
				context.AddBarrier(attachment, Image::ResourceState{
					.layout = vk::ImageLayout::eColorAttachmentOptimal,
					.stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
					.access =  vk::AccessFlagBits2::eColorAttachmentRead|vk::AccessFlagBits2::eColorAttachmentWrite,
					.queueFamily = context.QueueFamily()
				});

				attachments.emplace_back(vk::RenderingAttachmentInfo {
					.imageView = *attachment,
					.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
					.resolveMode = vk::ResolveModeFlagBits::eNone,
					.resolveImageView = {},
					.resolveImageLayout = vk::ImageLayout::eUndefined,
					.loadOp  = vk::AttachmentLoadOp::eClear,
					.storeOp = vk::AttachmentStoreOp::eStore,
					.clearValue = clearValue
				});
			}
		}

		context.ExecuteBarriers();

		context->beginRendering(vk::RenderingInfo {
			.renderArea = vk::Rect2D{ vk::Offset2D{0, 0}, vk::Extent2D{ imageExtent.x, imageExtent.y } },
			.layerCount = 1,
			.viewMask = 0,
			.colorAttachmentCount = (uint32_t)attachments.size(),
			.pColorAttachments = attachments.data(),
			.pDepthAttachment = hasDepthAttachment ? &depthAttachmentInfo : nullptr,
			.pStencilAttachment = nullptr
		});

		context->setViewport(0, vk::Viewport{ 0, 0, (float)imageExtent.x, (float)imageExtent.y, 0, 1 });
		context->setScissor(0, vk::Rect2D{ vk::Offset2D{0, 0}, vk::Extent2D{ imageExtent.x, imageExtent.y } } );
	}

public:
	inline const auto& Camera() const { return camera; }
	inline const auto& AttachmentInfos() const { return attachmentInfos; }

	inline ViewportWidget(const std::vector<ViewportAttachmentInfo>& attachments_, const std::vector<RendererRef>& renderers_) : attachmentInfos(attachments_), renderers(renderers_) {}

	inline void InspectorWidget(CommandContext& context) {
		if (ImGui::CollapsingHeader("Camera")) {
			camera.DrawInspectorGui();
		}

		for (const auto& rv : renderers)
			std::visit([&](auto& r) { RendererDrawGui(*r, context); }, rv);
	}

	inline void Update(double dt) {
		camera.Update(dt);
	}

	inline void Render(CommandContext& context, RenderArgType& renderData, auto getImageFn) const {
		{ // pre render
			context.PushDebugLabel("ViewportWidget::PreRender");
			for (const auto& rv : renderers)
				std::visit([&](auto& r) { RendererPreRender(*r, context, renderData); }, rv);
			context.PopDebugLabel(); // ViewportWidget::PreRender
		}

		 // rasterization
		BeginRendering(context, getImageFn);
		{
			context.PushDebugLabel("ViewportWidget::Render");
			for (const auto& rv : renderers)
				std::visit([&](const auto& r) { RendererRender(*r, context, renderData); }, rv);
			context.PopDebugLabel(); // ViewportWidget::Render
		}
		context->endRendering();

		{ // post render
			context.PushDebugLabel("ViewportWidget::PostRender");
			for (const auto& rv : renderers)
				std::visit([&](const auto& r) { RendererPostRender(*r, context, renderData); }, rv);
			context.PopDebugLabel(); // ViewportWidget::PostRender
		}
	}
};

}