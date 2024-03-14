#include "Image.hpp"
#include "Buffer.hpp"

#include <span>

namespace RoseEngine {

std::vector<std::vector<Image::ResourceState>> CreateSubresourceStates(const ImageInfo& info) {
	return std::vector<std::vector<Image::ResourceState>>(
		info.arrayLayers,
		std::vector<Image::ResourceState>(
			info.mipLevels,
			Image::ResourceState{
				.layout = vk::ImageLayout::eUndefined,
				.stage  = vk::PipelineStageFlagBits2::eTopOfPipe,
				.access = vk::AccessFlagBits2::eNone,
				.queueFamily = info.queueFamilies.empty() ? VK_QUEUE_FAMILY_IGNORED : info.queueFamilies.front() }));
}

Image::Image(Device& device, const ImageInfo& info, const vk::MemoryPropertyFlags memoryFlags, const VmaAllocationCreateFlags allocationFlags) : mInfo(info) {
	VmaAllocationCreateInfo allocationCreateInfo {
		.flags = allocationFlags,
		.usage = VMA_MEMORY_USAGE_AUTO,
		.requiredFlags = (VkMemoryPropertyFlags)memoryFlags,
		.memoryTypeBits = 0,
		.pool = VK_NULL_HANDLE,
		.pUserData = VK_NULL_HANDLE,
		.priority = 0 };

	vk::ImageCreateInfo createInfo{
		.flags       = mInfo.createFlags,
		.imageType   = mInfo.type,
		.format      = mInfo.format,
		.extent      = mInfo.extent,
		.mipLevels   = mInfo.mipLevels,
		.arrayLayers = mInfo.arrayLayers,
		.samples     = mInfo.samples,
		.tiling      = mInfo.tiling,
		.usage       = mInfo.usage,
		.sharingMode = mInfo.sharingMode,
		.initialLayout = vk::ImageLayout::eUndefined };
	createInfo.setQueueFamilyIndices(mInfo.queueFamilies);

	vk::Result result = (vk::Result)vmaCreateImage(device.MemoryAllocator(), &(const VkImageCreateInfo&)createInfo, &allocationCreateInfo, &(VkImage&)mImage, &mAllocation, nullptr);
	if (result != vk::Result::eSuccess) {
		mImage = nullptr;
		mAllocation = nullptr;
		mMemoryAllocator = nullptr;
		return;
	}

	mMemoryAllocator = device.MemoryAllocator();
	mSubresourceStates = CreateSubresourceStates(mInfo);
}
Image::Image(const vk::Image image, const ImageInfo& info) : mImage(image), mInfo(info), mSubresourceStates(CreateSubresourceStates(mInfo)) {}
Image::~Image() {
	if (mMemoryAllocator && mImage && mAllocation) {
		vmaDestroyImage(mMemoryAllocator, mImage, mAllocation);
		mMemoryAllocator = nullptr;
		mImage = nullptr;
		mAllocation = nullptr;
	}
}

ImageView ImageView::Create(const Device& device, const ref<Image>& image, const vk::ImageSubresourceRange& subresource, const vk::ImageViewType type, const vk::ComponentMapping& componentMapping) {
	auto key = std::tie(subresource, type, componentMapping);
	auto it = image->mCachedViews.find(key);
	if (it == image->mCachedViews.end()) {
		vk::raii::ImageView v(*device, vk::ImageViewCreateInfo{
			.image = **image,
			.viewType = type,
			.format = image->Info().format,
			.components = componentMapping,
			.subresourceRange = subresource });
		it = image->mCachedViews.emplace(key, std::move(v)).first;
	}
	return ImageView{
		.mView = *it->second,
		.mImage = image,
		.mSubresource = subresource,
		.mType = type,
		.mComponentMapping = componentMapping
	};
}

}