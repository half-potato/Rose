#pragma once

#include "Image.hpp"

namespace RoseEngine {

class Swapchain {
public:
	static ref<Swapchain> Create(Device& device, const vk::raii::SurfaceKHR& surface,
		const uint32_t minImages = 2,
		const vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
		const vk::SurfaceFormatKHR preferredSurfaceFormat = vk::SurfaceFormatKHR(vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear),
		const vk::PresentModeKHR presentMode = vk::PresentModeKHR::eImmediate);

	inline       vk::raii::SwapchainKHR& operator*()        { return mSwapchain; }
	inline const vk::raii::SwapchainKHR& operator*() const  { return mSwapchain; }
	inline       vk::raii::SwapchainKHR* operator->()       { return &mSwapchain; }
	inline const vk::raii::SwapchainKHR* operator->() const { return &mSwapchain; }

	inline uint2                GetExtent() const { return mExtent; }
	inline vk::SurfaceFormatKHR GetFormat() const { return mSurfaceFormat; }
	inline void                 SetFormat(const vk::SurfaceFormatKHR format) { mSurfaceFormat = format; mDirty = true; }
	inline vk::PresentModeKHR   GetPresentMode() const { return mPresentMode; }
	inline void                 SetPresentMode(const vk::PresentModeKHR mode) { mPresentMode = mode; mDirty = true; }
	inline vk::ImageUsageFlags  GetImageUsage() const { return mUsage; }
	inline void                 SetImageUsage(const vk::ImageUsageFlags usage) { mUsage = usage; mDirty = true; }
	inline uint64_t             GetImageAvailableCounterValue() const { return mImageAvailableCounterValue; }

	inline uint32_t          GetMinImageCount() const { return mMinImageCount; }
	inline void              SetMinImageCount(uint32_t count) { mMinImageCount = count; mDirty = true; }
	inline uint32_t          GetImageCount() const { return (uint32_t)mImages.size(); }
	inline uint32_t          GetImageIndex() const { return mImageIndex; }
	inline const ref<Image>& GetImage() const { return mImages[mImageIndex]; }
	inline const ref<Image>& GetImage(uint32_t i) const { return mImages[i]; }

	inline bool IsDirty() const { return mDirty || mWindow.GetExtent() != GetExtent(); }
	bool Create();

	bool AcquireImage(const std::chrono::nanoseconds& timeout = std::chrono::nanoseconds(0));
	void Present(const vk::raii::Queue queue, const vk::ArrayProxy<const vk::Semaphore>& waitSemaphores = {});

private:
	vk::raii::SwapchainKHR mSwapchain = nullptr;
	uint2 mExtent;
	std::vector<ref<Image>> mImages;
	uint32_t mMinImageCount;
	uint32_t mImageIndex;
	uint64_t mImageAvailableCounterValue;
	vk::ImageUsageFlags mUsage;

	vk::SurfaceFormatKHR mSurfaceFormat;
	vk::PresentModeKHR mPresentMode;
	bool mDirty = false;
};

}