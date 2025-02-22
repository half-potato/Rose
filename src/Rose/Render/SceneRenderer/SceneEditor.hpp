#pragma once

#include <queue>
#include <portable-file-dialogs.h>

#include "SceneRenderer.hpp"

namespace RoseEngine {

ref<SceneNode> LoadGLTF(CommandContext& context, const std::filesystem::path& filename);

class SceneEditor {
private:
	ref<SceneRenderer> scene = nullptr;
	weak_ref<SceneNode> selected = {};

	ref<Pipeline> outlinePipeline = {};
	ref<Pipeline> buildVbvhPipeline = {};
	ref<Pipeline> drawVbvhPipeline = {};

	uint32_t operation = ImGuizmo::TRANSLATE | ImGuizmo::ROTATE;
	bool opLocal = false;
	bool opOriginWorld = false;

	struct ViewportPickerData {
		BufferRange<uint4>               visibility = {};
		uint64_t                         timelineCounterValue = 0;
		std::vector<weak_ref<SceneNode>> nodes = {};
	};
	std::queue<ViewportPickerData> viewportPickerQueue = {};

	inline void SceneNodeTreeGui(SceneNode* n, const SceneNode* selected_ptr = nullptr) {
		bool open = true;

		ImGui::PushID(n);
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_OpenOnDoubleClick;
		if (selected_ptr == n)      flags |= ImGuiTreeNodeFlags_Selected;
		if (std::ranges::empty(*n)) flags |= ImGuiTreeNodeFlags_Leaf;
		open = ImGui::TreeNodeEx(n->Name() == "" ? "<unnamed>" : n->Name().c_str(), flags);

		if (ImGui::IsItemClicked())
			selected = n->shared_from_this();

		if (ImGui::BeginPopupContextWindow()) {
			bool deleted = false;
			if (ImGui::Selectable("Delete")) {
				if (n->GetParent()) {
					n->GetParent()->RemoveChild(n);
					deleted = true;
				}
			}
			ImGui::EndPopup();
			if (deleted) return;
		}

		if (open) {
			for (const ref<SceneNode>& c : *n)
				SceneNodeTreeGui(c.get(), selected_ptr);

			ImGui::TreePop();
		}

		ImGui::PopID();
	}

public:
	inline SceneEditor(const ref<SceneRenderer>& scene_) : scene(scene_) {}

	inline void Initialize(CommandContext& context) {}

	inline void LoadScene(CommandContext& context) {
		auto f = pfd::open_file("Open scene", "", {
			//"All files (.*)", "*.*",
			"glTF Scenes (.gltf .glb)", "*.gltf *.glb",
			"Environment maps (.exr .hdr .dds .png .jpg)", "*.exr *.hdr *.dds *.png *.jpg",
		});
		for (const std::string& filepath : f.result()) {
			std::filesystem::path p = filepath;
			if (p.extension() == ".gltf" || p.extension() == ".glb") {
				scene->SetScene(nullptr);
				context.GetDevice().Wait();
				const ref<SceneNode> s = LoadGLTF(context, filepath);
				if (!s) continue;
				scene->SetScene(s);
			} else {
				const PixelData d = LoadImageFile(context, p);
				if (!d.data) continue;
				const uint32_t maxMips = GetMaxMipLevels(d.extent);
				const ImageView img = ImageView::Create(
					Image::Create(context.GetDevice(), ImageInfo{
						.format = d.format,
						.extent = d.extent,
						.mipLevels = maxMips,
						.queueFamilies = { context.QueueFamily() } }),
					vk::ImageSubresourceRange{
						.aspectMask = vk::ImageAspectFlagBits::eColor,
						.baseMipLevel = 0,
						.levelCount = maxMips,
						.baseArrayLayer = 0,
						.layerCount = 1 });
				if (!img) continue;
				context.Copy(d.data, img);
				context.GenerateMipMaps(img.mImage);
				scene->SetBackgroundImage(img);
				scene->SetBackgroundColor(float3(1));
			}

		}
	}

	inline void SceneGraphWidget() {
		if (scene && scene->GetSceneRoot()) {
			SceneNode* sel_p = selected.lock().get();
			for (const ref<SceneNode>& c : *scene->GetSceneRoot())
				SceneNodeTreeGui(c.get(), sel_p);
		}
	}

	inline void ToolsWidget() {
		const float w = ImGui::GetWindowContentRegionWidth();

		if (ImGui::IsKeyPressed(ImGuiKey_O, false) && !ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) opOriginWorld = !opOriginWorld;
		if (ImGui::Selectable("Object", opOriginWorld, 0, ImVec2(w/2,0))) opOriginWorld = true;
		ImGui::SameLine();
		if (ImGui::Selectable("AABB", !opOriginWorld, 0, ImVec2(w/2,0))) opOriginWorld = false;

		if (ImGui::IsKeyPressed(ImGuiKey_L, false)) opLocal = !opLocal;
		if (ImGui::Selectable("Local", opLocal, 0, ImVec2(w/2,0))) opLocal = true;
		ImGui::SameLine();
		if (ImGui::Selectable("Global", !opLocal, 0, ImVec2(w/2,0))) opLocal = false;

		if (ImGui::Selectable("Translate", (operation & ImGuizmo::TRANSLATE) != 0) || ImGui::IsKeyPressed(ImGuiKey_T, false)) operation ^= (uint32_t)ImGuizmo::TRANSLATE;
		if (ImGui::Selectable("Rotate",    (operation & ImGuizmo::ROTATE) != 0)    || ImGui::IsKeyPressed(ImGuiKey_R, false)) operation ^= (uint32_t)ImGuizmo::ROTATE;
		if (ImGui::Selectable("Scale",     (operation & ImGuizmo::SCALE) != 0)     || ImGui::IsKeyPressed(ImGuiKey_G, false)) operation ^= (uint32_t)ImGuizmo::SCALE;
	}

	inline void InspectorWidget(CommandContext& context) {
		bool changed = false;

		auto n = selected.lock();
		if (!n) return;

		{
			float3 c = scene->GetBackgroundColor();
			if (ImGui::ColorEdit3("Background color", &c.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float)) {
				scene->SetBackgroundColor(c);
				changed = true;
			}
		}

		if (ImGui::CollapsingHeader("Selected node")) {
			ImGui::Text("Transform: %s", n->transform ? "true" : "false");
			if (n->transform.has_value()) {
				if (InspectorGui(n->transform.value()))
					changed = true;
			} else {
				Transform t = Transform::Identity();
				if (InspectorGui(t)) {
					n->transform = t;
					changed = true;
				}
			}
			if (n->material) {
				if (InspectorGui(*n->material))
					changed = true;
			}
		}

		if (changed)
			scene->SetDirty();
	}

	inline void PreRender(CommandContext& context, const RenderData& renderData) {
		if (!viewportPickerQueue.empty()) {
			auto&[buf, value, nodes] = viewportPickerQueue.front();
			if (context.GetDevice().CurrentTimelineValue() >= value) {
				if (buf[0].x < nodes.size())
					selected = std::move(nodes[buf[0].x]);
				else
					selected.reset();
				viewportPickerQueue.pop();
			}
		}

		if (auto n = selected.lock(); n) {
			Transform parentTransform = Transform::Identity();
			SceneNode* parent = n->GetParent().get();
			while (parent) {
				if (parent->transform.has_value())
					parentTransform = parent->transform.value() * parentTransform;
				parent = parent->GetParent().get();
			}

			if (!opOriginWorld && n->mesh) {
				const float3 aabbMin = float3(n->mesh->aabb.minX, n->mesh->aabb.minY, n->mesh->aabb.minZ);
				const float3 aabbMax = float3(n->mesh->aabb.maxX, n->mesh->aabb.maxY, n->mesh->aabb.maxZ);
				parentTransform = parentTransform * Transform::Translate((aabbMin + aabbMax) / 2.f);
			}

			Transform t = n->transform.has_value() ? (parentTransform * n->transform.value()) : parentTransform;
			if (TransformGizmoGui(t, renderData.worldToCamera, renderData.projection, (ImGuizmo::OPERATION)operation, opLocal)) {
				n->transform = inverse(parentTransform) * t;
				scene->SetDirty();
			}
		}
	}

	inline void Render(CommandContext& context, const RenderData& renderData) {}

	inline void PostRender(CommandContext& context, const RenderData& renderData) {
		if (auto n = selected.lock(); n && n->mesh && n->material) {
			uint32_t idx = -1;
			for (uint32_t i = 0; i < scene->GetInstanceNodes().size(); i++) {
				if (scene->GetInstanceNodes()[i].lock() == n) {
					idx = i;
					break;
				}
			}
			if (idx != -1) {
				// outline selected object
				{
					if (!outlinePipeline || (ImGui::IsKeyDown(ImGuiKey_F5) && outlinePipeline->GetShader()->IsStale())) {
						if (outlinePipeline) context.GetDevice().Wait();
						outlinePipeline = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), FindShaderPath("Outline.cs.slang")));
					}

					ShaderParameter params = {};
					params["color"]      = ImageParameter{renderData.gbuffer.renderTarget, vk::ImageLayout::eGeneral};
					params["visibility"] = ImageParameter{renderData.gbuffer.visibility, vk::ImageLayout::eShaderReadOnlyOptimal};
					params["highlightColor"] = float3(1, 0.9f, 0.2f);
					params["selected"] = idx;
					context.Dispatch(*outlinePipeline, renderData.gbuffer.renderTarget.Extent(), params);
				}

				{
					ShaderParameter params = {};
					params["scene"]      = scene->GetSceneParameters();
					params["color"]      = ImageParameter{renderData.gbuffer.renderTarget, vk::ImageLayout::eGeneral};
					params["selected"] = idx;
					params["imageSize"] = uint2(renderData.gbuffer.renderTarget.Extent());
					params["worldToCamera"] = renderData.worldToCamera;
					params["projection"]    = renderData.projection;

					// build vbvh

					if (!buildVbvhPipeline || (ImGui::IsKeyDown(ImGuiKey_F5) && buildVbvhPipeline->GetShader()->IsStale())) {
						if (buildVbvhPipeline) context.GetDevice().Wait();
						buildVbvhPipeline = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), FindShaderPath("vbvh.cs.slang"), "build"));
					}
					context.Dispatch(*buildVbvhPipeline, 1u, params);

					// draw vbvh

					if (!drawVbvhPipeline || (ImGui::IsKeyDown(ImGuiKey_F5) && drawVbvhPipeline->GetShader()->IsStale())) {
						if (drawVbvhPipeline) context.GetDevice().Wait();
						drawVbvhPipeline = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), FindShaderPath("vbvh.cs.slang"), "render"));
					}
					context.Dispatch(*drawVbvhPipeline, renderData.gbuffer.renderTarget.Extent(), params);
				}
			}
		}

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowFocused() && !ImGuizmo::IsUsing()) {
			// copy selected pixel from visibility buffer into host-visible buffer
			float4 rect;
			ImGuizmo::GetRect(&rect.x);
			float2 cursorScreen = std::bit_cast<float2>(ImGui::GetIO().MousePos);
			int2 cursor = int2(cursorScreen - float2(rect));
			if (cursor.x >= 0 && cursor.y >= 0 && cursor.x < int(rect.z) && cursor.y < int(rect.w)) {
				context.AddBarrier(renderData.gbuffer.visibility, Image::ResourceState{
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

				context->copyImageToBuffer(**renderData.gbuffer.visibility.GetImage(), vk::ImageLayout::eTransferSrcOptimal, **buf.mBuffer, vk::BufferImageCopy{
					.bufferOffset = 0,
					.bufferRowLength = 0,
					.bufferImageHeight = 0,
					.imageSubresource = renderData.gbuffer.visibility.GetSubresourceLayer(),
					.imageOffset = vk::Offset3D{ cursor.x, cursor.y, 0 },
					.imageExtent = vk::Extent3D{ 1, 1, 1 } });

				viewportPickerQueue.push({ buf, context.GetDevice().NextTimelineSignal(), scene->GetInstanceNodes() });
			}
		}
	}
};

}