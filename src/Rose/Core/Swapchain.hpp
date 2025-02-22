#pragma once

#include "Image.hpp"
#include "TransientResourceCache.hpp"

namespace RoseEngine {

class Swapchain {
public:
	static ref<Swapchain> Create(const ref<Device>& device, const vk::SurfaceKHR surface,
		const uint32_t             minImages              = 2,
		const vk::ImageUsageFlags  usage                  = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
		const vk::SurfaceFormatKHR preferredSurfaceFormat = vk::SurfaceFormatKHR(vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear),
		const vk::PresentModeKHR   preferredPresentMode   = vk::PresentModeKHR::eMailbox);

	inline       vk::raii::SwapchainKHR& operator*()        { return mSwapchain; }
	inline const vk::raii::SwapchainKHR& operator*() const  { return mSwapchain; }
	inline       vk::raii::SwapchainKHR* operator->()       { return &mSwapchain; }
	inline const vk::raii::SwapchainKHR* operator->() const { return &mSwapchain; }

	inline uint2                Extent() const { return mExtent; }
	inline vk::SurfaceFormatKHR GetFormat() const { return mSurfaceFormat; }
	inline void                 SetFormat(const vk::SurfaceFormatKHR format) { mSurfaceFormat = format; mDirty = true; }
	inline vk::PresentModeKHR   GetPresentMode() const { return mPresentMode; }
	inline void                 SetPresentMode(const vk::PresentModeKHR mode) { mPresentMode = mode; mDirty = true; }
	inline vk::ImageUsageFlags  GetImageUsage() const { return mUsage; }
	inline void                 SetImageUsage(const vk::ImageUsageFlags usage) { mUsage = usage; mDirty = true; }
	inline uint32_t             GetMinImageCount() const { return mMinImageCount; }
	inline void                 SetMinImageCount(uint32_t count) { mMinImageCount = count; mDirty = true; }

	inline uint32_t ImageCount() const { return (uint32_t)mImages.size(); }
	inline uint32_t ImageIndex() const { return mImageIndex; }
	inline const ImageView& CurrentImage() const { return mImages[mImageIndex]; }
	inline vk::Semaphore    ImageAvailableSemaphore() const { return **mImageAvailableSemaphore; }

	inline bool Dirty() const { return mDirty; }
	bool Recreate(const vk::SurfaceKHR surface, const std::vector<uint32_t>& queueFamilies);

	bool AcquireImage(const std::chrono::nanoseconds& timeout = std::chrono::nanoseconds(0));
	void Present(const vk::Queue queue, const vk::ArrayProxy<const vk::Semaphore>& waitSemaphores = {});

private:
	vk::raii::SwapchainKHR mSwapchain = nullptr;
	ref<Device> mDevice = nullptr;
	std::vector<ImageView> mImages = {};
	uint32_t mMinImageCount = 2;
	vk::ImageUsageFlags mUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
	uint2 mExtent = uint2(0);
	vk::SurfaceFormatKHR mSurfaceFormat = vk::SurfaceFormatKHR{
		.format = vk::Format::eB8G8R8A8Unorm,
		.colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear };
	vk::PresentModeKHR mPresentMode = vk::PresentModeKHR::eFifo;
	bool mDirty = true;

	uint32_t mImageIndex = 0;
	ref<vk::raii::Semaphore> mImageAvailableSemaphore;
	TransientResourceCache<ref<vk::raii::Semaphore>> mCachedSemaphores = {};
};

}