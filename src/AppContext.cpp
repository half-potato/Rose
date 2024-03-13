#include "AppContext.hpp"

#include <GLFW/glfw3.h>

namespace RoseEngine {

AppContext CreateContext() {
	AppContext context;

	std::vector<std::string> instanceExtensions = {
		VK_KHR_SURFACE_EXTENSION_NAME,
	#ifdef _WIN32
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
	#endif
	#ifdef __linux
		VK_KHR_XCB_SURFACE_EXTENSION_NAME,
	#endif
	};

	uint32_t count;
	const char** exts = glfwGetRequiredInstanceExtensions(&count);
	for (uint32_t i = 0; i < count; i++) instanceExtensions.emplace_back(exts[i]);

	context.mInstance = std::make_unique<Instance>(instanceExtensions);

	vk::raii::PhysicalDevice physicalDevice = nullptr;
	for (const auto& device : (*context.mInstance)->enumeratePhysicalDevices())
		physicalDevice = device;

	std::vector<std::string> deviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME

		VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,

		VK_KHR_RAY_QUERY_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
	};

	context.mDevices.emplace_back(*context.mInstance, physicalDevice, deviceExtensions);

	return context;
}

}