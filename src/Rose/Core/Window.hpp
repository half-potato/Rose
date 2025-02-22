#pragma once

#include "Swapchain.hpp"

struct GLFWwindow;

namespace RoseEngine {

class Instance;
class Image;

class Window {
public:
	static std::span<const char*>                        RequiredInstanceExtensions();
	static std::pair<vk::raii::PhysicalDevice, uint32_t> FindSupportedDevice(const vk::raii::Instance& instance);
	static std::vector<uint32_t>                         FindSupportedQueueFamilies(const vk::Instance instance, const vk::PhysicalDevice physicalDevice);
	static void                                          PollEvents();

	static ref<Window> Create(Instance& instance, const std::string& title, const uint2& extent);
	~Window();

	inline GLFWwindow*                 GetWindow() const { return mWindow; }
	inline const vk::raii::SurfaceKHR& GetSurface() const { return mSurface; }
	inline const uint2&                GetExtent() const { return mClientExtent; }
	std::vector<std::string>&          GetDroppedFiles() { return mDroppedFiles; }

	inline bool IsFullscreen() const { return mFullscreen; }
	void        SetFullscreen(const bool fs);
	void        Resize(const uint2 extent) const;
	bool        IsOpen() const;

private:
	GLFWwindow*              mWindow = nullptr;
	vk::raii::SurfaceKHR     mSurface = nullptr;
	uint2                    mClientExtent = {};
	vk::Rect2D               mRestoreRect = {};
	bool                     mFullscreen = false;
	std::vector<std::string> mDroppedFiles = {};

	ref<Swapchain>           mSwapchain = {};

	void CreateSwapchain();

	static void WindowSizeCallback(GLFWwindow* window, int width, int height);
	static void DropCallback(GLFWwindow* window, int count, const char** paths);
};

}