#include "Swapchain.hpp"
#include "Window.hpp"

namespace RoseEngine {

ref<Swapchain> Swapchain::Create(const ref<Device>& device, const vk::SurfaceKHR surface, const uint32_t minImages, const vk::ImageUsageFlags imageUsage, const vk::SurfaceFormatKHR surfaceFormat, const vk::PresentModeKHR preferredPresentMode) {
	auto swapchain = make_ref<Swapchain>();
	swapchain->mMinImageCount = minImages;
	swapchain->mUsage = imageUsage;

	// select the format of the swapchain
	const auto formats = device->PhysicalDevice().getSurfaceFormatsKHR(surface);
	swapchain->mSurfaceFormat = formats.front();
	for (const vk::SurfaceFormatKHR& format : formats)
		if (format == surfaceFormat) {
			swapchain->mSurfaceFormat = format;
			break;
		}

	swapchain->mPresentMode = vk::PresentModeKHR::eFifo; // required to be supported
	for (const vk::PresentModeKHR& mode : device->PhysicalDevice().getSurfacePresentModesKHR(surface))
		if (mode == preferredPresentMode) {
			swapchain->mPresentMode = mode;
			break;
		}

	swapchain->mDirty = true;
	swapchain->mDevice = device;

	return swapchain;
}

bool Swapchain::Recreate(vk::SurfaceKHR surface, const std::vector<uint32_t>& queueFamilies) {
	// get the size of the swapchain
	const vk::SurfaceCapabilitiesKHR capabilities = mDevice->PhysicalDevice().getSurfaceCapabilitiesKHR(surface);
	if (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0 ||
		capabilities.currentExtent.width > mDevice->Limits().maxImageDimension2D || capabilities.currentExtent.height > mDevice->Limits().maxImageDimension2D)
		return false;

	mExtent = uint2(capabilities.currentExtent.width, capabilities.currentExtent.height);
	mMinImageCount = std::max(mMinImageCount, capabilities.minImageCount);

	vk::raii::SwapchainKHR oldSwapchain = std::move( mSwapchain );
	vk::SwapchainCreateInfoKHR info {
		.surface = surface,
		.minImageCount    = mMinImageCount,
		.imageFormat      = mSurfaceFormat.format,
		.imageColorSpace  = mSurfaceFormat.colorSpace,
		.imageExtent      = capabilities.currentExtent,
		.imageArrayLayers = 1,
		.imageUsage       = mUsage,
		.imageSharingMode = vk::SharingMode::eExclusive,
		.preTransform     = capabilities.currentTransform,
		.compositeAlpha   = vk::CompositeAlphaFlagBitsKHR::eOpaque,
		.presentMode      = mPresentMode,
		.clipped          = VK_FALSE,
		.oldSwapchain     = *oldSwapchain ? *oldSwapchain : nullptr,
	};
	info.setQueueFamilyIndices(queueFamilies);
	mSwapchain = std::move( vk::raii::SwapchainKHR(**mDevice, info) );

	oldSwapchain = nullptr;

	const auto images = mSwapchain.getImages();

	mImages.resize(images.size());
	for (uint32_t i = 0; i < mImages.size(); i++) {
		mImages[i] = ImageView::Create(
			Image::Create(***mDevice, images[i], ImageInfo{
				.format = mSurfaceFormat.format,
				.extent = uint3(mExtent, 1),
				.usage = info.imageUsage,
				.queueFamilies = queueFamilies }),
			vk::ImageSubresourceRange{
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1 });
		mDevice->SetDebugName(**mImages[i].GetImage(), "Swapchain image " + std::to_string(i));
		mDevice->SetDebugName(*mImages[i], "Swapchain image view " + std::to_string(i));
	}

	mImageIndex = 0;
	mDirty = false;
	return true;
}

bool Swapchain::AcquireImage(const std::chrono::nanoseconds& timeout) {
	mImageAvailableSemaphore = mCachedSemaphores.pop_or_create(*mDevice, [&]() { return make_ref<vk::raii::Semaphore>(**mDevice, vk::SemaphoreCreateInfo{}); });

	vk::Result result;
	std::tie(result, mImageIndex) = mSwapchain.acquireNextImage(timeout.count(), **mImageAvailableSemaphore);

	if (result == vk::Result::eNotReady || result == vk::Result::eTimeout)
		return false;
	else if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eErrorSurfaceLostKHR) {
		mDirty = true;
		return false;
	} else if (result != vk::Result::eSuccess)
		throw std::runtime_error("Failed to acquire image");

	return true;
}

void Swapchain::Present(const vk::Queue queue, const vk::ArrayProxy<const vk::Semaphore>& waitSemaphores) {
	vk::PresentInfoKHR info = {};
	info.setSwapchains(*mSwapchain);
	info.setImageIndices(mImageIndex);
	info.setWaitSemaphores(waitSemaphores);
	const vk::Result result = queue.presentKHR(&info);
	if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eErrorSurfaceLostKHR)
		mDirty = true;
	mCachedSemaphores.push(std::move(mImageAvailableSemaphore), mDevice->NextTimelineSignal());
}

}