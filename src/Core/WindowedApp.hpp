#pragma once

#include "Instance.hpp"
#include "Window.hpp"
#include "CommandContext.hpp"
#include "Gui.hpp"

namespace RoseEngine {

// Stores Widgets which have a callback that is called every frame.
struct WindowedApp {
	ref<Instance>  instance  = nullptr;
	ref<Device>    device    = nullptr;
	ref<Window>    window    = nullptr;
	ref<Swapchain> swapchain = nullptr;
	std::vector<ref<CommandContext>> contexts = {};

	vk::raii::Semaphore commandSignalSemaphore = nullptr;

	uint32_t presentQueueFamily = 0;

	struct Widget {
		std::function<void()> draw;
		bool visible = true;
		ImGuiWindowFlags flags = (ImGuiWindowFlags)0;
	};

	std::unordered_map<std::string, Widget> widgets = {};

	double dt = 0;
	double fps = 0;
	std::chrono::high_resolution_clock::time_point lastFrame = {};

	inline WindowedApp(std::span<const char*, std::dynamic_extent> args) {
		std::vector<std::string> instanceExtensions;
		for (const auto& e : Window::RequiredInstanceExtensions())
			instanceExtensions.emplace_back(e);

		instance = Instance::Create(instanceExtensions, {
			"VK_LAYER_KHRONOS_validation",
			//"VK_LAYER_KHRONOS_synchronization2"
		});

		vk::raii::PhysicalDevice physicalDevice = nullptr;
		std::tie(physicalDevice, presentQueueFamily) = Window::FindSupportedDevice(**instance);
		device = Device::Create(*instance, physicalDevice, { VK_KHR_SWAPCHAIN_EXTENSION_NAME });

		window    = Window::Create(*instance, "Rose", uint2(1920, 1080));
		swapchain = Swapchain::Create(*device, *window->GetSurface());

		contexts.emplace_back(CommandContext::Create(device, presentQueueFamily));

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

		AddWidget("Dear ImGui Demo", [&]() {
			ImGui::ShowDemoWindow(&widgets["Dear ImGui Demo"].visible);
		});

	}
	inline ~WindowedApp() {
		device->Wait();
		Gui::Destroy();
	}

	inline void AddWidget(const std::string& name, auto fn, const bool initialState = false, const ImGuiWindowFlags flags = (ImGuiWindowFlags)0) {
		widgets[name] = Widget{fn, initialState, flags};
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

		return true;
	}

	inline void Update() {
		// window dockspace
		ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize), ImGuiCond_Always;
		ImGui::Begin("Main Dockspace", nullptr, ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoBringToFrontOnFocus|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_MenuBar);

		// Menu bar
		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem("Open")) {
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("View")) {
				for (auto&[name, widget] : widgets) {
					if (ImGui::MenuItem(name.c_str())) {
						widget.visible = !widget.visible;
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

		// Widgets

		for (auto&[name, widget] : widgets) {
			if (widget.visible) {
				if (ImGui::Begin(name.c_str(), &widget.visible, widget.flags))
					widget.draw();
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

}