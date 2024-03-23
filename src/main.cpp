#include <Core/Instance.hpp>
#include <Core/CommandContext.hpp>
#include <Core/TransientResourceCache.hpp>
#include <Core/Window.hpp>
#include <Core/Gui.hpp>

#include <Scene/Mesh.hpp>
#include <Scene/Transform.h>

#include <ImGuizmo.h>

using namespace RoseEngine;

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

struct Renderer {
	Mesh          mesh = {};
	MeshLayout    meshLayout = {};
	ref<Pipeline> pipeline = {};

	Transform meshTransform   = Transform::Identity();
	float3    cameraPos = float3(0,0,1);
	float2    cameraAngle = float2(0,0);
	float     fovY  = 70.f; // in degrees
	float     nearZ = 0.01f;

	inline static Renderer Create(CommandContext& context) {
		Renderer renderer = {};

		context.Begin();

		renderer.mesh = Mesh {
			.indexBuffer = context.UploadData(std::vector<uint16_t>{ 0, 1, 2, 1, 3, 2 }, vk::BufferUsageFlagBits::eIndexBuffer),
			.indexType = vk::IndexType::eUint16,
			.topology = vk::PrimitiveTopology::eTriangleList };
		renderer.mesh.vertexAttributes[MeshVertexAttributeType::ePosition].emplace_back(
			context.UploadData(std::vector<float3>{
					float3(-.25f, -.25f, 0), float3(.25f, -.25f, 0),
					float3(-.25f,  .25f, 0), float3(.25f,  .25f, 0),
				}, vk::BufferUsageFlagBits::eVertexBuffer),
			MeshVertexAttributeLayout{
				.stride = sizeof(float3),
				.format = vk::Format::eR32G32B32Sfloat,
				.offset = 0,
				.inputRate = vk::VertexInputRate::eVertex });
		renderer.mesh.vertexAttributes[MeshVertexAttributeType::eColor].emplace_back(
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
				.cullMode = vk::CullModeFlagBits::eNone,
				.frontFace = vk::FrontFace::eCounterClockwise,
				.depthBiasEnable = false },
			.multisampleState = vk::PipelineMultisampleStateCreateInfo{},
			.depthStencilState = vk::PipelineDepthStencilStateCreateInfo{
				.depthTestEnable = false,
				.depthWriteEnable = true,
				.depthCompareOp = vk::CompareOp::eGreater,
				.depthBoundsTestEnable = false,
				.stencilTestEnable = false },
			.viewports = { vk::Viewport{} },
			.scissors = { vk::Rect2D{} },
			.colorBlendState = ColorBlendState{
				.attachments = { vk::PipelineColorBlendAttachmentState{
					.blendEnable         = false,
					.srcColorBlendFactor = vk::BlendFactor::eZero,
					.dstColorBlendFactor = vk::BlendFactor::eOne,
					.colorBlendOp        = vk::BlendOp::eAdd,
					.srcAlphaBlendFactor = vk::BlendFactor::eZero,
					.dstAlphaBlendFactor = vk::BlendFactor::eOne,
					.alphaBlendOp        = vk::BlendOp::eAdd,
					.colorWriteMask      = vk::ColorComponentFlags{vk::FlagTraits<vk::ColorComponentFlagBits>::allFlags} } } },
			.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor },
			.dynamicRenderingState = DynamicRenderingState{
				.colorFormats = { format },
				.depthFormat = {} } };
		pipeline = Pipeline::CreateGraphics(device, vertexShader, fragmentShader, pipelineInfo);
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
		quat rx = glm::angleAxis( cameraAngle.x, float3(1,0,0));
		quat ry = glm::angleAxis(-cameraAngle.y, float3(0,1,0));
		Transform cameraTransform = Transform{ .transform = transpose(float4x4(ry * rx)) } * Transform::Translate(cameraPos);
		Transform view = inverse(cameraTransform);
		Transform projection = Transform{ .transform = transpose( glm::infinitePerspective(glm::radians(fovY), (float)renderTarget.Extent().x / (float)renderTarget.Extent().y, nearZ) ) };
		TransformGizmoGui(meshTransform, view, projection);

		if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
			CreatePipeline(context.GetDevice(), renderTarget.GetImage()->Info().format);

		ShaderParameter params = {};
		params["objectToWorld"] = meshTransform;
		params["worldToCamera"] = view;
		params["projection"]    = projection;

		auto descriptorSets = context.GetDescriptorSets(*pipeline->Layout());
		context.UpdateDescriptorSets(*descriptorSets, params, *pipeline->Layout());

		context.AddBarrier(renderTarget, Image::ResourceState{
			.layout = vk::ImageLayout::eColorAttachmentOptimal,
			.stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			.access =  vk::AccessFlagBits2::eColorAttachmentRead|vk::AccessFlagBits2::eColorAttachmentWrite,
			.queueFamily = context.QueueFamily()
		});
		context.ExecuteBarriers();

		vk::RenderingAttachmentInfo attachment {
			.imageView = *renderTarget,
			.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
			.resolveMode = vk::ResolveModeFlagBits::eNone,
			.resolveImageView = {},
			.resolveImageLayout = vk::ImageLayout::eUndefined,
			.loadOp  = vk::AttachmentLoadOp::eLoad,
			.storeOp = vk::AttachmentStoreOp::eStore,
			.clearValue = vk::ClearValue{vk::ClearColorValue{std::array<float,4>{}}} };
		vk::RenderingInfo renderInfo {
			.renderArea = vk::Rect2D{ vk::Offset2D{0, 0}, vk::Extent2D{ renderTarget.Extent().x, renderTarget.Extent().y } },
			.layerCount = 1,
			.viewMask = 0 };
		renderInfo.setColorAttachments(attachment);
		context->beginRendering(renderInfo);

		context->setViewport(0, vk::Viewport{ 0, 0, (float)renderTarget.Extent().x, (float)renderTarget.Extent().y, 0, 1 });
		context->setScissor(0, vk::Rect2D{ vk::Offset2D{0, 0}, vk::Extent2D{ renderTarget.Extent().x, renderTarget.Extent().y } } );

		context->bindPipeline(vk::PipelineBindPoint::eGraphics, ***pipeline);
		context.BindParameters(params, *pipeline->Layout(), descriptorSets);
		mesh.Bind(context, meshLayout);
		context->drawIndexed(mesh.indexBuffer.size_bytes() / sizeof(uint16_t), 1, 0, 0, 0);

		context->endRendering();
		renderTarget.SetState(Image::ResourceState{
			.layout = vk::ImageLayout::eColorAttachmentOptimal,
			.stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			.access = vk::AccessFlagBits2::eColorAttachmentWrite,
			.queueFamily = context.QueueFamily() });

	}
};

inline void InspectorGui(Renderer& renderer) {
	if (ImGui::CollapsingHeader("Mesh")) {
		ImGui::PushID("Mesh");
		InspectorGui(renderer.meshTransform);
		ImGui::PopID();
	}
	if (ImGui::CollapsingHeader("Camera")) {
		ImGui::PushID("Camera");
		ImGui::DragFloat3("Position", &renderer.cameraPos.x);
		ImGui::DragFloat2("Angle", &renderer.cameraAngle.x);
		Gui::ScalarField("Vertical field of view", &renderer.fovY);
		Gui::ScalarField("Near Z", &renderer.nearZ);
		ImGui::PopID();
	}
}

class App {
public:
	ref<Instance>  instance  = nullptr;
	ref<Device>    device    = nullptr;
	ref<Window>    window    = nullptr;
	ref<Swapchain> swapchain = nullptr;
	std::vector<ref<CommandContext>> contexts = {};

	vk::raii::Semaphore commandSignalSemaphore = nullptr;

	uint32_t presentQueueFamily = 0;

	std::unordered_map<std::string, std::pair<bool, std::function<void()>>> widgets = {};

	TransientResourceCache<ImageView> cachedRenderTargets = {};
	uint2 cachedRenderTargetExtent = uint2(0,0);

	double dt = 0;
	double fps = 0;
	std::chrono::high_resolution_clock::time_point lastFrame = {};

	Renderer renderer = {};

	inline void AddWidget(const std::string& name, auto fn, const bool initialState = false) {
		widgets[name] = std::make_pair(initialState, fn);
	}

	inline App(std::span<const char*, std::dynamic_extent> args) {
		std::vector<std::string> instanceExtensions;
		for (const auto& e : Window::RequiredInstanceExtensions())
			instanceExtensions.emplace_back(e);

		instance = Instance::Create(instanceExtensions, { "VK_LAYER_KHRONOS_validation" });

		vk::raii::PhysicalDevice physicalDevice = nullptr;
		std::tie(physicalDevice, presentQueueFamily) = Window::FindSupportedDevice(**instance);
		device = Device::Create(*instance, physicalDevice, { VK_KHR_SWAPCHAIN_EXTENSION_NAME });

		window    = Window::Create(*instance, "Rose", uint2(1920, 1080));
		swapchain = Swapchain::Create(*device, *window->GetSurface());

		contexts.emplace_back(CommandContext::Create(device, presentQueueFamily));
		renderer = Renderer::Create(*contexts[0]);

		commandSignalSemaphore = vk::raii::Semaphore(**device, vk::SemaphoreCreateInfo{});

		AddWidget("Memory", [=]() {
			const bool memoryBudgetExt = device->EnabledExtensions().contains(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
			vk::StructureChain<vk::PhysicalDeviceMemoryProperties2, vk::PhysicalDeviceMemoryBudgetPropertiesEXT> structureChain;
			if (memoryBudgetExt) {
				const auto tmp = device->PhysicalDevice().getMemoryProperties2<vk::PhysicalDeviceMemoryProperties2, vk::PhysicalDeviceMemoryBudgetPropertiesEXT>();
				structureChain = tmp;
			} else {
				structureChain.get<vk::PhysicalDeviceMemoryProperties2>() = device->PhysicalDevice().getMemoryProperties2();
			}

			const vk::PhysicalDeviceMemoryProperties2& properties = structureChain.get<vk::PhysicalDeviceMemoryProperties2>();
			const vk::PhysicalDeviceMemoryBudgetPropertiesEXT& budgetProperties = structureChain.get<vk::PhysicalDeviceMemoryBudgetPropertiesEXT>();

			VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
			vmaGetHeapBudgets(device->MemoryAllocator(), budgets);

			for (uint32_t heapIndex = 0; heapIndex < properties.memoryProperties.memoryHeapCount; heapIndex++) {
				const char* isDeviceLocalStr = (properties.memoryProperties.memoryHeaps[heapIndex].flags & vk::MemoryHeapFlagBits::eDeviceLocal) ? " (device local)" : "";

				if (memoryBudgetExt) {
					const auto[usage, usageUnit]   = FormatBytes(budgetProperties.heapUsage[heapIndex]);
					const auto[budget, budgetUnit] = FormatBytes(budgetProperties.heapBudget[heapIndex]);
					ImGui::Text("Heap %u%s (%llu %s / %llu %s)", heapIndex, isDeviceLocalStr, usage, usageUnit, budget, budgetUnit);
				} else
					ImGui::Text("Heap %u%s", heapIndex, isDeviceLocalStr);
				ImGui::Indent();

				// VMA stats
				{
					const auto[usage, usageUnit]   = FormatBytes(budgets[heapIndex].usage);
					const auto[budget, budgetUnit] = FormatBytes(budgets[heapIndex].budget);
					ImGui::Text("%llu %s used, %llu %s budgeted", usage, usageUnit, budget, budgetUnit);

					const auto[allocationBytes, allocationBytesUnit] = FormatBytes(budgets[heapIndex].statistics.allocationBytes);
					ImGui::Text("%u allocations\t(%llu %s)", budgets[heapIndex].statistics.allocationCount, allocationBytes, allocationBytesUnit);

					const auto[blockBytes, blockBytesUnit] = FormatBytes(budgets[heapIndex].statistics.blockBytes);
					ImGui::Text("%u memory blocks\t(%llu %s)", budgets[heapIndex].statistics.blockCount, blockBytes, blockBytesUnit);
				}

				ImGui::Unindent();
			}
		});

		AddWidget("Window", [=]() {
			{
				uint2 e = window->GetExtent();
				bool changed = false;
				ImGui::InputScalar("Width", ImGuiDataType_U32, &e.x);
				changed |= ImGui::IsItemDeactivatedAfterEdit();
				ImGui::InputScalar("Height", ImGuiDataType_U32, &e.y);
				changed |= ImGui::IsItemDeactivatedAfterEdit();
				if (changed) window->Resize(e);
			}

			vk::SurfaceCapabilitiesKHR capabilities = device->PhysicalDevice().getSurfaceCapabilitiesKHR(*window->GetSurface());
			ImGui::SetNextItemWidth(40);
			uint32_t imageCount = swapchain->GetMinImageCount();
			if (ImGui::DragScalar("Min image count", ImGuiDataType_U32, &imageCount, 1, &capabilities.minImageCount, &capabilities.maxImageCount))
				swapchain->SetMinImageCount(imageCount);
			ImGui::LabelText("Min image count", "%u", imageCount);
			ImGui::LabelText("Image count", "%u", swapchain->ImageCount());

			if (ImGui::BeginCombo("Present mode", to_string(swapchain->GetPresentMode()).c_str())) {
				for (auto mode : device->PhysicalDevice().getSurfacePresentModesKHR(*window->GetSurface()))
					if (ImGui::Selectable(vk::to_string(mode).c_str(), swapchain->GetPresentMode() == mode)) {
						swapchain->SetPresentMode(mode);
					}
				ImGui::EndCombo();
			}

			if (ImGui::CollapsingHeader("Usage flags")) {
				uint32_t usage = uint32_t(swapchain->GetImageUsage());
				for (uint32_t i = 0; i < 8; i++)
					if (ImGui::CheckboxFlags(to_string((vk::ImageUsageFlagBits)(1 << i)).c_str(), &usage, 1 << i))
						swapchain->SetImageUsage(vk::ImageUsageFlags(usage));
			}

			auto fmt_to_str = [](vk::SurfaceFormatKHR f) { return vk::to_string(f.format) + ", " + vk::to_string(f.colorSpace); };
			if (ImGui::BeginCombo("Surface format", fmt_to_str(swapchain->GetFormat()).c_str())) {
				for (auto format : device->PhysicalDevice().getSurfaceFormatsKHR(*window->GetSurface())) {
					vk::ImageFormatProperties p;
					vk::Result e = (*device->PhysicalDevice()).getImageFormatProperties(format.format, vk::ImageType::e2D, vk::ImageTiling::eOptimal, swapchain->GetImageUsage(), {}, &p);
					if (e == vk::Result::eSuccess) {
						if (ImGui::Selectable(fmt_to_str(format).c_str(), swapchain->GetFormat() == format)) {
							swapchain->SetFormat(format);
						}
					}
				}
				ImGui::EndCombo();
			}
		});

		AddWidget("Profiler", [&](){
			ImGui::Text("%.1f fps (%.1f ms)", fps, 1000 / fps);
		});

		AddWidget("Renderer", [&]() {
			InspectorGui(renderer);
		}, true);

		AddWidget("Viewport", [&]() {
			const float2 extent = std::bit_cast<float2>(ImGui::GetWindowContentRegionMax()) - std::bit_cast<float2>(ImGui::GetWindowContentRegionMin());

			renderer.Update(dt);

			if (extent.x > 0 && extent.y > 0) {
				if (cachedRenderTargetExtent != uint2(extent)) {
					device->Wait();
					cachedRenderTargets.clear();
					cachedRenderTargetExtent = uint2(extent);
				}
				ImageView renderTarget = cachedRenderTargets.pop_or_create(*device, [&]() {
					return ImageView::Create(
							Image::Create(*device, ImageInfo{
								.format = swapchain->GetFormat().format,
								.extent = uint3(uint2(extent), 1),
								.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment,
								.queueFamilies = { presentQueueFamily } }),
							vk::ImageSubresourceRange{
								.aspectMask = vk::ImageAspectFlagBits::eColor,
								.baseMipLevel = 0,
								.levelCount = 1,
								.baseArrayLayer = 0,
								.layerCount = 1 });
				});

				ImGui::Image(Gui::GetTextureID(renderTarget, vk::Filter::eNearest), std::bit_cast<ImVec2>(extent));

				float2 viewportMin = std::bit_cast<float2>(ImGui::GetItemRectMin());
				float2 viewportMax = std::bit_cast<float2>(ImGui::GetItemRectMax());
				ImGuizmo::SetRect(viewportMin.x, viewportMin.y, viewportMax.x - viewportMin.x, viewportMax.y - viewportMin.y);
				ImGuizmo::SetID(0);

				const auto& context = contexts[swapchain->ImageIndex()];
				context->ClearColor(renderTarget, vk::ClearColorValue{ std::array<float,4>{ .5f, .6f, .7f, 1.f } });
				renderer.Render(*context, renderTarget);

				cachedRenderTargets.push(renderTarget, device->NextTimelineSignal());
			}
		}, true);
	}
	inline ~App() {
		device->Wait();
		Gui::Destroy();
	}

	inline bool CreateSwapchain() {
		device->Wait();
		if (!swapchain->Recreate(*device, *window->GetSurface(), { presentQueueFamily }))
			return false; // Window unavailable (minimized?)

		contexts.resize(swapchain->ImageCount());
		for (auto& c : contexts)
			if (!c)
				c = CommandContext::Create(device, presentQueueFamily);

		Gui::Initialize(*contexts[0], *window, *swapchain, presentQueueFamily);

		cachedRenderTargets.clear();
		renderer.CreatePipeline(*device, swapchain->GetFormat().format);

		return true;
	}

	inline void Update() {
		// Menu bar
		ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize), ImGuiCond_Always;
		ImGui::Begin("Main Dockspace", nullptr, ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoBringToFrontOnFocus|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_MenuBar);

		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem("Reload shaders")) {
					renderer.CreatePipeline(*device, swapchain->GetFormat().format);
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("View")) {
				for (auto&[name, widget] : widgets) {
					if (ImGui::MenuItem(name.c_str())) {
						widget.first = !widget.first;
					}
				}
				ImGui::EndMenu();
			}

			ImGui::Dummy(ImVec2(16, ImGui::GetContentRegionAvail().y));

			ImGui::Text("Vulkan %u.%u.%u",
				VK_API_VERSION_MAJOR(instance->VulkanVersion()),
				VK_API_VERSION_MINOR(instance->VulkanVersion()),
				VK_API_VERSION_PATCH(instance->VulkanVersion()));

			ImGui::EndMenuBar();
		}

		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) {
			ImGui::DockSpace(ImGui::GetID("Main Dockspace"), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
		}

		ImGui::End();

		// Widget windows

		for (auto&[name, widget] : widgets) {
			if (widget.first) {
				if (ImGui::Begin(name.c_str(), &widget.first))
					widget.second();
				ImGui::End();
			}
		}
	}

	inline void DoFrame() {
		// count fps
		const auto now = std::chrono::high_resolution_clock::now();
		dt = std::chrono::duration_cast<std::chrono::duration<double>>(now - lastFrame).count();
		lastFrame = now;
		// moving average over the last second
		fps = lerp(fps, 1.0 / dt, std::min(1.0, dt));

		Gui::NewFrame();

		const auto& context = contexts[swapchain->ImageIndex()];

		context->Begin();
		context->ClearColor(swapchain->CurrentImage(), vk::ClearColorValue{std::array<float,4>{ .5f, .7f, 1.f, 1.f }});

		Update();

		Gui::Render(*context, swapchain->CurrentImage());

		context->AddBarrier(swapchain->CurrentImage(), Image::ResourceState{
			.layout = vk::ImageLayout::ePresentSrcKHR,
			.stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			.access = vk::AccessFlagBits2::eNone,
			.queueFamily = presentQueueFamily });
		context->ExecuteBarriers();
		context->Submit(0,
			*commandSignalSemaphore, (size_t)0,
			swapchain->ImageAvailableSemaphore(),
			(vk::PipelineStageFlags)vk::PipelineStageFlagBits::eColorAttachmentOutput,
			(size_t)0);

		swapchain->Present(*(*device)->getQueue(presentQueueFamily, 0), *commandSignalSemaphore);
	}

	inline void Run() {
		while (true) {
			Window::PollEvents();
			if (!window->IsOpen())
				break;

			if (swapchain->Dirty() || window->GetExtent() != swapchain->Extent()) {
				if (!CreateSwapchain())
					continue;
			}

			if (swapchain->AcquireImage())
				DoFrame();
		}
	}
};

int main(int argc, const char** argv) {
	App app(std::span { argv, (size_t)argc });
	app.Run();
	return EXIT_SUCCESS;
}