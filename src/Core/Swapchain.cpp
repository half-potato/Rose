#include "Swapchain.hpp"

namespace RoseEngine {

ref<Swapchain> Swapchain::Create(Device& device, const vk::raii::SurfaceKHR& surface, const uint32_t minImages, const vk::ImageUsageFlags imageUsage, const vk::SurfaceFormatKHR surfaceFormat, const vk::PresentModeKHR presentMode) {
	auto swapchain = make_ref<Swapchain>();
	swapchain->mMinImageCount = minImages;
	swapchain->mUsage = imageUsage;

	// select the format of the swapchain
	const auto formats = device.GetPhysicalDevice().getSurfaceFormatsKHR(*surface);
	swapchain->mSurfaceFormat = formats.front();
	for (const vk::SurfaceFormatKHR& format : formats)
		if (format == surfaceFormat) {
			swapchain->mSurfaceFormat = format;
			break;
		}

	swapchain->mPresentMode = vk::PresentModeKHR::eFifo; // required to be supported
	for (const vk::PresentModeKHR& mode : mDevice.GetPhysicalDevice().getSurfacePresentModesKHR(*surface))
		if (mode == presentMode) {
			swapchain->mPresentMode = mode;
			break;
		}

	swapchain->Create();
	return swapchain;
}

bool Swapchain::Create() {
	// get the size of the swapchain
	const vk::SurfaceCapabilitiesKHR capabilities = mDevice.GetPhysicalDevice().getSurfaceCapabilitiesKHR(*mWindow.GetSurface());
	mExtent = capabilities.currentExtent;
	if (mExtent.x == 0 || mExtent.y == 0 || mExtent.x > mDevice.GetLimits().maxImageDimension2D || mExtent.height > mDevice.GetLimits().maxImageDimension2D)
		return false;

	mMinImageCount = max(mMinImageCount, capabilities.minImageCount);

	vk::raii::SwapchainKHR oldSwapchain = std::move( mSwapchain );

	vk::SwapchainCreateInfoKHR info = {};
	info.surface = *mWindow.GetSurface();
	if (*oldSwapchain) info.oldSwapchain = *oldSwapchain;
	info.minImageCount = mMinImageCount;
	info.imageFormat = mSurfaceFormat.format;
	info.imageColorSpace = mSurfaceFormat.colorSpace;
	info.imageExtent = vk::Extent2D{ mExtent.x, mExtent.y };
	info.imageArrayLayers = 1;
	info.imageUsage = mUsage;
	info.imageSharingMode = vk::SharingMode::eExclusive;
	info.preTransform = capabilities.currentTransform;
	info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	info.presentMode = mPresentMode;
	info.clipped = VK_FALSE;
	mSwapchain = std::move( vk::raii::SwapchainKHR(*mDevice, info) );

	oldSwapchain = nullptr;

	const std::vector<VkImage> images = mSwapchain.getImages();

	mDevice.mFramesInFlight = images.size();

	mImages.resize(images.size());
	mImageAvailableSemaphores.resize(images.size());
	for (uint32_t i = 0; i < mImages.size(); i++) {
		ImageInfo m = {
			.format = mSurfaceFormat.format,
			.extent = uint3(mExtent, 1),
			.usage = info.imageUsage,
			.queueFamilies = Window::FindSupportedQueueFamilies(mDevice.GetPhysicalDevice()) };
		mImages[i] = Image::Create(images[i], m);
		mImageAvailableSemaphores[i] = std::make_shared<vk::raii::Semaphore>(*mDevice, vk::SemaphoreCreateInfo{});
	}

	mImageIndex = 0;
	mImageAvailableSemaphoreIndex = 0;
	mDirty = false;
	return true;
}

bool Swapchain::AcquireImage(const std::chrono::nanoseconds& timeout) {
	vk::Result result;
	std::tie(result, mImageIndex) = mSwapchain.acquireNextImage(timeout.count(), **mImageAvailableSemaphores[semaphore]);

	if (result == vk::Result::eNotReady || result == vk::Result::eTimeout)
		return false;
	else if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eErrorSurfaceLostKHR) {
		mDirty = true;
		return false;
	} else if (result != vk::Result::eSuccess)
		throw std::runtime_error("Failed to acquire image");

	return true;
}

void Swapchain::Present(const vk::raii::Queue queue, const vk::ArrayProxy<const vk::Semaphore>& waitSemaphores) {
	vk::PresentInfoKHR info = {};
	info.setSwapchains(*mSwapchain);
	info.setImageIndices(mImageIndex);
	info.setWaitSemaphores(waitSemaphores);
	const vk::Result result = (*queue).presentKHR(&info);
	if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eErrorSurfaceLostKHR)
		mDirty = true;
}

}