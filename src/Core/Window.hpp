#pragma once

#include "Swapchain.hpp"
#include "MathTypes.hpp"

struct GLFWwindow;

namespace RoseEngine {

class Instance;
class Image;

class Window {
public:
	static ref<Window> Create(Instance& instance, const std::string& title, const uint2& extent);
	~Window();

	inline GLFWwindow*                 GetWindow() const { return mWindow; }
	inline const std::string&          GetTitle() const { return mTitle; }
	inline const vk::raii::SurfaceKHR& GetSurface() const { return mSurface; }
	inline const uint2&                GetExtent() const {return mClientExtent; }
	std::vector<std::string>&          GetDroppedFiles() { return mDroppedFiles; }

	inline bool IsFullscreen() const { return mFullscreen; }
	void        SetFullscreen(const bool fs);
	bool        IsOpen() const;
	void        Resize(const vk::Extent2D& extent) const;
	void        PollEvents() const;

	static std::span<const char*>                        RequiredInstanceExtensions();
	static std::pair<vk::raii::PhysicalDevice, uint32_t> SupportedDevice(const vk::raii::Instance& instance);
	static std::vector<uint32_t>                         SupportedQueueFamilies(const vk::Instance instance, const vk::PhysicalDevice physicalDevice);

private:
	GLFWwindow*              mWindow = nullptr;
	vk::raii::SurfaceKHR     mSurface = nullptr;
	std::string              mTitle = {};
	uint2                    mClientExtent = {};
	vk::Rect2D               mRestoreRect = {};
	bool                     mFullscreen = false;
	bool                     mRecreateSwapchain = false;
	std::vector<std::string> mDroppedFiles = {};

	void CreateSwapchain();

	static void WindowSizeCallback(GLFWwindow* window, int width, int height);
	static void DropCallback(GLFWwindow* window, int count, const char** paths);
};

}