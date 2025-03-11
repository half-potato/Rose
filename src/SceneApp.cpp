#include <Rose/Core/WindowedApp.hpp>
#include <Rose/Render/SceneRenderer/SceneRenderer.hpp>
#include <Rose/Render/SceneRenderer/SceneEditor.hpp>

#include <ImGuizmo.h>

using namespace RoseEngine;

int main(int argc, const char** argv) {
	WindowedApp app("GLTF Viewer", {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_KHR_RAY_QUERY_EXTENSION_NAME,
		VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME,
	});

	auto sceneRenderer = make_ref<SceneRenderer>();
	auto sceneEditor   = make_ref<SceneEditor>();

	ref<Scene> scene = make_ref<Scene>();
	sceneRenderer->SetScene(scene);
	sceneEditor->SetScene(scene);

	ViewportWidget<SceneRendererArgs, SceneRenderer, SceneEditor> viewport(
		{ // attachments
			{ "renderTarget", vk::Format::eR8G8B8A8Unorm,    vk::ClearValue{vk::ClearColorValue(0.f,0.f,0.f,1.f)} },
			{ "visibility",   vk::Format::eR32G32B32A32Uint, vk::ClearValue{vk::ClearColorValue(~0u,~0u,~0u,~0u)} },
			{ "depthBuffer",  vk::Format::eD32Sfloat,        vk::ClearValue{vk::ClearDepthStencilValue{1.f, 0}} },
		},
		{ // renderers
			sceneRenderer,
			sceneEditor
		}
	);

	app.AddMenuItem("File", [&]() {
		if (ImGui::MenuItem("Open scene")) {
			scene->LoadDialog(app.CurrentContext());
		}
	});
	app.AddWidget("Renderers", [&]() { viewport.InspectorWidget(app.CurrentContext()); }, true);

	SceneRendererArgs renderData;
	app.AddWidget("Viewport", [&]() {
		viewport.Update(app.dt);

		const float2 extentf = std::bit_cast<float2>(ImGui::GetWindowContentRegionMax()) - std::bit_cast<float2>(ImGui::GetWindowContentRegionMin());
		const uint2 extent = extentf;
		if (extent.x == 0 || extent.y == 0) return;

		CommandContext& context = app.CurrentContext();

		{
			if (renderData.renderExtent != extent) {
				context.GetDevice().Wait();
				renderData.attachments.clear();
				for (const auto&[name, format, clearValue] : viewport.AttachmentInfos()) {
					ImageView attachment;
					if (IsDepthStencil(format)) {
						attachment = ImageView::Create(
							Image::Create(context.GetDevice(), ImageInfo{
								.format = format,
								.extent = uint3(extent, 1),
								.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eDepthStencilAttachment,
								.queueFamilies = { context.QueueFamily() } }),
							vk::ImageSubresourceRange{
								.aspectMask = vk::ImageAspectFlagBits::eDepth,
								.baseMipLevel = 0,
								.levelCount = 1,
								.baseArrayLayer = 0,
								.layerCount = 1 });
					} else {
						attachment = ImageView::Create(
							Image::Create(context.GetDevice(), ImageInfo{
								.format = format,
								.extent = uint3(extent, 1),
								.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eColorAttachment,
								.queueFamilies = { context.QueueFamily() } }));
					}
					renderData.attachments.emplace_back(name, attachment);
				}

				renderData.renderExtent = extent;
			}

			renderData.cameraToWorld = viewport.Camera().GetCameraToWorld();
			renderData.worldToCamera = inverse(renderData.cameraToWorld);
			renderData.projection = viewport.Camera().GetProjection(extent.x / (float)extent.y);
		}

		ImGui::Image(Gui::GetTextureID(renderData.GetAttachment("renderTarget"), vk::Filter::eNearest), std::bit_cast<ImVec2>(extentf));

		// ImGuizmo viewport
		const float2 viewportMin = std::bit_cast<float2>(ImGui::GetItemRectMin());
		const float2 viewportMax = std::bit_cast<float2>(ImGui::GetItemRectMax());
		ImGuizmo::SetRect(viewportMin.x, viewportMin.y, viewportMax.x - viewportMin.x, viewportMax.y - viewportMin.y);
		ImGuizmo::SetID(0);

		renderData.viewportFocused = ImGui::IsItemFocused();
		renderData.viewportRect = float4(viewportMin, viewportMax - viewportMin);

		viewport.Render(context, renderData, [&](const std::string& name){ return renderData.GetAttachment(name); });
	}, true);

	app.AddWidget("Scene graph", [&]() { sceneEditor->SceneGraphWidget(); }, true);
	app.AddWidget("Tools", [&]()       { sceneEditor->ToolsWidget(); }, true);

	app.Run();

	app.device->Wait();

	return EXIT_SUCCESS;
}