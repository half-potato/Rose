#include <iostream>
#include "Window.hpp"
#include "Instance.hpp"

// vulkan.h needed by glfw.h
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <imgui/imgui.h>

namespace RoseEngine {

static bool gInitialized = false;

std::pair<vk::raii::PhysicalDevice, uint32_t> Window::FindSupportedDevice(const vk::raii::Instance& instance) {
	for (const vk::raii::PhysicalDevice physicalDevice : instance.enumeratePhysicalDevices()) {
		const auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
		for (uint32_t i = 0; i < queueFamilyProperties.size(); i++)
			if (glfwGetPhysicalDevicePresentationSupport(*instance, *physicalDevice, i))
				return std::tie(physicalDevice, i);
	}
	return std::make_pair( vk::raii::PhysicalDevice(nullptr), uint32_t(-1) );
}
std::vector<uint32_t> Window::FindSupportedQueueFamilies(const vk::raii::Instance& instance, const vk::raii::PhysicalDevice& physicalDevice) {
	std::vector<uint32_t> families;
	const auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
	for (uint32_t i = 0; i < queueFamilyProperties.size(); i++)
		if (glfwGetPhysicalDevicePresentationSupport(*instance, *physicalDevice, i))
			families.emplace_back(i);
	return families;
}

std::span<const char*> Window::RequiredInstanceExtensions() {
	if (!gInitialized) {
		if (glfwInit() != GLFW_TRUE) {
			std::cerr << "Error: Failed to initialize GLFW" << std::endl;
			throw std::runtime_error("Failed to initialized GLFW");
		}
	}

	uint32_t count;
	const char** ptr = glfwGetRequiredInstanceExtensions(&count);
	return std::span<const char*>{ ptr, count };
}

void Window::WindowSizeCallback(GLFWwindow* window, int width, int height) {
	Window* w = (Window*)glfwGetWindowUserPointer(window);
	w->mClientExtent = uint2{ (uint32_t)width, (uint32_t)height };
}
void Window::DropCallback(GLFWwindow* window, int count, const char** paths) {
	Window* w = (Window*)glfwGetWindowUserPointer(window);
	for (int i = 0; i < count; i++)
		w->mDroppedFiles.emplace_back(paths[i]);
}

void ErrorCallback(int code, const char* msg) {
	std::cerr << msg;
	//throw runtime_error(msg);
}

ref<Window> Window::Create(Instance& instance, const std::string& title, const uint2& extent) {
	if (!gInitialized) {
		if (glfwInit() != GLFW_TRUE) {
			std::cerr << "Error: Failed to initialize GLFW" << std::endl;
			throw std::runtime_error("Failed to initialized GLFW");
		}
	}

	auto window = make_ref<Window>();
	window->mTitle = title;
	window->mClientExtent = extent;

	glfwSetErrorCallback(ErrorCallback);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_REFRESH_RATE, GLFW_DONT_CARE);
	window->mWindow = glfwCreateWindow(extent.x, extent.y, title.c_str(), NULL, NULL);
	glfwSetWindowUserPointer(window->mWindow, window.get());

	VkSurfaceKHR surface;
	if (glfwCreateWindowSurface(**instance, window->mWindow, NULL, &surface))
		throw std::runtime_error("Failed to create surface");
	window->mSurface = vk::raii::SurfaceKHR(*instance, surface);

	glfwSetFramebufferSizeCallback(window->mWindow, Window::WindowSizeCallback);
	glfwSetDropCallback           (window->mWindow, Window::DropCallback);

	return window;
}
Window::~Window() {
	glfwDestroyWindow(mWindow);
	glfwTerminate();
	gInitialized = false;
}

void Window::PollEvents() const {
	glfwPollEvents();
}

bool Window::IsOpen() const {
	return !glfwWindowShouldClose(mWindow);
}

void Window::Resize(const vk::Extent2D& extent) const {
	glfwSetWindowSize(mWindow, extent.width, extent.height);
}

void Window::SetFullscreen(const bool fs) {
	mFullscreen = fs;
	if (mFullscreen) {
		glfwGetWindowPos(mWindow, &mRestoreRect.offset.x, &mRestoreRect.offset.y);
		glfwGetWindowSize(mWindow, reinterpret_cast<int*>(&mRestoreRect.extent.width), reinterpret_cast<int*>(&mRestoreRect.extent.height));
		GLFWmonitor* monitor = glfwGetWindowMonitor(mWindow);
		const GLFWvidmode* mode = glfwGetVideoMode(monitor);
		glfwWindowHint(GLFW_RED_BITS, mode->redBits);
		glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
		glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
		glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
		glfwSetWindowMonitor(mWindow, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
	} else {
		glfwSetWindowMonitor(mWindow, nullptr, mRestoreRect.offset.x, mRestoreRect.offset.y, mRestoreRect.extent.width, mRestoreRect.extent.height, 0);
	}
}

}